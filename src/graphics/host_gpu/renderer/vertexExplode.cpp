#include "graphics/host_gpu/renderer/vertexExplode.h"

#include "common/logging/log.h"
#include "common/switches.h"
#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/renderer/dispatchSnapshot.h"
#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "graphics/presentation/renderDoc.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/VertexExplodeLog.h"
#include "graphics/shader/recompiler/ir/VertexValueTrace.h"
#include "graphics/shader/shader.h"
#include "kernel/memory.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Libs::Graphics::VertexExplode {

namespace IR = ShaderRecompiler::IR;

namespace {

// What a red line tells about the last draw of a vertex-stage shader.
struct DrawInfo {
	uint64_t    frame = 0;
	uint64_t    draw  = 0;
	std::string text;
	// The first stage's fetch buffers, to print the raw bytes of a reported vertex.
	std::array<ShaderVertexInputBuffer, 4> buffers {};
	uint32_t                                buffer_count = 0;
	uint32_t                                target_width = 0, target_height = 0;
	// The index buffer, to find a reported vertex index that no index of the buffer can be.
	uint64_t index_address = 0;
	uint32_t index_count   = 0;
	uint32_t index_size    = 0;
	bool     index_gpu_args = false;
	// A copy of the 16-bit indices taken at the draw (up to 64Ki indices; empty: not readable).
	std::vector<uint16_t> indices;
	// Over all draws of the shader in this frame: draws, vertex offsets, indexed and 32-bit draws.
	uint32_t frame_draws = 0, indexed_draws = 0, index32_draws = 0, gpu_args_draws = 0;
	int32_t  min_vertex_offset = 0, max_vertex_offset = 0;
};

// The indices of a shader's CPU-args indexed draws in one frame (copies read without faulting),
// to find where a reported vertex index came from. The last three frames are kept: a record is
// read back a frame or two after its draw.
struct FrameIndices {
	uint64_t                           frame = 0;
	uint32_t                           draws16 = 0, draws32 = 0;
	uint32_t                           gpu_written = 0; // indexed draws whose indices the GPU wrote
	uint32_t                           unreadable  = 0; // other indexed draws not copied
	std::vector<std::vector<uint16_t>> copies16;
	std::vector<std::vector<uint32_t>> copies32;
	size_t                             copied = 0; // indices in the copies
};
constexpr size_t MaxCopiedIndices = 1u << 16u; // per shader and frame

std::atomic<uint32_t> g_index_type_source {0};

const char* IndexTypeSourceName(uint32_t source) {
	switch (source) {
		case 1: return "INDEX_TYPE packet";
		case 2: return "SET_UCONFIG VGT_INDEX_TYPE";
		case 3: return "SET_UCONFIG_REG_INDIRECT";
		default: return "unknown";
	}
}

// A GPU-written 32-bit index buffer to read back at the next safe point.
struct PendingSample {
	uint64_t hash = 0, frame = 0, address = 0;
	uint32_t count = 0, records = 0, source = 0;
};

struct ValueTraceDraw {
	uint64_t token = 0;
	uint64_t submit = 0;
	uint64_t frame = 0;
	uint32_t last_sequence = 0;
	bool reported = false;
	nlohmann::json metadata;
};

struct State {
	std::mutex                                 mutex;
	uint64_t                                   draws = 0;
	std::unordered_map<uint64_t, DrawInfo>     last_draw;   // by shader hash
	std::unordered_set<uint64_t>               reported;    // hash * 4 + kind (GPU records)
	std::unordered_set<uint64_t>               cpu_reported; // hash ^ index address
	std::unordered_map<uint64_t, uint64_t>     index_checked; // index address ^ count -> frame
	std::unordered_map<uint64_t, std::array<FrameIndices, 3>> frame_indices; // by shader hash
	std::vector<PendingSample>                 pending_samples;
	std::unordered_map<uint64_t, uint64_t>     last_sample; // hash -> frame
	std::array<uint64_t, 4>                    hits {};      // by kind, all records
	std::array<uint32_t, 4>                    shaders {};   // distinct shaders by kind
	uint64_t                                   cpu_hits         = 0;
	uint64_t                                   cpu_gpu_written  = 0;
	uint64_t                                   total            = 0;
	uint64_t                                   last_summary     = 0;
	std::array<ValueTraceDraw, IR::VertexTraceDrawSlots> value_traces;
	uint32_t value_trace_count = 0;
	bool value_trace_limit_reported = false;
	// Auto mode waits for the selected shader's first observed non-finite position, then
	// traces the following frame so its first 32 draws don't get consumed by earlier valid draws.
	std::atomic<uint64_t> value_trace_auto_frame {0};
};

State& GetState() {
	static State state;
	return state;
}

const char* KindName(uint32_t kind) {
	switch (static_cast<IR::VertexExplodeKind>(kind)) {
		case IR::VertexExplodeKind::NonFinite: return "nan/inf";
		case IR::VertexExplodeKind::WZero: return "w~0";
		case IR::VertexExplodeKind::Huge: return "huge";
		case IR::VertexExplodeKind::Zero: return "cull(xyw=0)";
		default: return "?";
	}
}

// Index buffer, fetch buffers and the first V#s of every vertex stage.
std::string DescribeDraw(const DrawNote& note, uint64_t draw) {
	std::string text = fmt::format("draw #{} frame {} prim={} inst={} first_inst={} voff={}", draw,
	                               note.frame, note.primitive, note.instance_count,
	                               note.first_instance, note.vertex_offset);
	text += fmt::format(" ps={:016x} rt={}x{} colors={} c0=0x{:x} depth=0x{:x}", note.ps_hash,
	                    note.target_width, note.target_height, note.color_count,
	                    note.color_address, note.depth_address);
	if (note.indexed) {
		text += fmt::format(" idx=0x{:x} n={} isize={}{}{}", note.index_address, note.index_count,
		                    note.index_size, note.gpu_args ? " gpu-args" : "",
		                    note.restart ? " restart" : "");
	} else {
		text += fmt::format(" verts={}", note.index_count);
	}
	for (const auto& stage: note.stages) {
		for (int i = 0; i < stage.buffers_num && i < 4; i++) {
			const auto& b = stage.buffers[i];
			text += fmt::format(" | vb{} 0x{:x} stride={} recs={}", i, b.addr, b.stride,
			                    b.num_records);
		}
		for (int i = 0; i < stage.resources_num && i < 4; i++) {
			const auto& r = stage.resources[i];
			text += fmt::format(" | attr{} 0x{:x} fmt={} stride={} recs={}", i, r.Base48(),
			                    static_cast<uint32_t>(r.RawFormat()), r.Stride(), r.NumRecords());
		}
		if (stage.stage.resources != nullptr) {
			const auto& buffers = stage.stage.resources->buffers;
			for (size_t i = 0; i < buffers.size() && i < 4; i++) {
				ShaderBufferResource r;
				std::memcpy(r.fields, buffers[i].dwords.data(), sizeof(r.fields));
				text += fmt::format(" | v#{} 0x{:x} fmt={} stride={} recs={}", i, r.Base48(),
				                    static_cast<uint32_t>(r.RawFormat()), r.Stride(),
				                    r.NumRecords());
			}
		}
	}
	return text;
}

bool IsFillPattern(uint32_t value, uint32_t size) {
	const auto byte = value & 0xffu;
	const auto all  = size == 2 ? byte * 0x0101u : byte * 0x01010101u;
	return value == all && value != 0;
}

// The CPU check: the largest index of a CPU-args indexed draw against the fetch buffers.
void CheckIndices(State& state, BufferCache& cache, const DrawNote& note, uint64_t vs_hash) {
	if (!note.indexed || note.gpu_args || note.index_count == 0 || note.stages.empty() ||
	    (note.index_address == 0 && note.index_host == nullptr)) {
		return;
	}
	const auto& stage = note.stages.front();
	if (stage.buffers_num == 0) {
		return;
	}
	const auto key = note.index_address ^ (uint64_t {note.index_count} << 40u) ^ vs_hash;
	if (auto found = state.index_checked.find(key);
	    found != state.index_checked.end() && note.frame - found->second < 60) {
		return;
	}
	state.index_checked[key] = note.frame;
	const uint64_t bytes = uint64_t {note.index_count} * note.index_size;
	if (note.index_host == nullptr && cache.IsRegionGpuModified(note.index_address, bytes)) {
		// The CPU copy of a GPU-written index buffer is stale.
		state.cpu_gpu_written++;
		return;
	}
	// Guest memory is read without faulting (a fault handler may wait for the GPU).
	std::vector<uint8_t> copy;
	const void*          data = note.index_host;
	if (data == nullptr) {
		copy.resize(bytes);
		if (!LibKernel::Memory::TryReadGpuCleanBacking(note.index_address, copy.data(), bytes)) {
			state.cpu_gpu_written++;
			return;
		}
		data = copy.data();
	}
	const uint32_t restart = note.index_size == 2 ? 0xffffu : 0xffffffffu;
	uint32_t       max_index = 0;
	uint32_t       patterns  = 0;
	for (uint32_t i = 0; i < note.index_count; i++) {
		uint32_t index = 0;
		if (note.index_size == 2) {
			index = static_cast<const uint16_t*>(data)[i];
		} else {
			index = static_cast<const uint32_t*>(data)[i];
		}
		if (note.restart && index == restart) {
			continue;
		}
		patterns += IsFillPattern(index, note.index_size) && index > 0xffu ? 1u : 0u;
		max_index = std::max(max_index, index);
	}
	const int64_t last = int64_t {max_index} + note.vertex_offset;
	for (int i = 0; i < stage.buffers_num; i++) {
		const auto& b = stage.buffers[i];
		if (b.stride == 0 || b.num_records == 0 || last < b.num_records) {
			continue;
		}
		state.cpu_hits++;
		if (!state.cpu_reported.insert(vs_hash ^ note.index_address ^ (uint64_t(i) << 56u)).second) {
			continue;
		}
		std::printf("\x1b[91mVtxExplode CPU: vs=%016" PRIx64 " index past vertex buffer: max_index=%u"
		            " (+voff %d = %" PRId64 ") >= vb%d records=%u (stride %u, 0x%" PRIx64
		            ", %" PRIu64 " bytes)%s%s | %s\x1b[0m\n",
		            vs_hash, max_index, note.vertex_offset, last, i, b.num_records, b.stride,
		            b.addr, uint64_t {b.stride} * b.num_records,
		            IsFillPattern(max_index, note.index_size) ? " PATTERN" : "",
		            patterns != 0 ? fmt::format(" ({} fill-pattern indices)", patterns).c_str() : "",
		            DescribeDraw(note, state.draws).c_str());
		std::fflush(stdout);
		LOGF("VtxExplode CPU: vs=%016" PRIx64 " max_index=%u voff=%d vb%d records=%u stride=%u\n",
		     vs_hash, max_index, note.vertex_offset, i, b.num_records, b.stride);
	}
}

} // namespace

bool Enabled() {
	return IR::VertexExplodeCheck().enabled;
}

// KYTY_VTX_EXPLODE_PROBE=<hash>:<sgpr>:<offset>: for draws of that vertex shader, prints the
// dword at <offset> of the buffer whose V# the shader gets in s[<sgpr>..<sgpr>+3] (read like the
// shader's own constant reads, GPU-written bytes read back), with the draw's index size and
// whether its index buffer is GPU-written. 64 lines, then one per 600 draws.
struct Probe {
	uint64_t hash = 0;
	uint32_t sgpr = 0, offset = 0;
};

const Probe& GetProbe() {
	static const Probe probe = [] {
		Probe result;
		if (const char* text = KYTY_GETENV("KYTY_VTX_EXPLODE_PROBE"); text != nullptr) {
			char* end    = nullptr;
			result.hash  = std::strtoull(text, &end, 16);
			if (end != nullptr && *end == ':') {
				result.sgpr = static_cast<uint32_t>(std::strtoul(end + 1, &end, 0));
			}
			if (end != nullptr && *end == ':') {
				result.offset = static_cast<uint32_t>(std::strtoul(end + 1, &end, 0));
			}
		}
		return result;
	}();
	return probe;
}

void RunProbe(BufferCache& cache, const DrawNote& note, const ShaderVertexInputInfo& stage) {
	const auto& probe = GetProbe();
	if (probe.hash == 0 || stage.stage.program == nullptr || stage.stage.resources == nullptr ||
	    stage.stage.program->shader_hash != probe.hash) {
		return;
	}
	static uint32_t count = 0;
	count++;
	if (count > 64 && count % 600 != 0) {
		return;
	}
	const auto& program = *stage.stage.program;
	const auto& data    = stage.stage.resources->user_data;
	const auto  first   = probe.sgpr - program.user_data_base;
	if (probe.sgpr < program.user_data_base || first + 4 > data.size()) {
		std::printf("VtxExplode probe: s%u is not in the user data%s", probe.sgpr, "\n");
		return;
	}
	ShaderBufferResource v;
	for (uint32_t i = 0; i < 4; i++) {
		v.fields[i] = data[first + i];
	}
	const uint64_t address = v.Base48() + probe.offset;
	uint32_t       value   = 0xdeadbeefu;
	const bool     gpu     = cache.IsRegionGpuModified(address, 4);
	LibKernel::Memory::ReadGuestOnGpuThread(address, &value, sizeof(value));
	const bool index_gpu = note.indexed && note.index_host == nullptr && note.index_address != 0 &&
	                       cache.IsRegionGpuModified(note.index_address,
	                                                 uint64_t {note.index_count} * note.index_size);
	// The permutation the draw uses (its compiled info is unique per permutation) and its buffer
	// descriptors: a buffer whose V# is empty reads zero in this permutation.
	const auto& buffers = stage.stage.resources->buffers;
	uint32_t    empty   = 0;
	std::string list;
	for (size_t i = 0; i < buffers.size(); i++) {
		ShaderBufferResource b;
		std::memcpy(b.fields, buffers[i].dwords.data(), sizeof(b.fields));
		empty += b.Base48() == 0 && b.NumRecords() == 0 ? 1u : 0u;
		list += fmt::format(" {}:{:x}/{}/{}", i, b.Base48(), b.Stride(), b.NumRecords());
	}
	std::printf("VtxExplode probe #%u: vs=%016" PRIx64 " frame %" PRIu64 " V#(s%u) 0x%" PRIx64
	            " +%u = 0x%08x (GPU-written %d) | indexed %d isize %u idx 0x%" PRIx64
	            " n=%u index GPU-written %d gpu-args %d | permutation %p, %zu buffers, %u empty:%s%s",
	            count, probe.hash, note.frame, probe.sgpr, v.Base48(), probe.offset, value, gpu,
	            note.indexed, note.index_size, note.index_address, note.index_count, index_gpu,
	            note.gpu_args, static_cast<const void*>(&program), buffers.size(), empty,
	            list.c_str(), "\n");
}

void NoteDraw(BufferCache& cache, const DrawNote& note) {
	auto&           state = GetState();
	std::lock_guard lock(state.mutex);
	state.draws++;
	for (const auto& stage: note.stages) {
		RunProbe(cache, note, stage);
	}
	std::string text;
	for (const auto& stage: note.stages) {
		if (stage.stage.program == nullptr) {
			continue;
		}
		const auto hash = stage.stage.program->shader_hash;
		auto&      info = state.last_draw[hash];
		if (info.frame != note.frame || info.draw == 0) {
			info.frame_draws = info.indexed_draws = info.index32_draws = info.gpu_args_draws = 0;
			info.min_vertex_offset = info.max_vertex_offset = note.vertex_offset;
			// Described once per frame and shader: the text costs more than the draw.
			if (text.empty()) {
				text = DescribeDraw(note, state.draws);
			}
			info.text = text;
			const auto& first = note.stages.front();
			info.buffer_count = static_cast<uint32_t>(std::clamp(first.buffers_num, 0, 4));
			info.target_width  = note.target_width;
			info.target_height = note.target_height;
			info.index_address  = note.indexed && note.index_host == nullptr ? note.index_address : 0;
			info.index_count    = note.index_count;
			info.index_size     = note.index_size;
			info.index_gpu_args = note.gpu_args;
			info.indices.clear();
			if (info.index_address != 0 && note.index_size == 2 && !note.gpu_args &&
			    note.index_count <= 65536u) {
				info.indices.resize(note.index_count);
				if (!LibKernel::Memory::TryReadGpuCleanBacking(note.index_address, info.indices.data(),
				                                               info.indices.size() * 2u)) {
					info.indices.clear();
				}
			}
			for (uint32_t i = 0; i < info.buffer_count; i++) {
				info.buffers[i] = first.buffers[i];
			}
		}
		info.frame = note.frame;
		info.draw  = state.draws;
		info.frame_draws++;
		info.indexed_draws += note.indexed ? 1u : 0u;
		info.index32_draws += note.indexed && note.index_size == 4 ? 1u : 0u;
		info.gpu_args_draws += note.gpu_args ? 1u : 0u;
		info.min_vertex_offset = std::min(info.min_vertex_offset, note.vertex_offset);
		info.max_vertex_offset = std::max(info.max_vertex_offset, note.vertex_offset);
		if (note.indexed && !note.gpu_args && note.index_host == nullptr && note.index_address != 0) {
			auto& slot = state.frame_indices[hash][note.frame % 3u];
			if (slot.frame != note.frame) {
				slot = FrameIndices {};
				slot.frame = note.frame;
			}
			(note.index_size == 4 ? slot.draws32 : slot.draws16)++;
			const uint64_t bytes = uint64_t {note.index_count} * note.index_size;
			if (cache.IsRegionGpuModified(note.index_address, bytes)) {
				slot.gpu_written++;
				auto& last = state.last_sample[hash];
				if (note.index_size == 4 && state.pending_samples.size() < 8 &&
				    (last == 0 || note.frame - last >= 60)) {
					last = note.frame;
					const auto& first = note.stages.front();
					state.pending_samples.push_back(
					    {hash, note.frame, note.index_address, std::min(note.index_count, 4096u),
					     first.buffers_num > 0 ? first.buffers[0].num_records : 0u,
					     g_index_type_source.load(std::memory_order_relaxed)});
				}
			} else if (slot.copied + note.index_count > MaxCopiedIndices) {
				slot.unreadable++;
			} else if (note.index_size == 4) {
				std::vector<uint32_t> copy(note.index_count);
				if (LibKernel::Memory::TryReadGpuCleanBacking(note.index_address, copy.data(), bytes)) {
					slot.copied += copy.size();
					slot.copies32.push_back(std::move(copy));
				} else {
					slot.unreadable++;
				}
			} else {
				std::vector<uint16_t> copy(note.index_count);
				if (LibKernel::Memory::TryReadGpuCleanBacking(note.index_address, copy.data(), bytes)) {
					slot.copied += copy.size();
					slot.copies16.push_back(std::move(copy));
				} else {
					slot.unreadable++;
				}
			}
		}
	}
	if (!note.stages.empty() && note.stages.front().stage.program != nullptr) {
		CheckIndices(state, cache, note, note.stages.front().stage.program->shader_hash);
	}
}

namespace {
std::filesystem::path ValueTraceDirectory() {
	const char* value = KYTY_GETENV("KYTY_VS_VALUE_TRACE_DIR");
	return value != nullptr && *value != '\0' ? value : "_vertex_value_traces";
}

void SaveValueTrace(const ValueTraceDraw& draw) {
	try {
		const auto directory = ValueTraceDirectory();
		std::filesystem::create_directories(directory);
		std::ofstream file(directory / fmt::format("draw-{}.json", draw.token), std::ios::binary);
		file << draw.metadata.dump(2) << '\n';
		if (!file) std::printf("VertexValueTrace: token=%llu metadata write failed\n", draw.token);
	} catch (const std::exception& error) {
		std::printf("VertexValueTrace: metadata write failed: %s\n", error.what());
	}
}
} // namespace

uint64_t RegisterValueTrace(RenderContext& context, const DrawNote& note, uint64_t submit) {
	const auto& settings = IR::VertexValueTrace();
	if (!settings.enabled) return 0;
	static const uint64_t first_frame = [] {
		const char* text = KYTY_GETENV("KYTY_VS_VALUE_TRACE_FRAME");
		return text != nullptr && std::strcmp(text, "auto") == 0 ? UINT64_MAX
		                                                               : (text != nullptr ? std::strtoull(text, nullptr, 0) : 0ull);
	}();
	if (first_frame != 0xffffffffffffffffull && note.frame < first_frame) return 0;
	bool selected = false;
	for (const auto& stage: note.stages)
		selected |= stage.stage.program != nullptr && stage.stage.program->bindings.vertex_value_trace;
	if (!selected) return 0;
	auto& state = GetState();
	if (first_frame == 0xffffffffffffffffull) {
		const auto automatic_frame = state.value_trace_auto_frame.load(std::memory_order_relaxed);
		if (automatic_frame == 0 || note.frame < automatic_frame) return 0;
	}
	std::lock_guard lock(state.mutex);
	if (state.value_trace_count == state.value_traces.size()) {
		if (!state.value_trace_limit_reported) {
			state.value_trace_limit_reported = true;
			std::printf("VertexValueTrace: draw budget exhausted (%u immutable slots); later draws have token=0\n",
			            IR::VertexTraceDrawSlots);
		}
		return 0;
	}
	auto& draw = state.value_traces[state.value_trace_count++];
	draw.token = state.value_trace_count;
	draw.submit = submit;
	draw.frame = note.frame;
	draw.metadata = {
	    {"schema", 1}, {"token", draw.token}, {"submit", submit}, {"frame", note.frame},
	    {"draw", DescribeDraw(note, state.draws)}, {"selected_shader", fmt::format("{:016x}", settings.hash)},
	    {"selected_vertex", settings.vertex}, {"selected_instance", settings.instance},
	    {"nf_trace_enabled", settings.nonfinite},
	    {"nf_trace_protocol", 2},
	    {"nf_trace_scope", "first invalid compiled scalar F32 expression for the owning invocation; guest EXEC unknown, may be inactive/unselected SSA"},
	    {"first_admitted_frame", first_frame},
	    {"selected_pcs", std::vector<uint32_t>(settings.pcs.begin(), settings.pcs.begin() + settings.pc_count)},
	    {"events", nlohmann::json::array()}, {"stages", nlohmann::json::array()},
	    {"coverage", "live selected loads, 3D sample coordinates/red, pre/post clip Position; no arithmetic trace or dead-load resurrection"},
	    {"image_predicate_contract", "selected_use or adjacent_selected_use filter; guest EXEC provenance is unknown"},
	    {"missing_event_interpretation", "missing event does not prove guest inactivity: absent index/instance, branch, selected-use filter, dead/unsupported hook or budget may explain it"},
	    {"half_coverage", "one invocation/half claims the slot; each event names that half; other half is not traced"},
	    {"status", "pending: selected vertex/instance may be absent or branch may not execute"}};
	draw.metadata["clip_control"] = {
	    {"raw_pa_cl_clip_cntl_available", false}, {"raw_pa_cl_clip_cntl", nullptr},
	    {"user_clip_planes", note.clip.user_clip_planes},
	    {"user_clip_plane_mode", note.clip.user_clip_plane_mode},
	    {"dx_clip_space", note.clip.dx_clip_space}, {"vertex_kill_any", note.clip.vertex_kill_any},
	    {"min_z_clip_disable", note.clip.min_z_clip_disable},
	    {"max_z_clip_disable", note.clip.max_z_clip_disable},
	    {"user_clip_plane_negate_y", note.clip.user_clip_plane_negate_y},
	    {"clip_disable", note.clip.clip_disable},
	    {"user_clip_plane_cull_only", note.clip.user_clip_plane_cull_only},
	    {"cull_on_clipping_error_disable", note.clip.cull_on_clipping_error_disable},
	    {"linear_attribute_clip_enable", note.clip.linear_attribute_clip_enable},
	    {"force_viewport_index_from_vs_enable", note.clip.force_viewport_index_from_vs_enable}};
	draw.metadata["rasterization_policy"] = {
	    {"effective_depth_clip_enable", note.effective_depth_clip_enable},
	    {"native_depth_clamp_enable", note.native_depth_clamp_enable},
	    {"negative_one_to_one", !note.clip.dx_clip_space},
	    {"source", "copied context getter and current native pipeline construction policy; readiness/recording not implied"}};
	draw.metadata["hardware_wave_flags"] = {
	    {"raw_vgt_shader_stages_en", note.shader_stages},
	    {"gs_w32_en", (note.shader_stages & 0x00400000u) != 0u},
	    {"es_uses_gs_wave_flag", true},
	    {"vs_w32_en", (note.shader_stages & 0x00800000u) != 0u},
	    {"hs_w32_en", (note.shader_stages & 0x00200000u) != 0u},
	    {"device_default_subgroup_size", note.device_default_subgroup_size},
	    {"vertex_stages", nlohmann::json::array()}};
	for (const auto& stage: note.stages) {
		draw.metadata["hardware_wave_flags"]["vertex_stages"].push_back({
		    {"logical_stage", static_cast<uint32_t>(stage.logical_stage)},
		    {"input_wave_size", stage.wave_size}, {"mesh_wave_size", stage.mesh.wave_size},
		    {"compiled_wave_size", stage.stage.program != nullptr
		                               ? nlohmann::json(stage.stage.program->wave_size) : nlohmann::json(nullptr)}});
	}
	// Each region is cleared once, before its draw; never reused or cleared by frame readback.
	auto* buffer = context.GetBufferCache().GetFaultBuffer();
	EXIT_IF(buffer == nullptr);
	const uint64_t offset = (uint64_t {IR::VertexTraceBaseWord} +
	                         (draw.token - 1u) * IR::VertexTraceSlotWords) * 4u;
	const uint64_t size = uint64_t {IR::VertexTraceSlotWords} * 4u;
	EXIT_IF(offset + size > buffer->Size());
	auto& scheduler = context.GetCommandScheduler();
	scheduler.EndRendering();
	const auto command = scheduler.Current().Handle();
	vk::BufferMemoryBarrier2 barrier;
	barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
	barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
	barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
	barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
	barrier.buffer = buffer->Handle(); barrier.offset = offset; barrier.size = size;
	vk::DependencyInfo dependency;
	dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
	command.pipelineBarrier2(dependency);
	command.fillBuffer(buffer->Handle(), offset, size, 0);
	barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
	barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
	barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
	barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
	command.pipelineBarrier2(dependency);
	SaveValueTrace(draw);
	std::printf("VertexValueTrace: token=%llu submit=%llu %s selected vertex=%u instance=%u; immutable slot initialized\n",
	            draw.token, submit, draw.metadata["draw"].get<std::string>().c_str(), settings.vertex, settings.instance);
	return draw.token;
}

void NoteValueTraceBindings(uint64_t token, std::span<PreparedBindings* const> stages) {
	if (token == 0 || token > IR::VertexTraceDrawSlots) return;
	auto& state = GetState();
	std::lock_guard lock(state.mutex);
	auto& draw = state.value_traces[token - 1u];
	const bool writer_history_enabled = DispatchSnapshot::WriterHistoryEnabled();
	const auto writer_stats = writer_history_enabled ? DispatchSnapshot::WriterHistoryStats()
	                                                : WriterHistory::Stats {};
	const auto writer_cutoff = writer_stats.last_sequence;
	if (writer_history_enabled) {
		draw.metadata["writer_history"] = {
		    {"enabled", true}, {"capacity", writer_stats.capacity}, {"retained", writer_stats.retained},
		    {"first_retained_sequence", writer_stats.first_retained_sequence},
		    {"reader_cutoff_sequence", writer_cutoff}, {"dropped", writer_stats.dropped},
		    {"rejected_invalid_ranges", writer_stats.rejected},
		    {"point", "admitted trace bindings before draw CommitBindings"},
		    {"order_scope", "CPU observation order; not GPU execution/completion"},
		    {"writer_scope", "declared written CS buffer ranges and existing PM4 write hooks; no guest CPU stores, untracked/BDA stores or optimized early-return paths"},
		    {"absence_interpretation", "no retained overlap does not prove no producer: eviction, untracked writer, address/range mismatch or CPU initialization are possible"}};
	}
	for (const auto* stage: stages) {
		if (stage == nullptr || stage->runtime == nullptr || stage->runtime->program == nullptr) continue;
		const auto& program = *stage->runtime->program;
		nlohmann::json entry = {
		    {"shader", fmt::format("{:016x}", program.shader_hash)},
		    {"token", stage->vertex_value_trace_token}, {"table", program.bindings.table},
		    {"token_dword", program.bindings.VertexTraceDword()}, {"shader_data", stage->shader_data},
		    {"push_data_start_dword", program.bindings.push_data_start_dword},
		    {"memory_offset_dword", program.bindings.memory_offset_dword},
		    {"memory_offset_count", program.bindings.memory_offset_count},
		    {"buffer_strides", program.bindings.buffer_strides},
		    {"buffer_formats", program.bindings.buffer_formats},
		    {"buffer_stride_dword", program.bindings.BufferStrideDword()},
		    {"buffer_format_dword", program.bindings.BufferFormatDword()},
		    {"buffers", nlohmann::json::array()}, {"source_present", bool(program.snapshot_source)}};
		for (size_t i = 0; i < stage->buffers.size(); ++i) {
			const auto& buffer = stage->buffers[i];
			const auto handle = static_cast<VkBuffer>(buffer.buffer);
			uint64_t opaque = 0;
			static_assert(sizeof(handle) <= sizeof(opaque));
			std::memcpy(&opaque, &handle, sizeof(handle));
			entry["buffers"].push_back({{"index", i}, {"buffer_handle", fmt::format("{:016x}", opaque)},
			                              {"offset", buffer.offset}, {"range", buffer.range}});
		}
		if (stage->runtime->resources != nullptr) {
			const auto& inputs = *stage->runtime->resources;
			entry["user_data_base"] = program.user_data_base;
			entry["user_data"] = inputs.user_data;
			entry["image_descriptors"] = nlohmann::json::array();
			entry["buffer_descriptors"] = nlohmann::json::array();
			entry["sampler_descriptors"] = nlohmann::json::array();
			for (const auto& descriptor: inputs.buffers)
				entry["buffer_descriptors"].push_back(descriptor.dwords);
			for (const auto& descriptor: inputs.images)
				entry["image_descriptors"].push_back(descriptor.dwords);
			for (const auto& descriptor: inputs.samplers)
				entry["sampler_descriptors"].push_back(descriptor.dwords);
			if (writer_history_enabled && program.bindings.vertex_value_trace) {
				constexpr size_t MaxReaderRanges = 64;
				entry["writer_history_reader_ranges"] = nlohmann::json::array();
				entry["writer_history_reader_ranges_skipped"] =
				    inputs.buffers.size() > MaxReaderRanges ? inputs.buffers.size() - MaxReaderRanges : 0;
				for (size_t i = 0; i < std::min(inputs.buffers.size(), MaxReaderRanges); ++i) {
					const auto& words = inputs.buffers[i];
					if (words.dword_count < 4) continue;
					const auto descriptor = DecodeNativeDescriptor<ShaderBufferResource>(words);
					const auto address = descriptor.Base48();
					const auto size = descriptor.GetSize();
					// Actual captures had 28 overlapping writers; the default newest-eight
					// view hid preceding producers. Export bounded older declarations too.
					const auto query = DispatchSnapshot::QueryWriterHistory(
					    address, size, writer_cutoff, WriterHistory::MaxMatches);
					nlohmann::json range = {
					    {"buffer_index", i}, {"guest_address", address}, {"guest_size", size},
					    {"valid_range", query.valid_range}, {"reader_cutoff_sequence", query.cutoff_sequence},
					    {"matching_records", query.matches}, {"matches_truncated", query.truncated},
					    {"scan_first_retained_sequence", query.stats.first_retained_sequence},
					    {"scan_last_sequence", query.stats.last_sequence},
					    {"scan_dropped", query.stats.dropped}, {"writers", nlohmann::json::array()}};
					for (const auto& writer: query.records) {
						nlohmann::json observation = {
						    {"sequence", writer.sequence}, {"source", writer.source.data()},
						    {"source_truncated", writer.source_truncated},
						    {"kind", writer.kind == WriterHistory::Kind::ComputeBuffer ? "compute_buffer" : "packet"},
						    {"guest_address", writer.address}, {"guest_size", writer.size},
						    {"detail", writer.detail}, {"overlap_address", std::max(address, writer.address)},
						    {"overlap_size", std::min(address + size, writer.address + writer.size) -
						                         std::max(address, writer.address)},
						    {"frame", writer.context_available ? nlohmann::json(writer.frame) : nlohmann::json(nullptr)},
						    {"tick_before_commit", writer.context_available ? nlohmann::json(writer.tick) : nlohmann::json(nullptr)},
						    {"submit", writer.context_available ? nlohmann::json(writer.submit) : nlohmann::json(nullptr)},
						    {"shader", writer.context_available ? nlohmann::json(fmt::format("{:016x}", writer.shader)) : nlohmann::json(nullptr)},
						    {"slot", writer.context_available ? nlohmann::json(writer.slot) : nlohmann::json(nullptr)},
						    {"groups", writer.context_available ? nlohmann::json(writer.groups) : nlohmann::json(nullptr)},
						    {"group_scope", !writer.context_available ? "unavailable at packet hook" :
						                    writer.indirect ? "CPU indirect input; actual command may differ" : "direct dispatch groups"},
						    {"phase", writer.context_available ? "dispatch_pre_commit" : "packet_hook"},
						    {"table", writer.table}, {"native_available", writer.native_available}};
						if (writer.native_available) {
							observation["native_buffer_handle"] = fmt::format("{:016x}", writer.native_buffer);
							observation["native_offset"] = writer.native_offset;
							observation["native_range"] = writer.native_range;
							observation["native_range_whole_size"] = writer.native_range == VK_WHOLE_SIZE;
							if (!program.bindings.table && i < entry["buffers"].size())
								observation["native_same_buffer_as_reader"] =
								    observation["native_buffer_handle"] == entry["buffers"][i]["buffer_handle"];
						}
						range["writers"].push_back(std::move(observation));
					}
					entry["writer_history_reader_ranges"].push_back(std::move(range));
				}
			}
		}
		if (program.bindings.vertex_value_trace && program.snapshot_source) {
			const auto source = std::static_pointer_cast<const DispatchSnapshot::Source>(program.snapshot_source);
			entry["spirv_words"] = source->spirv.size();
			entry["spirv_xxh3_64"] = fmt::format("{:016x}", XXH3_64bits(source->spirv.data(), source->spirv.size() * 4u));
			try {
				const auto directory = ValueTraceDirectory();
				std::filesystem::create_directories(directory);
				const auto name = fmt::format("draw-{}-{}.spv", token, entry["shader"].get<std::string>());
				std::ofstream file(directory / name, std::ios::binary);
				file.write(reinterpret_cast<const char*>(source->spirv.data()), source->spirv.size() * 4u);
				entry["spirv_file"] = name; entry["spirv_saved"] = bool(file);
			} catch (const std::exception& error) { entry["spirv_error"] = error.what(); }
		}
		draw.metadata["stages"].push_back(std::move(entry));
	}
	SaveValueTrace(draw);
}

void ReportValueTrace(const uint32_t* region, uint64_t frame) {
	auto& state = GetState();
	std::lock_guard lock(state.mutex);
	for (uint32_t slot = 0; slot < state.value_trace_count; ++slot) {
		auto& draw = state.value_traces[slot];
		const auto* header = region + slot * IR::VertexTraceSlotWords;
		if (header[0] == 0 || header[1] != draw.token || header[2] != 0) {
			if (!draw.reported) {
				draw.reported = true;
				draw.metadata["status"] = "no recorded event yet: selected index/instance absent, branch not reached, selected-use filter false, or untraced path; guest inactivity is not proved";
				SaveValueTrace(draw);
			}
			continue;
		}
		if (draw.last_sequence == header[6]) continue;
		draw.last_sequence = header[6];
		draw.metadata["readback_frame"] = frame;
		draw.metadata["truncated"] = header[5] != 0;
		draw.metadata["preclip_ready"] = header[7] != 0;
		draw.metadata["postclip_ready"] = header[8] != 0;
		draw.metadata["first_nonfinite_ready"] = header[9] != 0;
		draw.metadata["events"] = nlohmann::json::array();
		std::unordered_set<uint32_t> observed;
		for (uint32_t i = 0; i < IR::VertexTraceEvents; ++i) {
			const auto* event = header + IR::VertexTraceHeaderWords + i * IR::VertexTraceEventWords;
			if (event[0] != IR::VertexTraceMarker) continue;
			observed.insert(event[2]);
			const bool image = event[1] == static_cast<uint32_t>(IR::VertexTraceKind::Image);
			const bool nf = event[1] == static_cast<uint32_t>(IR::VertexTraceKind::NonFinite);
			const char* predicate_origin = nf ? "compiled_expression" : image
			    ? (event[7] == 1u ? "selected_use" : event[7] == 2u ? "adjacent_selected_use" : "unknown")
			    : "guest_exec_operand";
			draw.metadata["events"].push_back({{"kind", event[1]}, {"pc", event[2]}, {"sequence", event[3]},
			    {"vertex", event[4]}, {"instance", event[5]}, {"half", event[6]},
			    {"predicate_origin", predicate_origin}, {"predicate_origin_code", event[7]},
			    {"guest_exec_proven", !image && !nf},
			    {"guest_exec_active", image || nf ? nlohmann::json(nullptr) : nlohmann::json(event[10] != 0)},
			    {"resource", nf ? nlohmann::json(nullptr) : nlohmann::json(event[8])},
			    {"sampler", nf ? nlohmann::json(nullptr) : nlohmann::json(event[9])}, {"predicate_selected", event[10] != 0},
			    {"physical_subgroup_invocation", event[11]},
			    {"payload_bits", std::vector<uint32_t>(event + 12, event + 24)}});
			auto& saved = draw.metadata["events"].back();
			if (nf) {
				const auto map_hash = uint64_t {event[8]} | (uint64_t {event[9]} << 32u);
				const auto name = fmt::format("nf-ir-{:016x}-{:016x}.txt", IR::VertexValueTrace().hash, map_hash);
				saved["pc_available"] = false;
				saved["ir_map_xxh3_64"] = fmt::format("{:016x}", map_hash);
				saved["ir_map_file"] = name;
				std::error_code map_error;
				saved["ir_map_available"] = std::filesystem::exists(ValueTraceDirectory() / name, map_error);
				if (map_error) saved["ir_map_error"] = map_error.message();
				saved["result_bits"] = event[12];
				saved["opcode"] = event[13]; saved["block_index"] = event[14];
				saved["ir_ordinal"] = event[15]; saved["arg_count"] = event[16];
				saved["arg_flags"] = event[23]; saved["typed_args"] = nlohmann::json::array();
				for (uint32_t arg = 0; arg < std::min(event[16], 3u); ++arg)
					saved["typed_args"].push_back({{"type", event[17 + 2u * arg]},
					                               {"bits", event[18 + 2u * arg]},
					                               {"bits_available", (event[23] & (2u << arg)) == 0u}});
				saved["scope"] = "first invalid compiled expression; not proven first executed guest operation or exported dependency";
			} else saved["pc_available"] = true;
		}
		draw.metadata["missing_selected_pcs"] = nlohmann::json::array();
		const auto& settings = IR::VertexValueTrace();
		for (uint32_t i = 0; i < settings.pc_count; ++i)
			if (!observed.contains(settings.pcs[i])) draw.metadata["missing_selected_pcs"].push_back(settings.pcs[i]);
		draw.metadata["status"] = header[8] != 0 ? "stored Position observed" : "partial: Position export not yet observed";
		SaveValueTrace(draw);
		std::printf("VertexValueTrace: token=%llu frame=%llu events=%zu preclip=%u postclip=%u truncated=%u half=%u (other half not traced)\n",
		            draw.token, frame, draw.metadata["events"].size(), header[7], header[8], header[5],
		            draw.metadata["events"].empty() ? UINT32_MAX : draw.metadata["events"][0]["half"].get<uint32_t>());
		std::fflush(stdout);
	}
}

void Report(const uint32_t* region, uint64_t frame) {
	auto&           state = GetState();
	std::lock_guard lock(state.mutex);
	const auto      total = region[0];
	state.total += total;
	for (uint32_t slot = 0; slot < IR::ExplodeLogSlots; slot++) {
		const auto count = region[IR::ExplodeLogCounterWord + slot];
		if (count == 0) {
			continue;
		}
		const auto* record = region + IR::ExplodeLogRecordWord + slot * IR::ExplodeLogRecordWords;
		if ((record[0] & 0xffff0000u) != IR::ExplodeLogMarker) {
			continue;
		}
		const auto kind = record[0] & 0xffu;
		const bool mesh = (record[0] & 0x100u) != 0;
		const auto hash = uint64_t {record[1]} | (uint64_t {record[2]} << 32u);
		if (kind >= 4) {
			continue;
		}
		// Auto value-trace mode starts on the frame after the selected shader's first
		// observed non-finite position, avoiding slot exhaustion on earlier valid draws.
		if (kind == static_cast<uint32_t>(IR::VertexExplodeKind::NonFinite) &&
		    IR::VertexValueTrace().enabled && hash == IR::VertexValueTrace().hash) {
			uint64_t expected = 0;
			state.value_trace_auto_frame.compare_exchange_strong(expected, frame + 1u,
			                                                     std::memory_order_relaxed);
		}
		// Trace-layout vertex shaders write this immutable draw token into otherwise unused
		// record words. Report valid joins before the older per-hash/kind suppression: a
		// token0 hit can precede an admitted draw. Never interpret another stage/hash's words.
		static std::array<std::array<bool, 4>, IR::VertexTraceDrawSlots> joined_reported {};
		const auto trace_token = uint64_t {record[11]} | (uint64_t {record[12]} << 32u);
		if ((record[0] & 0xff00u) == 0u && IR::VertexTraceWanted(hash) && trace_token != 0u &&
		    trace_token <= state.value_trace_count && trace_token <= IR::VertexTraceDrawSlots) {
			auto& traced = state.value_traces[trace_token - 1u];
			const auto shader = fmt::format("{:016x}", hash);
			const bool stage_matches = traced.metadata.contains("stages") &&
			    std::any_of(traced.metadata["stages"].begin(), traced.metadata["stages"].end(),
			                [&](const auto& stage) {
				                return stage.value("shader", std::string {}) == shader &&
				                       stage.value("token", uint64_t {0}) == trace_token;
			                });
			if (traced.token == trace_token && stage_matches &&
			    traced.metadata.value("selected_shader", std::string {}) == shader &&
			    !joined_reported[trace_token - 1u][kind]) {
				joined_reported[trace_token - 1u][kind] = true;
				if (!traced.metadata.contains("legacy_position_reports"))
					traced.metadata["legacy_position_reports"] = nlohmann::json::array();
				traced.metadata["legacy_position_reports"].push_back({
				    {"draw_token", trace_token}, {"shader", shader}, {"stage", "vertex"},
				    {"kind", KindName(kind)}, {"readback_frame", frame}, {"hits", count},
				    {"vertex", record[3]}, {"instance", record[4]},
				    {"position_bits", std::vector<uint32_t>(record + 5, record + 9)},
				    {"base_vertex_present", record[10] == 1u}, {"base_vertex_bits", record[9]},
				    {"scope", "exact recorded draw token; first bucket writer, not all failing invocations"}});
				SaveValueTrace(traced);
				std::printf("VtxExplode trace join: token=%llu shader=%016" PRIx64
				            " stage=vertex kind=%s readback_frame=%llu vertex=%u instance=%u "
				            "position_bits=%08x,%08x,%08x,%08x | exact %s\n",
				            trace_token, hash, KindName(kind), frame, record[3], record[4],
				            record[5], record[6], record[7], record[8],
				            traced.metadata.value("draw", std::string("unknown")).c_str());
				std::fflush(stdout);
			}
		}
		const uint32_t capture_index = record[10] == 1u ? record[3] - record[9] : record[3];
		// KYTY_VTX_EXPLODE_CAPTURE (with --rd) requests RenderDoc captures, at most 3 per run and 300
		// frames apart: =<hash> at that shader's huge or non-finite positions at a vertex index above
		// 65535; =any at any shader's huge (w > 0) or non-finite position in a draw to a target of at
		// least 1280x720 (the main view, not the shadow maps).
		static const char*    capture_text = KYTY_GETENV("KYTY_VTX_EXPLODE_CAPTURE");
		static const bool     capture_any  = capture_text != nullptr && std::strcmp(capture_text, "any") == 0;
		static const uint64_t capture_hash =
		    capture_text != nullptr && !capture_any ? std::strtoull(capture_text, nullptr, 16) : 0u;
		static uint32_t       captures      = 0;
		static uint64_t       capture_frame = 0;
		const bool spike = kind == static_cast<uint32_t>(IR::VertexExplodeKind::Huge) ||
		                   kind == static_cast<uint32_t>(IR::VertexExplodeKind::NonFinite);
		const auto drawn     = state.last_draw.find(hash);
		const bool main_view = drawn != state.last_draw.end() && drawn->second.target_width >= 1280 &&
		                       drawn->second.target_height >= 720;
		if (spike && captures < 3 && (captures == 0 || frame - capture_frame >= 300) &&
		    ((capture_any && main_view && !mesh) ||
		     (capture_hash != 0 && hash == capture_hash && capture_index > 0xffffu))) {
			captures++;
			capture_frame = frame;
			std::printf("VtxExplode: requesting a RenderDoc capture for vs=%016" PRIx64 "%s", hash,
			            "\n");
			RenderDocRequestCapture();
		}
		state.hits[kind] += count;
		if (!state.reported.insert(hash * 4u + kind).second) {
			continue;
		}
		state.shaders[kind]++;
		std::string base_vertex;
		if (record[10] == 1u) {
			const auto base = static_cast<int32_t>(record[9]);
			base_vertex = fmt::format(" base_vertex={} index={}", base,
			                          static_cast<int64_t>(record[3]) - base);
		}
		const auto  x    = std::bit_cast<float>(record[5]);
		const auto  y    = std::bit_cast<float>(record[6]);
		const auto  z    = std::bit_cast<float>(record[7]);
		const auto  w    = std::bit_cast<float>(record[8]);
		const auto  draw = state.last_draw.find(hash);
		const auto  text = draw != state.last_draw.end() ? draw->second.text : std::string("draw unknown");
		const bool  cull = kind == static_cast<uint32_t>(IR::VertexExplodeKind::Zero);
		const char* on   = cull ? "" : "\x1b[91m";
		const char* off  = cull ? "" : "\x1b[0m";
		std::printf("%sVtxExplode: %s=%016" PRIx64 " kind=%s frame=%" PRIu64 " hits=%u %s=%u %s=%u"
		            "%s pos=(%g, %g, %g, %g) | last %s%s\n",
		            on, mesh ? "mesh" : "vs", hash, KindName(kind), frame, count,
		            mesh ? "invocation" : "vertex", record[3], mesh ? "group" : "instance",
		            record[4], base_vertex.c_str(), x, y, z, w, text.c_str(), off);
		// The reported vertex's bytes in each fetch buffer (as the CPU sees them now).
		if (draw != state.last_draw.end() && !mesh) {
			for (uint32_t i = 0; i < draw->second.buffer_count; i++) {
				const auto& b = draw->second.buffers[i];
				if (b.addr == 0 || b.stride == 0 || record[3] >= b.num_records) {
					continue;
				}
				// Read without faulting: this runs in a deferred scheduler callback, where a fault
				// handler waiting for the GPU would end the session.
				std::array<uint8_t, 32> bytes {};
				const auto size = std::min(b.stride, 32u);
				std::string hex;
				if (LibKernel::Memory::TryReadGpuCleanBacking(
				        b.addr + uint64_t {record[3]} * b.stride, bytes.data(), size)) {
					for (uint32_t k = 0; k < size; k++) {
						hex += fmt::format("{:02x}{}", bytes[k], (k % 4 == 3) ? " " : "");
					}
				} else {
					hex = "(not readable now: GPU-written or not on the GPU thread)";
				}
				std::printf("%s  vb%u[%u] @0x%" PRIx64 ": %s%s\n", on, i, record[3],
				            b.addr + uint64_t {record[3]} * b.stride, hex.c_str(), off);
			}
		}
		// A 16-bit indexed draw whose vertex index is above 65535 (+ the vertex offset in the draw
		// text): look for the index pair it would be if the GPU read the 16-bit indices as 32-bit.
		const uint32_t index = record[10] == 1u ? record[3] - record[9] : record[3];
		if (!mesh && index > 0xffffu) {
			// Every indexed draw of the shader in the last frames: is the index in a 32-bit buffer,
			// or the pair (low, high) at an even position of a 16-bit buffer?
			if (auto found = state.frame_indices.find(hash); found != state.frame_indices.end()) {
				for (const auto& slot: found->second) {
					if (slot.frame == 0) {
						continue;
					}
					uint32_t in32 = 0, pairs16 = 0;
					for (const auto& copy: slot.copies32) {
						in32 += static_cast<uint32_t>(std::count(copy.begin(), copy.end(), index));
					}
					for (const auto& copy: slot.copies16) {
						for (size_t k = 0; k + 1 < copy.size(); k += 2) {
							pairs16 += copy[k] == (index & 0xffffu) && copy[k + 1] == (index >> 16u) ? 1u : 0u;
						}
					}
					std::printf("%s  frame %" PRIu64 ": indexed draws 16-bit %u, 32-bit %u; GPU-written "
					            "%u, not copied %u; index found in 32-bit copies %ux, as 16-bit pair %ux%s%c",
					            on, slot.frame, slot.draws16, slot.draws32, slot.gpu_written,
					            slot.unreadable, in32, pairs16, off, 10);
				}
			}
		}
		if (draw != state.last_draw.end() && !mesh && index > 0xffffu &&
		    draw->second.index_size == 2 && draw->second.index_address != 0 &&
		    !draw->second.index_gpu_args) {
			const auto& info    = draw->second;
			const auto& indices = info.indices;
			std::string result;
			if (!indices.empty()) {
				const auto low  = index & 0xffffu;
				const auto high = index >> 16u;
				uint32_t   even = 0, odd = 0, max_index = 0;
				for (uint32_t k = 0; k < indices.size(); k++) {
					max_index = std::max<uint32_t>(max_index, indices[k]);
					if (k + 1 < indices.size() && indices[k] == low && indices[k + 1] == high) {
						((k % 2u) == 0 ? even : odd)++;
					}
				}
				result = fmt::format("16-bit indices max {}; pair ({}, {}) found {}x at even, {}x "
				                     "at odd positions",
				                     max_index, low, high, even, odd);
			} else {
				result = "no copy of the indices (GPU-written or more than 64Ki)";
			}
			std::printf("%s  index check: index %u (vertex - base vertex) > 65535 in a 16-bit draw (idx 0x%" PRIx64
			            " n=%u): %s; this shader's draws in frame %" PRIu64 ": %u (indexed %u, 32-bit %u, "
			            "gpu-args %u), vertex offsets %d..%d%s\n",
			            on, index, info.index_address, info.index_count, result.c_str(),
			            info.frame, info.frame_draws, info.indexed_draws, info.index32_draws,
			            info.gpu_args_draws, info.min_vertex_offset, info.max_vertex_offset, off);
		}
		LOGF("VtxExplode: %s=%016" PRIx64 " kind=%s frame=%" PRIu64 " hits=%u vertex=%u "
		     "instance=%u pos=(%g, %g, %g, %g) | last %s\n",
		     mesh ? "mesh" : "vs", hash, KindName(kind), frame, count, record[3], record[4], x,
		     y, z, w, text.c_str());
	}
	if (frame - state.last_summary >= 600) {
		state.last_summary = frame;
		std::printf("VtxExplode summary: frame %" PRIu64 " hits=%" PRIu64 " nan/inf=%" PRIu64
		            " (%u shaders) w~0=%" PRIu64 " (%u) huge=%" PRIu64 " (%u) zero/cull=%" PRIu64
		            " (%u) | cpu index hits=%" PRIu64 " gpu-written index buffers skipped=%" PRIu64
		            "\n",
		            frame, state.total, state.hits[0], state.shaders[0], state.hits[1],
		            state.shaders[1], state.hits[2], state.shaders[2], state.hits[3],
		            state.shaders[3], state.cpu_hits, state.cpu_gpu_written);
	}
	std::fflush(stdout);
}

void NoteIndexTypeSource(uint32_t source) {
	g_index_type_source.store(source, std::memory_order_relaxed);
}

void SampleGpuIndices() {
	std::vector<PendingSample> samples;
	{
		auto&           state = GetState();
		std::lock_guard lock(state.mutex);
		samples.swap(state.pending_samples);
	}
	for (const auto& s: samples) {
		std::vector<uint32_t> words(s.count);
		if (!LibKernel::Memory::TryReadBufferBacking(s.address, words.data(), words.size() * 4u)) {
			std::printf("VtxExplode GPU-idx32: vs=%016" PRIx64 " idx=0x%" PRIx64 " not readable\n",
			            s.hash, s.address);
			continue;
		}
		// valid: below the first fetch buffer's records (any value when it has none); pair: two
		// 16-bit halves that are both small and the high half not zero.
		uint32_t valid = 0, pairs = 0, other = 0;
		for (const auto w: words) {
			const auto lo = w & 0xffffu, hi = w >> 16u;
			if (s.records != 0 ? w < s.records : hi == 0) {
				valid++;
			} else if (hi != 0 && (s.records != 0 ? (lo < s.records && hi < s.records)
			                                     : (lo < 0x4000u && hi < 0x4000u))) {
				pairs++;
			} else {
				other++;
			}
		}
		std::string first;
		for (size_t k = 0; k < std::min<size_t>(words.size(), 12); k++) {
			first += fmt::format(" {:08x}", words[k]);
		}
		const bool bad = pairs != 0 || other != 0;
		std::printf("%sVtxExplode GPU-idx32: vs=%016" PRIx64 " frame %" PRIu64 " idx=0x%" PRIx64
		            " n=%u type from %s, vb0 records %u: valid %u, two-16-bit-like %u, other %u;"
		            " first:%s%s\n",
		            bad ? "\x1b[91m" : "", s.hash, s.frame, s.address, s.count,
		            IndexTypeSourceName(s.source), s.records, valid, pairs, other, first.c_str(),
		            bad ? "\x1b[0m" : "");
	}
	if (!samples.empty()) {
		std::fflush(stdout);
	}
}

} // namespace Libs::Graphics::VertexExplode
