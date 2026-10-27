#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h"

#include "common/logging/log.h"
#include "graphics/shader/recompiler/ir/VertexExplodeLog.h"
#include "graphics/shader/recompiler/ir/VertexValueTrace.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <xxhash.h>

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {
namespace {

bool UserDataDwordIndex(const EmitterState& state, IR::ScalarReg reg, uint32_t& dword_index) {
	const auto register_index = IR::RegIndex(reg);
	const auto& registers = state.program.bindings.user_data_registers;
	const auto  found     = std::lower_bound(registers.begin(), registers.end(), register_index);
	if (found == registers.end() || *found != register_index) {
		return false;
	}
	dword_index = static_cast<uint32_t>(found - registers.begin());
	return true;
}

uint32_t EmitBuiltinU32(EmitterState& state, IR::StageInputKind kind, uint32_t component) {
	if (kind == IR::StageInputKind::LocalInvocationIndex) {
		return EmitLocalInvocationIndex(state);
	}
	if (state.lane_count == 2 && (kind == IR::StageInputKind::LocalInvocationId ||
	                              kind == IR::StageInputKind::GlobalInvocationId)) {
		const auto* cs      = ShaderWorkgroupInput(state.program.stage, state.input_info);
		uint32_t    divisor = 1;
		for (uint32_t axis = 0; axis < component; axis++) {
			divisor *= std::max(cs->threads_num[axis], 1u);
		}
		const auto size    = std::max(cs->threads_num[component], 1u);
		const auto divided = EmitBinaryU32(state, spv::OpUDiv, EmitLocalInvocationIndex(state),
		                                   ConstantU32(state, divisor));
		const auto local   = EmitBinaryU32(state, spv::OpUMod, divided, ConstantU32(state, size));
		if (kind == IR::StageInputKind::LocalInvocationId) {
			return local;
		}
		const auto group = EmitInputComponentU32(state, IR::StageInputKind::WorkgroupId, component);
		return EmitAddU32(state, local,
		                  EmitBinaryU32(state, spv::OpIMul, group, ConstantU32(state, size)));
	}
	const bool centroid = kind == IR::StageInputKind::BaryCoordSmoothCentroid;
	const bool sample = kind == IR::StageInputKind::BaryCoordSmoothSample;
	const auto variable = InputVariableForKind(
	    state, centroid || sample ? IR::StageInputKind::BaryCoordSmooth : kind);
	if (variable == 0) {
		return ConstantU32(state, 0);
	}
	if (kind == IR::StageInputKind::FrontFacing) {
		const auto value = state.builder.AllocateId();
		const auto bits  = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLoad, TypeBool(state), value, variable);
		// PS5 initializes v_front_face with float +1.0/-1.0 bits.
		state.builder.AddFunction(spv::OpSelect, TypeU32(state), bits, value,
		                          ConstantU32(state, 0x3f800000u), ConstantU32(state, 0xbf800000u));
		return bits;
	}
	if (kind == IR::StageInputKind::VertexIndex || kind == IR::StageInputKind::InstanceIndex ||
	    kind == IR::StageInputKind::InvocationId || kind == IR::StageInputKind::PrimitiveId ||
	    kind == IR::StageInputKind::Layer || kind == IR::StageInputKind::SampleId) {
		const auto value = state.builder.AllocateId();
		const auto bits  = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLoad, TypeI32(state), value, variable);
		state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, value);
		return bits;
	}
	if (kind == IR::StageInputKind::FragCoord || kind == IR::StageInputKind::TessCoord) {
		const auto pointer = state.builder.AllocateId();
		const auto value   = state.builder.AllocateId();
		const auto bits    = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpAccessChain,
		                          TypePointer(state, spv::StorageClassInput, TypeF32(state)),
		                          pointer, variable, ConstantU32(state, component));
		state.builder.AddFunction(spv::OpLoad, TypeF32(state), value, pointer);
		state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, value);
		return bits;
	}
	if (centroid || sample || kind == IR::StageInputKind::BaryCoordSmooth ||
	    kind == IR::StageInputKind::BaryCoordNoPerspective) {
		const auto value   = state.builder.AllocateId();
		const auto bits    = state.builder.AllocateId();
		if (centroid || sample) {
			const auto coordinates = state.builder.AllocateId();
			state.builder.RequireCapability(spv::CapabilityInterpolationFunction);
			if (sample) {
				const auto sample_id = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpLoad, TypeI32(state), sample_id,
				                          InputVariableForKind(state, IR::StageInputKind::SampleId));
				state.builder.AddFunction(spv::OpExtInst, TypeF32Vector(state, 3), coordinates,
				                          GlslStd450(state), GLSLstd450InterpolateAtSample,
				                          variable, sample_id);
			} else {
				state.builder.AddFunction(spv::OpExtInst, TypeF32Vector(state, 3), coordinates,
				                          GlslStd450(state), GLSLstd450InterpolateAtCentroid, variable);
			}
			state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state), value,
			                          coordinates, component + 1u);
		} else {
			const auto pointer = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpAccessChain,
			                          TypePointer(state, spv::StorageClassInput, TypeF32(state)),
			                          pointer, variable, ConstantU32(state, component + 1u));
			state.builder.AddFunction(spv::OpLoad, TypeF32(state), value, pointer);
		}
		state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, value);
		return bits;
	}
	return EmitInputComponentU32(state, kind, component);
}

uint32_t EmitDppWriteCondition(ValueEmitContext& ctx, const IR::DppMoveFlags& flags,
                               uint32_t exec) {
	auto&      state      = ctx.state;
	const auto lane       = EmitSubgroupLocalInvocationId(state);
	const auto bank_shift = state.builder.AllocateId();
	const auto row_shift  = state.builder.AllocateId();
	const auto bank       = state.builder.AllocateId();
	const auto row        = state.builder.AllocateId();
	const auto bank_bit   = state.builder.AllocateId();
	const auto row_bit    = state.builder.AllocateId();
	const auto bank_hit   = state.builder.AllocateId();
	const auto row_hit    = state.builder.AllocateId();
	const auto bank_ok    = state.builder.AllocateId();
	const auto row_ok     = state.builder.AllocateId();
	const auto masks_ok   = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpShiftRightLogical, TypeU32(state), bank_shift, lane,
	                          ConstantU32(state, 2));
	state.builder.AddFunction(spv::OpShiftRightLogical, TypeU32(state), row_shift, lane,
	                          ConstantU32(state, 4));
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), bank, bank_shift,
	                          ConstantU32(state, 3));
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), row, row_shift,
	                          ConstantU32(state, 3));
	state.builder.AddFunction(spv::OpShiftLeftLogical, TypeU32(state), bank_bit,
	                          ConstantU32(state, 1), bank);
	state.builder.AddFunction(spv::OpShiftLeftLogical, TypeU32(state), row_bit,
	                          ConstantU32(state, 1), row);
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), bank_hit,
	                          ConstantU32(state, flags.bank_mask), bank_bit);
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), row_hit,
	                          ConstantU32(state, flags.row_mask), row_bit);
	state.builder.AddFunction(spv::OpINotEqual, TypeBool(state), bank_ok, bank_hit,
	                          ConstantU32(state, 0));
	state.builder.AddFunction(spv::OpINotEqual, TypeBool(state), row_ok, row_hit,
	                          ConstantU32(state, 0));
	state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(state), masks_ok, bank_ok, row_ok);
	uint32_t writable = masks_ok;
	if (!flags.bound_control) {
		const auto target  = EmitDppTargetLane(state, flags);
		const auto bounded = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(state), bounded, writable,
		                          target.valid);
		writable = bounded;
	}
	const auto result = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(state), result, exec, writable);
	return result;
}

uint32_t EmitAttribute(EmitterState& state, uint32_t attr, uint32_t chan) {
	const auto* input = InputBindingForParameter(state, attr);
	if (input == nullptr || input->variable_id == 0) {
		return ConstantU32(state, 0);
	}
	if (state.program.stage == ShaderType::Vertex || state.program.stage == ShaderType::Local) {
		return EmitVertexParameterComponentU32(state, *input, chan & 3u);
	}
	const auto load_per_vertex = [&](uint32_t vertex) {
		const auto pointer = state.builder.AllocateId();
		const auto value   = state.builder.AllocateId();
		state.builder.AddFunction(
		    spv::OpAccessChain, TypePointer(state, spv::StorageClassInput, TypeF32(state)), pointer,
		    input->variable_id, ConstantU32(state, vertex), ConstantU32(state, chan & 3u));
		state.builder.AddFunction(spv::OpLoad, TypeF32(state), value, pointer);
		return value;
	};
	if (input->per_vertex && PixelParameterIsFlat(state, attr)) {
		// A flat input read through raw vertices (it shares its vertex output with a smooth
		// input): the value of the provoking vertex, as the flat interpolation would give.
		const auto provoking =
		    state.input_info.pixel->parameter_mode == ShaderPixelParameterMode::LastVertex ? 2u : 0u;
		const auto bits = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, load_per_vertex(provoking));
		return bits;
	}
	if (input->per_vertex) {
		const auto barycentric_kind = state.input_info.pixel->ps_no_perspective
		                                  ? IR::StageInputKind::BaryCoordNoPerspective
		                                  : IR::StageInputKind::BaryCoordSmooth;
		const auto barycentric      = InputVariableForKind(state, barycentric_kind);
		uint32_t   sum              = 0;
		for (uint32_t vertex = 0; vertex < 3u; vertex++) {
			const auto pointer = state.builder.AllocateId();
			const auto weight  = state.builder.AllocateId();
			const auto product = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpAccessChain,
			                          TypePointer(state, spv::StorageClassInput, TypeF32(state)),
			                          pointer, barycentric, ConstantU32(state, vertex));
			state.builder.AddFunction(spv::OpLoad, TypeF32(state), weight, pointer);
			state.builder.AddFunction(spv::OpFMul, TypeF32(state), product, load_per_vertex(vertex),
			                          weight);
			if (vertex == 0u) {
				sum = product;
			} else {
				const auto next = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpFAdd, TypeF32(state), next, sum, product);
				sum = next;
			}
		}
		const auto bits = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, sum);
		return bits;
	}
	const auto vector    = state.builder.AllocateId();
	const auto component = state.builder.AllocateId();
	const auto bits      = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpLoad, TypeF32Vector(state, 4), vector, input->variable_id);
	state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state), component, vector,
	                          chan & 3u);
	state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, component);
	return bits;
}

uint32_t EmitInterpolationParameter(ValueEmitContext& ctx, uint32_t attr, uint32_t chan,
                                    uint32_t mode) {
	auto&       state = ctx.state;
	const auto* input = InputBindingForParameter(state, attr);
	if (!input->per_vertex) {
		return EmitAttribute(ctx.state, attr, chan);
	}
	const auto load_vertex = [&](uint32_t vertex) {
		const auto pointer = state.builder.AllocateId();
		const auto value   = state.builder.AllocateId();
		state.builder.AddFunction(
		    spv::OpAccessChain, TypePointer(state, spv::StorageClassInput, TypeF32(state)), pointer,
		    input->variable_id, ConstantU32(state, vertex), ConstantU32(state, chan & 3u));
		state.builder.AddFunction(spv::OpLoad, TypeF32(state), value, pointer);
		return value;
	};

	auto     selected_vertex = (mode + 1u) % 3u;
	// A flat parameter (mode 2) comes from the provoking vertex.
	if (mode == 2u && PixelParameterIsFlat(state, attr) &&
	    state.input_info.pixel->parameter_mode == ShaderPixelParameterMode::LastVertex) {
		selected_vertex = 2u;
	}
	uint32_t value = load_vertex(selected_vertex);
	if (!PixelParameterIsCustom(state, attr) && mode < 2u) {
		const auto delta = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpFSub, TypeF32(state), delta, value, load_vertex(0));
		value = delta;
	}
	const auto bits = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpBitcast, TypeU32(state), bits, value);
	return bits;
}

uint32_t MrtOutputMode(const EmitterState& state, const IR::ExportInfo& exp) {
	if (state.program.stage != ShaderType::Pixel || exp.kind != IR::ExportTargetKind::Mrt ||
	    exp.index >= std::size(state.input_info.pixel->target_output_mode)) {
		return 0;
	}
	return state.input_info.pixel->target_output_mode[exp.index];
}

uint32_t ExportRawComponent(ValueEmitContext& ctx, uint32_t vector, uint32_t component) {
	const auto value = ctx.state.builder.AllocateId();
	ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), value, vector,
	                              component);
	return value;
}

uint32_t ExportVector(ValueEmitContext& ctx, uint32_t data, const IR::ExportInfo& exp,
                      bool uint_output) {
	auto& state = ctx.state;
	if (exp.compr && !uint_output) {
		auto unpack = GLSLstd450UnpackHalf2x16;
		switch (MrtOutputMode(state, exp)) {
			case 5: unpack = GLSLstd450UnpackUnorm2x16; break;
			case 6: unpack = GLSLstd450UnpackSnorm2x16; break;
			default: break;
		}
		uint32_t f32[4] = {ConstantF32(state, 0), ConstantF32(state, 0), ConstantF32(state, 0),
		                   ConstantF32(state, 0x3f800000u)};
		for (uint32_t pair = 0; pair < 2u; pair++) {
			if ((exp.en & (3u << (pair * 2u))) == 0u) {
				continue;
			}
			const auto packed   = state.builder.AllocateId();
			const auto unpacked = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), packed, data, pair);
			state.builder.AddFunction(spv::OpExtInst, TypeF32Vector(state, 2), unpacked,
			                          GlslStd450(state), unpack, packed);
			for (uint32_t lane = 0; lane < 2u; lane++) {
				const auto component = pair * 2u + lane;
				if (((exp.en >> component) & 1u) != 0u) {
					f32[component] = state.builder.AllocateId();
					state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state),
					                          f32[component], unpacked, lane);
				}
			}
		}
		const auto vector = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeConstruct, TypeF32Vector(state, 4), vector,
		                          f32[0], f32[1], f32[2], f32[3]);
		return vector;
	}
	if (exp.compr || exp.en != 0xfu) {
		uint32_t raw[4] = {
		    ConstantU32(state, 0),
		    ConstantU32(state, 0),
		    ConstantU32(state, 0),
		    ConstantU32(state, uint_output ? 1u : 0x3f800000u),
		};
		if (exp.compr) {
			for (uint32_t pair = 0; pair < 2u; pair++) {
				if ((exp.en & (3u << (pair * 2u))) == 0u) {
					continue;
				}
				const auto packed = ExportRawComponent(ctx, data, pair);
				for (uint32_t lane = 0; lane < 2u; lane++) {
					const auto component = pair * 2u + lane;
					if (((exp.en >> component) & 1u) == 0u) {
						continue;
					}
					raw[component] = state.builder.AllocateId();
					state.builder.AddFunction(spv::OpBitFieldUExtract, TypeU32(state), raw[component],
					                          packed, ConstantU32(state, lane * 16u),
					                          ConstantU32(state, 16));
				}
			}
		} else {
			for (uint32_t component = 0; component < 4u; component++) {
				if (((exp.en >> component) & 1u) != 0u) {
					raw[component] = ExportRawComponent(ctx, data, component);
				}
			}
		}
		data = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeConstruct, TypeU32Vector(state, 4), data,
		                          raw[0], raw[1], raw[2], raw[3]);
	}
	if (uint_output) {
		return data;
	}
	const auto vector = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpBitcast, TypeF32Vector(state, 4), vector, data);
	return vector;
}

void EmitAuxPositionExport(ValueEmitContext& ctx, uint32_t data, const IR::ExportInfo& exp) {
	auto& state = ctx.state;
	for (uint32_t component = 0; component < 4; component++) {
		if ((exp.en & (1u << component)) == 0) {
			continue;
		}
		const auto output = IR::DecodePositionExportComponent(
		    state.input_info.vertex->pa_cl_vs_out_cntl, exp.index, component);
		if (output.layer || output.viewport) {
			const auto raw = ExportRawComponent(ctx, data, component);
			if (output.layer) {
				const auto layer = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), layer, raw,
				                          ConstantU32(state, 0x7ffu));
				const auto pointer = state.program.stage == ShaderType::Mesh
				                         ? MeshOutputPointer(state, IR::StageOutputKind::Layer)
				                         : state.layer_variable;
				state.builder.AddFunction(spv::OpStore, pointer, layer);
			}
			if (output.viewport) {
				// GFX10 MISC.z packs the viewport index in bits 16..19 alongside the layer.
				const auto viewport = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpBitFieldUExtract, TypeU32(state), viewport, raw,
				                          ConstantU32(state, 16), ConstantU32(state, 4));
				state.builder.AddFunction(spv::OpStore, state.viewport_index_variable, viewport);
			}
			continue;
		}
		if (!output.point_size && output.clip_distance == UINT32_MAX &&
		    output.cull_distance == UINT32_MAX) {
			continue;
		}

		const auto raw = ExportRawComponent(ctx, data, component);
		const auto f32 = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpBitcast, TypeF32(state), f32, raw);
		if (output.point_size) {
			state.builder.AddFunction(spv::OpStore, state.point_size_variable, f32);
			continue;
		}
		auto StoreDistance = [&](IR::StageOutputKind kind, uint32_t variable, uint32_t index) {
			if (index == UINT32_MAX) {
				return;
			}
			uint32_t pointer;
			if (state.program.stage == ShaderType::Mesh) {
				pointer = MeshOutputPointer(state, kind, index);
			} else {
				pointer = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpAccessChain,
				                          TypePointer(state, spv::StorageClassOutput, TypeF32(state)),
				                          pointer, variable, ConstantU32(state, index));
			}
			state.builder.AddFunction(spv::OpStore, pointer, f32);
		};
		StoreDistance(IR::StageOutputKind::ClipDistance, state.clip_distance_variable, output.clip_distance);
		StoreDistance(IR::StageOutputKind::CullDistance, state.cull_distance_variable, output.cull_distance);
	}
}

uint32_t ConvertClipCoordinate(EmitterState& state, uint32_t coordinate, float scale,
                               float offset, float half_extent) {
	const auto window  = state.builder.AllocateId();
	const auto biased  = state.builder.AllocateId();
	const auto divided = state.builder.AllocateId();
	const auto ndc     = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpFMul, TypeF32(state), window, coordinate,
	                          ConstantF32Value(state, scale));
	state.builder.AddFunction(spv::OpFAdd, TypeF32(state), biased, window,
	                          ConstantF32Value(state, offset));
	state.builder.AddFunction(spv::OpFDiv, TypeF32(state), divided, biased,
	                          ConstantF32Value(state, half_extent));
	state.builder.AddFunction(spv::OpFSub, TypeF32(state), ndc, divided,
	                          ConstantF32Value(state, 1.0f));
	return ndc;
}

uint32_t ConvertPositionToClipSpace(EmitterState& state, uint32_t position) {
	const auto& transform = state.input_info.vertex->clip_space;
	uint32_t    components[4] {};
	for (uint32_t i = 0; i < 4; i++) {
		components[i] = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state), components[i], position,
		                          i);
	}
	components[0] = ConvertClipCoordinate(state, components[0], transform.scale[0],
	                                      transform.offset[0], transform.half_extent[0]);
	components[1] = ConvertClipCoordinate(state, components[1], transform.scale[1],
	                                      transform.offset[1], transform.half_extent[1]);
	const auto converted = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpCompositeConstruct, TypeF32Vector(state, 4), converted,
	                          components[0], components[1], components[2], components[3]);
	return converted;
}

// KYTY_VTX_EXPLODE_CHECK: a position that explodes is logged into the fault buffer's explode
// region (layout in VertexExplodeLog.h); the position itself is written unchanged.
void EmitVertexExplodeCheck(EmitterState& state, uint32_t position) {
	if (state.fault_buffer_variable == 0 || !IR::UsesVertexExplodeLog(state.program)) {
		return;
	}
	static std::atomic_bool logged = false;
	if (!logged.exchange(true, std::memory_order_relaxed)) {
		Log::WriteToConsoleAndLog("Shader: emitted vertex explode check (KYTY_VTX_EXPLODE_CHECK)\n");
	}
	const auto boolean  = TypeBool(state);
	const auto booleans = TypeBoolVector(state, 4);
	const auto f32      = TypeF32(state);
	std::array<uint32_t, 4> value {};
	std::array<uint32_t, 4> magnitude {};
	for (uint32_t i = 0; i < 4; i++) {
		value[i] = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeExtract, f32, value[i], position, i);
		magnitude[i] = EmitGlsl<GLSLstd450FAbs, IR::Type::F32>(state, value[i]);
	}
	const auto is_zero = [&](uint32_t i) {
		return Binary(state, spv::OpFOrdEqual, boolean, value[i], ConstantF32Value(state, 0.0f));
	};
	const auto non_finite = Unary(
	    state, spv::OpAny, boolean,
	    Binary(state, spv::OpLogicalOr, booleans, Unary(state, spv::OpIsNan, booleans, position),
	           Unary(state, spv::OpIsInf, booleans, position)));
	// x, y and w zero (any z): the usual ways to cull a vertex, (0,0,0,0) and (0,0,10,0).
	const auto zero = Binary(state, spv::OpLogicalAnd, boolean,
	                         Binary(state, spv::OpLogicalAnd, boolean, is_zero(0), is_zero(1)),
	                         is_zero(3));
	const auto w_zero = Binary(state, spv::OpFOrdLessThan, boolean, magnitude[3],
	                           ConstantF32Value(state, 1e-6f));
	// Huge: in front of the camera (w > 1e-6) with |x| or |y| above limit * w. A point behind the
	// camera, however large, is clipped correctly.
	const auto xy_max = EmitGlsl<GLSLstd450FMax, IR::Type::F32>(state, magnitude[0], magnitude[1]);
	const auto huge   = Binary(
	    state, spv::OpLogicalAnd, boolean,
	    Binary(state, spv::OpFOrdGreaterThan, boolean, value[3], ConstantF32Value(state, 1e-6f)),
	    Binary(state, spv::OpFOrdGreaterThan, boolean, xy_max,
	           Binary(state, spv::OpFMul, f32, value[3],
	                  ConstantF32Value(state, IR::VertexExplodeCheck().limit))));
	const auto hit = Binary(state, spv::OpLogicalOr, boolean,
	                        Binary(state, spv::OpLogicalOr, boolean, non_finite, zero),
	                        Binary(state, spv::OpLogicalOr, boolean, w_zero, huge));
	const auto kind_constant = [&](IR::VertexExplodeKind kind) {
		return ConstantU32(state, static_cast<uint32_t>(kind));
	};
	const auto kind = Select(
	    state, TypeU32(state), non_finite, kind_constant(IR::VertexExplodeKind::NonFinite),
	    Select(state, TypeU32(state), zero, kind_constant(IR::VertexExplodeKind::Zero),
	           Select(state, TypeU32(state), w_zero, kind_constant(IR::VertexExplodeKind::WZero),
	                  kind_constant(IR::VertexExplodeKind::Huge))));
	EmitIfCondition(state, hit, [&] {
		const auto element = [&](uint32_t index) {
			const auto pointer = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpAccessChain, TypeStorageBufferElementPointer(state),
			                          pointer, state.fault_buffer_variable, ConstantU32(state, 0),
			                          index);
			return pointer;
		};
		const auto add = [&](uint32_t index) {
			const auto old = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpAtomicIAdd, TypeU32(state), old, element(index),
			                          ConstantU32(state, spv::ScopeDevice), ConstantU32(state, 0),
			                          ConstantU32(state, 1));
			return old;
		};
		const auto hash = state.program.shader_hash;
		(void)add(ConstantU32(state, IR::ExplodeLogBaseWord));
		const auto slot = EmitBinaryU32(
		    state, spv::OpIAdd, kind,
		    ConstantU32(state, IR::ExplodeLogBucket(hash) *
		                           static_cast<uint32_t>(IR::VertexExplodeKind::Count)));
		const auto first = Binary(
		    state, spv::OpIEqual, boolean,
		    add(EmitBinaryU32(state, spv::OpIAdd, slot,
		                      ConstantU32(state, IR::ExplodeLogBaseWord + IR::ExplodeLogCounterWord))),
		    ConstantU32(state, 0));
		EmitIfCondition(state, first, [&] {
			const bool mesh = state.program.stage == ShaderType::Mesh;
			const auto load_index = [&](IR::StageInputKind input) {
				const auto variable = InputVariableForKind(state, input);
				if (variable == 0) {
					return ConstantU32(state, 0xffffffffu);
				}
				const auto value = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpLoad, TypeI32(state), value, variable);
				return Unary(state, spv::OpBitcast, TypeU32(state), value);
			};
			const auto vertex   = mesh ? EmitLocalInvocationIndex(state)
			                           : load_index(IR::StageInputKind::VertexIndex);
			const auto instance = mesh ? EmitInputComponentU32(state, IR::StageInputKind::WorkgroupId, 0)
			                           : load_index(IR::StageInputKind::InstanceIndex);
			const auto base = EmitBinaryU32(
			    state, spv::OpIAdd,
			    EmitBinaryU32(state, spv::OpIMul, slot, ConstantU32(state, IR::ExplodeLogRecordWords)),
			    ConstantU32(state, IR::ExplodeLogBaseWord + IR::ExplodeLogRecordWord));
			const auto store = [&](uint32_t offset, uint32_t value) {
				state.builder.AddFunction(
				    spv::OpStore,
				    element(EmitBinaryU32(state, spv::OpIAdd, base, ConstantU32(state, offset))),
				    value);
			};
			store(0, EmitBinaryU32(state, spv::OpBitwiseOr, kind,
			                       ConstantU32(state, IR::ExplodeLogMarker | (mesh ? 0x100u : 0u))));
			store(1, ConstantU32(state, static_cast<uint32_t>(hash)));
			store(2, ConstantU32(state, static_cast<uint32_t>(hash >> 32u)));
			store(3, vertex);
			store(4, instance);
			for (uint32_t i = 0; i < 4; i++) {
				const auto component = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpCompositeExtract, f32, component, position, i);
				store(5 + i, Unary(state, spv::OpBitcast, TypeU32(state), component));
			}
			// Frame readback clears counters, not record payloads. Every first writer
			// must reset optional identity fields before recording its own values.
			for (uint32_t word = 9; word <= 12; ++word) store(word, ConstantU32(state, 0));
			if (state.explode_base_vertex_variable != 0) {
				const auto base_vertex = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpLoad, TypeI32(state), base_vertex,
				                          state.explode_base_vertex_variable);
				store(9, Unary(state, spv::OpBitcast, TypeU32(state), base_vertex));
				store(10, ConstantU32(state, 1));
				if (state.program.bindings.vertex_value_trace) {
					store(11, EmitShaderDataDwordLoad(state, state.program.bindings.VertexTraceDword()));
					store(12, EmitShaderDataDwordLoad(state, state.program.bindings.VertexTraceDword() + 1u));
				}
			}
		});
	});
}

} // namespace
uint32_t EmitWqmU64(EmitterState& state, uint32_t value) {
	const auto shifted_one = state.builder.AllocateId();
	const auto merged_one  = state.builder.AllocateId();
	const auto shifted_two = state.builder.AllocateId();
	const auto merged_two  = state.builder.AllocateId();
	const auto quad_bits   = state.builder.AllocateId();
	const auto result      = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpShiftRightLogical, TypeU64(state), shifted_one, value,
	                          ConstantU64(state, 0x0000000100000001ull));
	state.builder.AddFunction(spv::OpBitwiseOr, TypeU64(state), merged_one, value, shifted_one);
	state.builder.AddFunction(spv::OpShiftRightLogical, TypeU64(state), shifted_two, merged_one,
	                          ConstantU64(state, 0x0000000200000002ull));
	state.builder.AddFunction(spv::OpBitwiseOr, TypeU64(state), merged_two, merged_one,
	                          shifted_two);
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU64(state), quad_bits, merged_two,
	                          ConstantU64(state, 0x1111111111111111ull));
	state.builder.AddFunction(spv::OpIMul, TypeU64(state), result, quad_bits,
	                          ConstantU64(state, 0x0000000f0000000full));
	return result;
}

void EmitVertexValueTrace(ValueEmitContext& ctx, uint32_t kind, uint32_t pc,
                         uint32_t predicate, uint32_t predicate_origin,
                         uint32_t resource, uint32_t sampler,
                         const std::array<uint32_t, 12>& payload) {
	auto& state = ctx.state;
	if (!state.program.bindings.vertex_value_trace || state.fault_buffer_variable == 0) return;
	const auto zero = ConstantU32(state, 0);
	const auto one = ConstantU32(state, 1);
	const auto u32 = TypeU32(state);
	const auto boolean = TypeBool(state);
	const auto lo = EmitShaderDataDwordLoad(state, state.program.bindings.VertexTraceDword());
	const auto hi = EmitShaderDataDwordLoad(state, state.program.bindings.VertexTraceDword() + 1u);
	const auto vertex = EmitGetBuiltin(ctx, IR::Value(static_cast<uint32_t>(IR::StageInputKind::VertexIndex)), IR::Value(0u));
	const auto instance = EmitGetBuiltin(ctx, IR::Value(static_cast<uint32_t>(IR::StageInputKind::InstanceIndex)), IR::Value(0u));
	const auto and_bool = [&](uint32_t a, uint32_t b) { return Binary(state, spv::OpLogicalAnd, boolean, a, b); };
	auto selected = and_bool(predicate, Binary(state, spv::OpIEqual, boolean, hi, zero));
	selected = and_bool(selected, Binary(state, spv::OpUGreaterThan, boolean, lo, zero));
	selected = and_bool(selected, Binary(state, spv::OpULessThanEqual, boolean, lo,
	                                      ConstantU32(state, IR::VertexTraceDrawSlots)));
	if (IR::VertexValueTrace().vertex == UINT32_MAX) {
		// Wildcard mode admits only invalid F32 expressions or Position exports whose
		// predicate was restricted to a non-finite position by trace_position below.
		const bool allowed_kind = kind == static_cast<uint32_t>(IR::VertexTraceKind::NonFinite) ||
		                          kind == static_cast<uint32_t>(IR::VertexTraceKind::PreClip) ||
		                          kind == static_cast<uint32_t>(IR::VertexTraceKind::PostClip);
		selected = and_bool(selected, ConstantBool(state, allowed_kind));
	} else {
		selected = and_bool(selected, Binary(state, spv::OpIEqual, boolean, vertex,
		                                      ConstantU32(state, IR::VertexValueTrace().vertex)));
	}
	if (IR::VertexValueTrace().instance != UINT32_MAX) {
		selected = and_bool(selected, Binary(state, spv::OpIEqual, boolean, instance,
		                                      ConstantU32(state, IR::VertexValueTrace().instance)));
	}
	const auto base = EmitBinaryU32(state, spv::OpIAdd,
	    ConstantU32(state, IR::VertexTraceBaseWord),
	    EmitBinaryU32(state, spv::OpIMul, EmitBinaryU32(state, spv::OpISub, lo, one),
	                  ConstantU32(state, IR::VertexTraceSlotWords)));
	const auto element = [&](uint32_t index) {
		const auto pointer = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpAccessChain, TypeStorageBufferElementPointer(state),
		                          pointer, state.fault_buffer_variable, zero, index);
		return pointer;
	};
	const auto header = [&](uint32_t word) { return element(EmitBinaryU32(state, spv::OpIAdd, base, ConstantU32(state, word))); };
	const auto load = [&](uint32_t pointer) {
		const auto value = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLoad, u32, value, pointer);
		return value;
	};
	const auto store = [&](uint32_t pointer, uint32_t value) { state.builder.AddFunction(spv::OpStore, pointer, value); };
	const auto half = ctx.half;
	if (state.value_trace_claimed[half] == 0) {
		const auto pointer = TypePointer(state, spv::StorageClassPrivate, u32);
		state.value_trace_claimed[half] = state.builder.DefineGlobalVariable(pointer, spv::StorageClassPrivate, zero);
		state.value_trace_owner[half] = state.builder.DefineGlobalVariable(pointer, spv::StorageClassPrivate, zero);
	}
	EmitIfCondition(state, selected, [&] {
		EmitIfCondition(state, Binary(state, spv::OpIEqual, boolean,
		                              load(state.value_trace_claimed[half]), zero), [&] {
			store(state.value_trace_claimed[half], one);
			const auto previous = state.builder.AllocateId();
			state.builder.AddFunction(spv::OpAtomicCompareExchange, u32, previous, header(0),
			                          ConstantU32(state, spv::ScopeDevice), zero, zero, one, zero);
			const auto owner = Binary(state, spv::OpIEqual, boolean, previous, zero);
			store(state.value_trace_owner[half], Select(state, u32, owner, one, zero));
		});
		EmitIfCondition(state, Binary(state, spv::OpIEqual, boolean,
		                              load(state.value_trace_owner[half]), one), [&] {
			store(header(1), lo); store(header(2), hi);
			const bool image = kind == static_cast<uint32_t>(IR::VertexTraceKind::Image);
			const bool pre = kind == static_cast<uint32_t>(IR::VertexTraceKind::PreClip);
			const bool post = kind == static_cast<uint32_t>(IR::VertexTraceKind::PostClip);
			const bool nf = kind == static_cast<uint32_t>(IR::VertexTraceKind::NonFinite);
			const auto count_pointer = header(image ? 4u : 3u);
			const auto count = pre || post || nf ? zero : load(count_pointer);
			const auto capacity = ConstantU32(state, image ? 6u : IR::VertexValueTrace().nonfinite ? 23u : 24u);
			const auto available = nf ? Binary(state, spv::OpIEqual, boolean, load(header(9)), zero)
			    : pre || post ? state.builder.Constant(spv::OpConstantTrue, boolean)
			                  : Binary(state, spv::OpULessThan, boolean, count, capacity);
			EmitIfCondition(state, available, [&] {
				if (!pre && !post && !nf) store(count_pointer, EmitBinaryU32(state, spv::OpIAdd, count, one));
				const auto index = nf ? ConstantU32(state, 23u)
				    : pre || post ? ConstantU32(state, pre ? 30u : 31u)
				                                    : EmitBinaryU32(state, spv::OpIAdd, count, ConstantU32(state, image ? 24u : 0u));
				const auto record = EmitBinaryU32(state, spv::OpIAdd, base,
				    EmitBinaryU32(state, spv::OpIAdd, ConstantU32(state, IR::VertexTraceHeaderWords),
				                  EmitBinaryU32(state, spv::OpIMul, index, ConstantU32(state, IR::VertexTraceEventWords))));
				const auto put = [&](uint32_t word, uint32_t value) {
					store(element(EmitBinaryU32(state, spv::OpIAdd, record, ConstantU32(state, word))), value);
				};
				const auto sequence = load(header(6));
				store(header(6), EmitBinaryU32(state, spv::OpIAdd, sequence, one));
				put(1, ConstantU32(state, kind)); put(2, ConstantU32(state, pc)); put(3, sequence);
				put(4, vertex); put(5, instance); put(6, ConstantU32(state, half));
				put(7, ConstantU32(state, predicate_origin)); put(8, ConstantU32(state, resource));
				put(9, ConstantU32(state, sampler)); put(10, one);
				uint32_t physical_lane = ConstantU32(state, UINT32_MAX);
				if (state.subgroup_local_invocation_id_variable != 0) {
					physical_lane = state.builder.AllocateId();
					state.builder.AddFunction(spv::OpLoad, u32, physical_lane,
					                          state.subgroup_local_invocation_id_variable);
				}
				put(11, physical_lane);
				for (uint32_t i = 0; i < payload.size(); ++i) put(12u + i, payload[i]);
				put(0, ConstantU32(state, IR::VertexTraceMarker));
				if (pre || post) store(header(pre ? 7u : 8u), one);
				if (nf) store(header(9), one);
			});
			if (!nf) EmitIfCondition(state, Unary(state, spv::OpLogicalNot, boolean, available), [&] {
				// Saturated counts never wrap back into a live record slot.
				store(header(5), one);
			});
		});
	});
}

namespace {
void PrepareVertexTraceIrMap(EmitterState& state) {
	if (state.value_trace_ir_ready) return;
	state.value_trace_ir_ready = true;
	std::string map = "VertexValueTrace exact emitted IR map v1; ordinal matches %ID (1-based, including void instructions)\n";
	uint32_t ordinal = 1;
	for (uint32_t block = 0; block < state.program.blocks.size(); ++block) {
		for (const auto& inst: *state.program.blocks[block]) {
			state.value_trace_ir_ids.emplace(&inst, std::array<uint32_t, 2> {block, ordinal});
			map += fmt::format("%{} block={} cfg_id={} opcode={} opcode_id={} type={} type_id={} args={}\n",
			                   ordinal++, block, state.program.block_info[block].id,
			                   IR::ValueOpcodeName(inst.GetOpcode()), static_cast<uint32_t>(inst.GetOpcode()),
			                   IR::TypeName(inst.GetType()), static_cast<uint32_t>(inst.GetType()), inst.NumArgs());
		}
	}
	map += "\nIR:\n" + IR::ProgramToString(state.program);
	state.value_trace_ir_hash = XXH3_64bits(map.data(), map.size());
	// Bounded diagnostic artifacts; exhausted storage is reported, never a compile failure.
	static std::mutex mutex;
	static std::unordered_set<uint64_t> saved;
	static uint64_t bytes = 0;
	std::lock_guard lock(mutex);
	if (saved.contains(state.value_trace_ir_hash)) return;
	if (saved.size() >= 16u || map.size() > 32u * 1024u * 1024u - bytes) {
		std::printf("VertexValueTrace: exact IR map quota exhausted; map=%016" PRIx64 " unavailable\n", state.value_trace_ir_hash);
		return;
	}
	try {
		const char* text = KYTY_GETENV("KYTY_VS_VALUE_TRACE_DIR");
		const std::filesystem::path directory = text != nullptr && *text != '\0' ? text : "_vertex_value_traces";
		std::filesystem::create_directories(directory);
		const auto name = fmt::format("nf-ir-{:016x}-{:016x}.txt", state.program.shader_hash, state.value_trace_ir_hash);
		std::ofstream file(directory / name, std::ios::binary);
		file.write(map.data(), map.size());
		if (!file) {
			std::printf("VertexValueTrace: exact IR map write failed: %s\n", name.c_str());
			return;
		}
		saved.insert(state.value_trace_ir_hash); bytes += map.size();
	} catch (const std::exception& error) {
		std::printf("VertexValueTrace: exact IR map unavailable: %s\n", error.what());
	}
}

void EmitVertexTraceNonFinite(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto& state = ctx.state;
	if (!IR::VertexValueTrace().nonfinite || inst.GetType() != IR::Type::F32) return;
	// The compact helper has no emulated-half parameter. Vertex has no workgroup
	// input and therefore one emitter lane; explicitly decline other configurations.
	if (state.program.stage != ShaderType::Vertex || state.lane_count != 1u || ctx.half != 0u) {
		if (!state.value_trace_nf_unsupported) {
			state.value_trace_nf_unsupported = true;
			std::printf("VertexValueTrace: compact NF diagnostic unavailable for stage=%u lane_count=%u half=%u\n",
			            static_cast<uint32_t>(state.program.stage), state.lane_count, ctx.half);
		}
		return;
	}
	PrepareVertexTraceIrMap(state);
	const auto result = ctx.Result(inst);
	const auto invalid = Binary(state, spv::OpLogicalOr, TypeBool(state),
	    Unary(state, spv::OpIsNan, TypeBool(state), result),
	    Unary(state, spv::OpIsInf, TypeBool(state), result));
	std::array<uint32_t, 12> payload;
	payload.fill(ConstantU32(state, 0));
	const auto ids = state.value_trace_ir_ids.at(&inst);
	payload[0] = Unary(state, spv::OpBitcast, TypeU32(state), result);
	payload[1] = ConstantU32(state, static_cast<uint32_t>(inst.GetOpcode()));
	payload[2] = ConstantU32(state, ids[0]); payload[3] = ConstantU32(state, ids[1]);
	payload[4] = ConstantU32(state, static_cast<uint32_t>(inst.NumArgs()));
	uint32_t flags = inst.NumArgs() > 3u ? 1u : 0u;
	for (uint32_t i = 0; i < std::min<size_t>(inst.NumArgs(), 3u); ++i) {
		const auto type = inst.Arg(i).GetType();
		payload[5u + 2u * i] = ConstantU32(state, static_cast<uint32_t>(type));
		if (type == IR::Type::F32) payload[6u + 2u * i] = Unary(state, spv::OpBitcast, TypeU32(state), ctx.Arg(inst, i));
		else if (type == IR::Type::U1) payload[6u + 2u * i] = Select(state, TypeU32(state), ctx.Arg(inst, i), ConstantU32(state, 1), ConstantU32(state, 0));
		else if (type == IR::Type::U8 || type == IR::Type::U16 || type == IR::Type::U32 || type == IR::Type::F16)
			payload[6u + 2u * i] = ctx.Arg(inst, i);
		else flags |= 2u << i; // Wide/opaque argument is not represented by one DWORD.
	}
	payload[11] = ConstantU32(state, flags);
	if (state.value_trace_nf_function == 0) state.value_trace_nf_function = state.builder.AllocateId();
	// Keep the large bounded writer out of every expression site. Parameters are
	// already-live raw values/metadata; this adds no guest load or result replacement.
	EmitIfCondition(state, invalid, [&] {
		std::vector<uint32_t> call {spv::OpFunctionCall, TypeVoid(state), state.builder.AllocateId(),
		                            state.value_trace_nf_function};
		call.insert(call.end(), payload.begin(), payload.end());
		state.builder.AddFunction(call);
	});
}

std::optional<IR::Value> ImageSelectedUsePredicate(const IR::Inst& image) {
	std::optional<IR::Value> condition;
	for (const auto& use: image.Uses()) {
		if (use.user->GetOpcode() != IR::ValueOpcode::CompositeExtractU32x4) continue;
		for (const auto& extract_use: use.user->Uses()) {
			const auto* select = extract_use.user;
			if (select->GetOpcode() != IR::ValueOpcode::SelectU32 ||
			    select->Arg(1).ResolveInstruction() != use.user) continue;
			if (condition && *condition != select->Arg(0)) return std::nullopt;
			condition = select->Arg(0);
		}
	}
	return condition;
}
}

void EmitVertexTraceNonFiniteHelper(EmitterState& state) {
	if (state.value_trace_nf_function == 0) return;
	const auto u32 = TypeU32(state);
	const auto type = state.builder.Type(spv::OpTypeFunction, TypeVoid(state),
	    u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);
	state.builder.AddFunction(spv::OpFunction, TypeVoid(state), state.value_trace_nf_function,
	                          spv::FunctionControlDontInlineMask, type);
	std::array<uint32_t, 12> payload;
	for (auto& param: payload) {
		param = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpFunctionParameter, u32, param);
	}
	const auto* previous_block = state.current_block;
	const auto previous_half = state.lane_half;
	state.current_block = nullptr;
	state.lane_half = 0;
	EmitLabel(state, state.builder.AllocateId());
	ValueEmitContext ctx(state);
	EmitVertexValueTrace(ctx, static_cast<uint32_t>(IR::VertexTraceKind::NonFinite), UINT32_MAX,
	    ConstantBool(state, true), 3u, static_cast<uint32_t>(state.value_trace_ir_hash),
	    static_cast<uint32_t>(state.value_trace_ir_hash >> 32u), payload);
	state.builder.AddFunction(spv::OpReturn);
	state.builder.AddFunction(spv::OpFunctionEnd);
	state.current_block = previous_block;
	state.lane_half = previous_half;
}

void EmitVertexTraceLiveResult(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto& state = ctx.state;
	if (!state.program.bindings.vertex_value_trace) return;
	EmitVertexTraceNonFinite(ctx, inst);
	const auto opcode = inst.GetOpcode();
	const bool image = opcode == IR::ValueOpcode::ImageSampleRaw;
	const bool buffer = IR::BufferAccessOf(opcode) == IR::BufferAccess::Read;
	if (!image && !buffer) return;
	const auto flags = inst.Flags<IR::MemoryFlags>();
	if (!IR::VertexTracePcWanted(flags.pc)) return;
	const auto& mem = ctx.Memory(inst);
	std::array<uint32_t, 12> payload;
	payload.fill(ConstantU32(state, 0));
	uint32_t predicate = 0, origin = 1;
	if (image) {
		// Initial collection understands only the decoded 32-bit red D3 sample_lz shape.
		// A materialized D2 placeholder is still recorded, with both dimensions below.
		if (mem.image_dimension != Decoder::ImageDimension::Dim3D ||
		    mem.image_sample_flags != Decoder::ImageSampleFlagLevelZero || mem.dmask != 1u ||
		    mem.image_address_components != 3u || mem.data_bits != 32u || mem.data_dwords != 1u ||
		    mem.image_has_mip || mem.image_r128) {
			std::printf("VertexValueTrace: pc=0x%08x image skipped: diagnostic requires decoded D3 sample_lz dmask1/32-bit/plain xyz (dimension=%u flags=0x%x dmask=0x%x components=%u bits=%u r128=%u mip=%u)\n",
			            flags.pc, static_cast<uint32_t>(mem.image_dimension), mem.image_sample_flags,
			            mem.dmask, mem.image_address_components, mem.data_bits,
			            uint32_t(mem.image_r128), uint32_t(mem.image_has_mip));
			return;
		}
		auto condition = ImageSelectedUsePredicate(inst);
		if (!condition) {
			// An adjacent sample's selected-use condition is only a diagnostic filter.
			// Neither this fallback nor a direct selected use proves the image's guest EXEC.
			for (const auto& neighbor: *state.current_block) {
				if (neighbor.GetOpcode() != IR::ValueOpcode::ImageSampleRaw) continue;
				const auto pc = neighbor.Flags<IR::MemoryFlags>().pc;
				if (uint64_t(pc) + 8u != flags.pc && uint64_t(flags.pc) + 8u != pc) continue;
				if (auto found = ImageSelectedUsePredicate(neighbor)) { condition = found; origin = 2; break; }
			}
		}
		if (!condition) {
			std::printf("VertexValueTrace: pc=0x%08x image skipped: selected-use predicate unavailable (guest EXEC unknown)\n", flags.pc);
			return;
		}
		if (const auto* producer = condition->ResolveInstruction();
		    producer != nullptr && !ctx.definitions.contains(producer)) {
			std::printf("VertexValueTrace: pc=0x%08x image skipped: selected-use predicate not emitted yet (guest EXEC unknown)\n", flags.pc);
			return;
		}
		predicate = ctx.Def(*condition);
		const auto* address = ctx.ImageAddress(inst.Arg(2));
		if (address == nullptr || address->NumArgs() < 3u || mem.image_address_components != 3u) {
			std::printf("VertexValueTrace: pc=0x%08x image skipped: initial trace supports 3-component addresses\n", flags.pc);
			return;
		}
		for (uint32_t i = 0; i < 3u; ++i) payload[i] = ctx.Def(address->Arg(i));
		const auto result = ctx.Result(inst);
		const auto red = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), red, result, 0u);
		payload[3] = red;
		payload[4] = ConstantU32(state, mem.image_sample_flags);
		payload[5] = ConstantU32(state, mem.dmask);
		payload[6] = ConstantU32(state, static_cast<uint32_t>(mem.image_dimension));
		payload[7] = ConstantU32(state, static_cast<uint32_t>(state.program.info.images.at(mem.resource).dimension));
	} else {
		if (inst.NumArgs() != 5u || inst.Arg(4).GetType() != IR::Type::U1) return;
		predicate = ctx.Arg(inst, 4);
		payload[0] = ctx.Arg(inst, 1); payload[1] = ctx.Arg(inst, 2); payload[2] = ctx.Arg(inst, 3);
		payload[3] = ConstantU32(state, mem.offset);
		const auto result = ctx.Result(inst);
		uint32_t components = inst.GetType() == IR::Type::U32x4 ? 4u :
		                      inst.GetType() == IR::Type::U32x3 ? 3u :
		                      inst.GetType() == IR::Type::U32x2 ? 2u : 1u;
		for (uint32_t i = 0; i < components; ++i) {
			payload[4u + i] = result;
			if (components != 1u) {
				payload[4u + i] = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), payload[4u + i], result, i);
			}
		}
		payload[8] = ConstantU32(state, components);
		payload[9] = ConstantU32(state, mem.data_bits);
		payload[10] = ConstantU32(state, uint32_t(mem.idxen) | (uint32_t(mem.offen) << 1u) |
		                                     (uint32_t(mem.formatted) << 2u) | (uint32_t(mem.typed) << 3u));
		payload[11] = ConstantU32(state, mem.data_dwords);
	}
	EmitVertexValueTrace(ctx, static_cast<uint32_t>(image ? IR::VertexTraceKind::Image : IR::VertexTraceKind::Buffer),
	                     flags.pc, predicate, origin, mem.resource, mem.sampler, payload);
}

void EmitSetAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto&       state = ctx.state;
	const auto& exp   = ctx.Export(inst);
	const auto  exec  = ctx.Arg(inst, 1);
	if (state.program.stage == ShaderType::Pixel && exp.vm && state.requirements.pixel_valid_mask &&
	    state.pixel_valid_mask_variable != 0) {
		const auto value = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpSelect, TypeU32(state), value, exec, ConstantU32(state, 1),
		                          ConstantU32(state, 0));
		state.builder.AddFunction(spv::OpStore, state.pixel_valid_mask_variable, value);
	}
	if (exp.kind == IR::ExportTargetKind::Null || exp.en == 0u) {
		return;
	}
	// Skip dormant color exports after their valid mask; MRT1 is reserved for logical alpha.
	if (state.program.stage == ShaderType::Pixel && exp.kind == IR::ExportTargetKind::Mrt &&
	    exp.index != 0 && state.input_info.pixel->alpha_blend_source != ShaderAlphaBlendSource::None) {
		return;
	}
	EmitIfCondition(state, exec, [&]() {
		const auto data = ctx.Arg(inst, 0);
		if (exp.kind == IR::ExportTargetKind::Primitive) {
			if (state.program.stage == ShaderType::Mesh) {
				state.builder.AddFunction(spv::OpStore, MeshPrimitivePointer(state),
				                          ExportRawComponent(ctx, data, 0));
			}
			return;
		}
		if (exp.kind == IR::ExportTargetKind::Position && exp.index != 0) {
			EmitAuxPositionExport(ctx, data, exp);
			return;
		}
		if (exp.kind == IR::ExportTargetKind::MrtZ) {
			if ((exp.en & 1u) != 0u && state.depth_variable != 0) {
				const auto raw = ExportRawComponent(ctx, data, 0);
				const auto f32 = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpBitcast, TypeF32(state), f32, raw);
				state.builder.AddFunction(spv::OpStore, state.depth_variable, f32);
			}
			if ((exp.en & 4u) != 0u && state.sample_mask_variable != 0) {
				const auto raw     = ExportRawComponent(ctx, data, 2);
				const auto value   = state.builder.AllocateId();
				const auto pointer = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpBitcast, TypeI32(state), value, raw);
				state.builder.AddFunction(
				    spv::OpAccessChain, TypePointer(state, spv::StorageClassOutput, TypeI32(state)),
				    pointer, state.sample_mask_variable, ConstantU32(state, 0));
				state.builder.AddFunction(spv::OpStore, pointer, value);
			}
			return;
		}
		const auto variable =
		    state.program.stage == ShaderType::Mesh ? 0u : OutputVariableForExport(state, exp);
		if (state.program.stage != ShaderType::Mesh && variable == 0) {
			return;
		}
		const bool uint_output = MrtOutputMode(state, exp) == 7u;
		const auto vector_type = uint_output ? TypeU32Vector(state, 4) : TypeF32Vector(state, 4);
		auto       value       = ExportVector(ctx, data, exp, uint_output);
		if (state.program.stage == ShaderType::Pixel && exp.kind == IR::ExportTargetKind::Mrt &&
		    exp.index == 0 && !uint_output &&
		    state.input_info.pixel->alpha_blend_source != ShaderAlphaBlendSource::None) {
			// Broadcast logical alpha before swizzling the primary output.
			const auto blend_output =
			    OutputVariableForExport(state, {.kind = IR::ExportTargetKind::Mrt, .index = 1});
			if (blend_output != 0) {
				auto alpha = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpVectorShuffle, vector_type, alpha, value, value,
				                          3u, 3u, 3u, 3u);
				uint32_t alpha_factor = 0;
				switch (state.input_info.pixel->alpha_blend_source) {
					case ShaderAlphaBlendSource::SourceAlphaOne:
						alpha_factor = ConstantF32Value(state, 1.0f);
						break;
					case ShaderAlphaBlendSource::SourceAlphaZero:
						alpha_factor = ConstantF32Value(state, 0.0f);
						break;
					default: break;
				}
				if (alpha_factor != 0) {
					const auto mapping = state.input_info.pixel->target_export_mapping[0];
					for (uint32_t component = 0; component < 4; component++) {
						if (mapping.Map(component) != 3u) continue;
						const auto factors = state.builder.AllocateId();
						state.builder.AddFunction(spv::OpCompositeInsert, vector_type, factors,
						                          alpha_factor, alpha, component);
						alpha = factors;
						break;
					}
				}
				state.builder.AddFunction(spv::OpStore, blend_output, alpha);
			}
		}
		if (state.program.stage == ShaderType::Pixel && exp.kind == IR::ExportTargetKind::Mrt &&
		    exp.index < state.input_info.pixel->target_export_mapping.size()) {
			const auto mapping = state.input_info.pixel->target_export_mapping[exp.index];
			if (!mapping.IsIdentity()) {
				const auto mapped = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpVectorShuffle, vector_type, mapped, value, value,
				                          mapping.Map(0), mapping.Map(1), mapping.Map(2),
				                          mapping.Map(3));
				value = mapped;
			}
		}
		const auto trace_position = [&](IR::VertexTraceKind kind) {
			if (!state.program.bindings.vertex_value_trace ||
			    exp.kind != IR::ExportTargetKind::Position || exp.index != 0) return;
			std::array<uint32_t, 12> payload;
			payload.fill(ConstantU32(state, 0));
			for (uint32_t i = 0; i < 4; ++i) {
				const auto component = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state), component, value, i);
				payload[i] = Unary(state, spv::OpBitcast, TypeU32(state), component);
			}
			auto trace_predicate = exec;
			if (IR::VertexValueTrace().vertex == UINT32_MAX) {
				// Capture the actual vertex that exports a NaN/Inf Position, not an arbitrary
				// vertex selected before the GPU-generated index buffer is finalized.
				const auto booleans = TypeBoolVector(state, 4);
				const auto non_finite = Unary(
				    state, spv::OpAny, TypeBool(state),
				    Binary(state, spv::OpLogicalOr, booleans, Unary(state, spv::OpIsNan, booleans, value),
				           Unary(state, spv::OpIsInf, booleans, value)));
				trace_predicate = Binary(state, spv::OpLogicalAnd, TypeBool(state), exec, non_finite);
			}
			EmitVertexValueTrace(ctx, static_cast<uint32_t>(kind), inst.Flags<IR::ExportFlags>().pc,
			                     trace_predicate, 1, UINT32_MAX, UINT32_MAX, payload);
		};
		trace_position(IR::VertexTraceKind::PreClip);
		if (exp.kind == IR::ExportTargetKind::Position &&
		    state.input_info.vertex->clip_space.enabled) {
			value = ConvertPositionToClipSpace(state, value);
		}
		trace_position(IR::VertexTraceKind::PostClip);
		if (state.program.stage == ShaderType::Mesh) {
			const auto kind = exp.kind == IR::ExportTargetKind::Position
			                      ? IR::StageOutputKind::Position
			                      : IR::StageOutputKind::Parameter;
			if (kind == IR::StageOutputKind::Position && exp.index == 0) {
				EmitVertexExplodeCheck(state, value);
			}
			state.builder.AddFunction(spv::OpStore, MeshOutputPointer(state, kind, exp.index),
			                          value);
		} else if (exp.kind == IR::ExportTargetKind::Position) {
			EmitVertexExplodeCheck(state, value);
			if (state.invalid_position_clip_distance != UINT32_MAX) {
				const auto zero = state.builder.Constant(spv::OpConstantNull, TypeF32Vector(state, 4));
				const auto equal = state.builder.AllocateId();
				const auto zero_position = state.builder.AllocateId();
				const auto is_nan = state.builder.AllocateId();
				const auto is_inf = state.builder.AllocateId();
				const auto non_finite_components = state.builder.AllocateId();
				const auto non_finite = state.builder.AllocateId();
				const auto invalid = state.builder.AllocateId();
				const auto distance = state.builder.AllocateId();
				const auto distance_pointer = state.builder.AllocateId();
				state.builder.AddFunction(spv::OpFOrdEqual, TypeBoolVector(state, 4), equal,
				                          value, zero);
				state.builder.AddFunction(spv::OpAll, TypeBool(state), zero_position, equal);
				state.builder.AddFunction(spv::OpIsNan, TypeBoolVector(state, 4), is_nan, value);
				state.builder.AddFunction(spv::OpIsInf, TypeBoolVector(state, 4), is_inf, value);
				state.builder.AddFunction(spv::OpLogicalOr, TypeBoolVector(state, 4),
				                          non_finite_components, is_nan, is_inf);
				state.builder.AddFunction(spv::OpAny, TypeBool(state), non_finite,
				                          non_finite_components);
				state.builder.AddFunction(spv::OpLogicalOr, TypeBool(state), invalid,
				                          zero_position, non_finite);
				// Zero and non-finite positions are invalid vertices. A negative clip distance
				// makes a primitive containing one collapse to its remaining edge, avoiding
				// undefined NaN/Inf clipping and the 0/0 perspective divide.
				state.builder.AddFunction(spv::OpSelect, TypeF32(state), distance, invalid,
				                          ConstantF32Value(state, -1.0f),
				                          ConstantF32Value(state, 0.0f));
				state.builder.AddFunction(
				    spv::OpAccessChain, TypePointer(state, spv::StorageClassOutput, TypeF32(state)),
				    distance_pointer, state.clip_distance_variable,
				    ConstantU32(state, state.invalid_position_clip_distance));
				state.builder.AddFunction(spv::OpStore, distance_pointer, distance);
				static std::atomic_bool logged = false;
				if (!logged.exchange(true, std::memory_order_relaxed)) {
					Log::WriteToConsoleAndLog(
					    "Shader: emitted zero-position clip guard\n");
				}
			}
			const auto pointer = state.builder.AllocateId();
			state.builder.AddFunction(
			    spv::OpAccessChain,
			    TypePointer(state, spv::StorageClassOutput, TypeF32Vector(state, 4)), pointer,
			    variable, ConstantU32(state, 0));
			state.builder.AddFunction(spv::OpStore, pointer, value);
		} else {
			state.builder.AddFunction(spv::OpStore, variable, value);
		}
	});
}

uint32_t EmitIdentity(ValueEmitContext&, uint32_t value) {
	return value;
}

void EmitVoid(ValueEmitContext&) {}

void EmitStoreCompletion(EmitterState& state) {
	state.builder.AddFunction(
	    spv::OpControlBarrier, ConstantU32(state, spv::ScopeSubgroup),
	    ConstantU32(state, spv::ScopeDevice),
	    ConstantU32(state, spv::MemorySemanticsReleaseMask | spv::MemorySemanticsUniformMemoryMask |
	                          spv::MemorySemanticsImageMemoryMask));
}

void EmitBarrier(EmitterState& state) {
	const auto tessellation = state.program.stage == ShaderType::TessellationControl;
	if (!tessellation && ShaderWorkgroupInput(state.program.stage, state.input_info) == nullptr) {
		// Independent graphics invocations have no native workgroup left to synchronize.
		return;
	}
	const auto memory_scope = tessellation ? spv::ScopeInvocation : spv::ScopeWorkgroup;
	const auto memory = state.lds_storage_class == spv::StorageClassStorageBuffer
	                        ? spv::MemorySemanticsUniformMemoryMask
	                        : spv::MemorySemanticsWorkgroupMemoryMask;
	const auto semantics = tessellation ? spv::MemorySemanticsMaskNone
	                                    : spv::MemorySemanticsAcquireReleaseMask | memory;
	state.builder.AddFunction(spv::OpControlBarrier, ConstantU32(state, spv::ScopeWorkgroup),
	                          ConstantU32(state, memory_scope), ConstantU32(state, semantics));
}

uint32_t EmitLaneId(EmitterState& state) {
	return state.program.stage == ShaderType::TessellationControl
	           ? EmitBuiltinU32(state, IR::StageInputKind::InvocationId, 0)
	           : EmitSubgroupLocalInvocationId(state);
}

uint32_t EmitIsHelperInvocation(EmitterState& state) {
	if (state.helper_invocation_variable == 0) {
		return ConstantBool(state, false);
	}
	const auto helper = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpLoad, TypeBool(state), helper,
	                          state.helper_invocation_variable);
	return helper;
}

uint32_t EmitMeshDrawParameter(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto&      state  = ctx.state;
	const auto result = state.builder.AllocateId();
	const auto index  = inst.Arg(0).U32();
	if (state.program.stage != ShaderType::Mesh || index >= IR::PushData::MeshDrawDwordCount) {
		ctx.Fail(inst, "invalid mesh draw parameter");
	}
	// Push-constant dwords 0 and 1 hold the device address of the draw's parameter record, so
	// that an indirect draw can have the GPU write the record from its arguments.
	const auto push_word = [&](uint32_t word) {
		const auto pointer = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpAccessChain, TypePushConstantElementPointer(state),
		                          pointer, state.push_constant_variable, ConstantU32(state, 0),
		                          ConstantU32(state, word));
		const auto value = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLoad, TypeU32(state), value, pointer);
		return value;
	};
	const auto u64      = TypeScalarU64(state);
	const auto constant = [&](uint64_t value) {
		return state.builder.Constant(spv::OpConstant, u64, static_cast<uint32_t>(value),
		                              static_cast<uint32_t>(value >> 32u));
	};
	const auto low  = Unary(state, spv::OpUConvert, u64, push_word(0));
	const auto high = Binary(state, spv::OpShiftLeftLogical, u64,
	                         Unary(state, spv::OpUConvert, u64, push_word(1)), constant(32));
	const auto address =
	    Binary(state, spv::OpIAdd, u64, Binary(state, spv::OpBitwiseOr, u64, low, high),
	           constant(uint64_t {index} * sizeof(uint32_t)));
	const auto pointer = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpConvertUToPtr, TypePhysicalU32Pointer(state), pointer,
	                          address);
	state.builder.AddFunction(spv::OpLoad, TypeU32(state), result, pointer,
	                          spv::MemoryAccessAlignedMask, static_cast<uint32_t>(sizeof(uint32_t)));
	return result;
}

uint32_t EmitGetUserData(EmitterState& state, IR::ScalarReg reg) {

	uint32_t dword = 0;
	if (!UserDataDwordIndex(state, reg, dword)) {
		return ConstantU32(state, 0);
	} else {
		return EmitShaderDataDwordLoad(state, dword);
	}
}

uint32_t EmitGetBuiltin(ValueEmitContext& ctx, IR::Value kind, IR::Value index) {
	return EmitBuiltinU32(ctx.state, static_cast<IR::StageInputKind>(kind.U32()), index.U32());
}

uint32_t EmitGetDispatchThreadExtent(ValueEmitContext& ctx, const IR::Inst& inst) {
	const auto start = ctx.state.program.bindings.dispatch_thread_dword;
	if (start == IR::PushData::NoStart || !inst.Arg(0).IsImmediate() || inst.Arg(0).U32() >= 3u) {
		ctx.Fail(inst, "invalid dispatch thread extent");
	}
	return EmitShaderDataDwordLoad(ctx.state, start + inst.Arg(0).U32());
}

uint32_t EmitUndefU1(EmitterState& state, const IR::Inst& inst) {
	const auto result = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpUndef, TypeId(state, inst.GetType()), result);
	return result;
}

uint32_t EmitDppMoveU32(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto&      state    = ctx.state;
	const auto flags    = inst.Flags<IR::DppMoveFlags>();
	const auto target   = EmitDppTargetLane(state, flags);
	const auto shuffled = ctx.Shuffle(inst, 0, target.lane);
	if (flags.fetch_inactive) {
		return shuffled;
	}
	const auto ballot        = ctx.Ballot(inst.Arg(1));
	auto       source_active = EmitBallotLaneActiveBool(state, ballot, target.lane);
	if (state.helper_invocation_variable != 0) {
		// Ballots leave helper invocations out (they are not in the guest's initial EXEC), but
		// after S_WQM they are active lanes whose values a DPP read gets, as on the GPU (the min
		// scan of a material loop otherwise reads 0 from a helper and never ends). A helper source
		// counts by its own EXEC bit.
		const auto helper = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLoad, TypeBool(state), helper,
		                          state.helper_invocation_variable);
		const auto helpers = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformBallot, TypeU32Vector(state, 4), helpers,
		                          ConstantU32(state, spv::ScopeSubgroup), helper);
		const auto helper_source = EmitBallotLaneActiveBool(state, helpers, target.lane);
		const auto helper_active = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(state), helper_active, helper_source,
		                          ctx.Shuffle(inst, 1, target.lane));
		const auto either = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpLogicalOr, TypeBool(state), either, source_active,
		                          helper_active);
		source_active = either;
	}
	const auto can_fetch     = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(state), can_fetch, target.valid,
	                          source_active);
	return EmitNative<spv::OpSelect, IR::Type::U32>(ctx.state, can_fetch, shuffled,
	                                                ConstantU32(state, 0));
}

uint32_t EmitDppUpdateU32(ValueEmitContext& ctx, const IR::Inst& inst) {
	const auto flags = inst.Flags<IR::DppMoveFlags>();
	const auto write = EmitDppWriteCondition(ctx, flags, ctx.Arg(inst, 2));
	return EmitNative<spv::OpSelect, IR::Type::U32>(ctx.state, write, ctx.Arg(inst, 0),
	                                                ctx.Arg(inst, 1));
}

uint32_t EmitConditionRef(ValueEmitContext& ctx, const IR::Inst& inst) {
	if (ShaderRecompiler::WaveBranchFull()) {
		// Upstream a8beac19: scalar branches include lanes disabled by EXEC, which the branch body
		// may restore.
		if (ctx.other_half != nullptr && ctx.half != 0) {
			return ctx.other_half->Def(IR::Value(const_cast<IR::Inst*>(&inst)));
		}
		const auto kind = inst.Flags<CFG::BranchCondition>();
		if (kind == CFG::BranchCondition::ScalarInstruction) return ctx.Arg(inst, 0);
		const bool zero = kind == CFG::BranchCondition::ExecZero ||
		                  kind == CFG::BranchCondition::VccZero ||
		                  kind == CFG::BranchCondition::SccZero;
		const auto ballot    = ctx.Ballot(inst.Arg(0));
		const auto reference = zero ? ctx.Ballot(IR::Value(true)) : ConstantU32CompositeZero(ctx.state, 4);
		const auto compare   = Binary(ctx.state, zero ? spv::OpIEqual : spv::OpINotEqual,
		                              TypeBoolVector(ctx.state, 4), ballot, reference);
		return Unary(ctx.state, zero ? spv::OpAll : spv::OpAny, TypeBool(ctx.state), compare);
	}
	// Argument 1 is the branch's emitted condition; argument 0 is only for analysis.
	if (ctx.other_half == nullptr) return ctx.Arg(inst, 1);
	// A native scalar branch makes one decision for both emulated wave halves.
	// &inst is a const Inst*; without the cast it would pick the explicit Value(bool) constructor
	// (pointer-to-bool), turning the condition into a constant true for the second half.
	if (ctx.half != 0) return ctx.other_half->Def(IR::Value(const_cast<IR::Inst*>(&inst)));
	const auto kind = inst.Flags<CFG::BranchCondition>();
	if (kind == CFG::BranchCondition::ScalarInstruction) return ctx.Arg(inst, 1);
	const auto ballot = ctx.Ballot(inst.Arg(1));
	const auto low = ctx.state.builder.AllocateId();
	const auto high = ctx.state.builder.AllocateId();
	const auto combined = ctx.state.builder.AllocateId();
	const auto result = ctx.state.builder.AllocateId();
	ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), low, ballot, 0);
	ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), high, ballot, 1);
	const bool zero = kind == CFG::BranchCondition::ExecZero ||
	                  kind == CFG::BranchCondition::VccZero || kind == CFG::BranchCondition::SccZero;
	if (!zero) {
		ctx.state.builder.AddFunction(spv::OpBitwiseOr, TypeU32(ctx.state), combined, low, high);
		ctx.state.builder.AddFunction(spv::OpINotEqual, TypeBool(ctx.state), result, combined,
		                              ConstantU32(ctx.state, 0u));
		return result;
	}
	// "Zero" holds when every active lane of both halves says so. Invocations that are not running
	// (a partial wave, lanes that left the shader) have no ballot bit; comparing with all ones
	// instead of the active mask kept such waves in their loop forever (Ghost of Yotei cutscene
	// compute b5d1ae71f2600147, a merge-path search, hung the GPU).
	const auto active      = ctx.Ballot(IR::Value(true));
	const auto active_word = ctx.state.builder.AllocateId();
	const auto low_all     = ctx.state.builder.AllocateId();
	const auto high_all    = ctx.state.builder.AllocateId();
	ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), active_word, active, 0);
	ctx.state.builder.AddFunction(spv::OpIEqual, TypeBool(ctx.state), low_all, low, active_word);
	ctx.state.builder.AddFunction(spv::OpIEqual, TypeBool(ctx.state), high_all, high, active_word);
	ctx.state.builder.AddFunction(spv::OpLogicalAnd, TypeBool(ctx.state), combined, low_all, high_all);
	return combined;
}

uint32_t EmitBallot(ValueEmitContext& ctx, IR::Value predicate) {
	return ctx.Ballot(predicate);
}

uint32_t EmitReadFirstLane(ValueEmitContext& ctx, const IR::Inst& inst) {
	const auto ballot   = ctx.Ballot(inst.Arg(1));
	const auto lane     = ctx.FirstLane(ballot);
	const auto shuffled = ctx.Shuffle(inst, 0, lane);
	if (WaveHalvesInHostSubgroup(ctx.state)) {
		// Each half is its own guest wave: the value is uniform per half only.
		return shuffled;
	}
	// Every lane now holds the same value, so reading the first active lane changes nothing but
	// tells the driver the result is uniform. AMD compiles the shuffle alone to ds_bpermute and
	// treats its result as per-lane, which turned everything derived from a waterfall key
	// (scalar loads, address math, loop exits) into vector code.
	auto&      state  = ctx.state;
	const auto result = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpGroupNonUniformBroadcastFirst,
	                          TypeId(state, inst.Arg(0).GetType()), result,
	                          ConstantU32(state, spv::ScopeSubgroup), shuffled);
	return result;
}

uint32_t EmitReadLane(ValueEmitContext& ctx, const IR::Inst& inst) {
	// V_READLANE's lane is an SGPR or a constant, so the result is uniform too; see
	// EmitReadFirstLane.
	auto& state = ctx.state;
	if (WaveHalvesInHostSubgroup(state)) {
		// The lane is the guest wave's (wave32 reads its low 5 bits): offset it into the
		// invocation's own half.
		const auto lane =
		    EmitBinaryU32(state, spv::OpBitwiseAnd, ctx.Arg(inst, 1), ConstantU32(state, 31));
		return ctx.Shuffle(inst, 0,
		                   EmitAddU32(state, EmitLaunchedLaneAtOrBelow(state, lane),
		                              EmitOwnWaveHalfBase(state)));
	}
	const auto shuffled = ctx.Shuffle(inst, 0, EmitLaunchedLaneAtOrBelow(state, ctx.Arg(inst, 1)));
	const auto result   = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpGroupNonUniformBroadcastFirst,
	                          TypeId(state, inst.Arg(0).GetType()), result,
	                          ConstantU32(state, spv::ScopeSubgroup), shuffled);
	return result;
}

uint32_t EmitWriteLane(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto&      state = ctx.state;
	const auto hit   = state.builder.AllocateId();
	const auto lane  = WaveHalvesInHostSubgroup(state)
	                       ? EmitBinaryU32(state, spv::OpBitwiseAnd,
	                                       EmitSubgroupLocalInvocationId(state),
	                                       ConstantU32(state, 31))
	                       : EmitSubgroupLocalInvocationId(state);
	state.builder.AddFunction(spv::OpIEqual, TypeBool(state), hit, lane, ctx.Arg(inst, 2));
	return EmitNative<spv::OpSelect, IR::Type::U32>(ctx.state, hit, ctx.Arg(inst, 1),
	                                                ctx.Arg(inst, 0));
}

uint32_t EmitPermlane16U32(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto&      state     = ctx.state;
	const auto flags     = inst.Flags<IR::PermlaneFlags>();
	const auto subid     = EmitSubgroupLocalInvocationId(state);
	const auto row       = state.builder.AllocateId();
	const auto row_value = state.builder.AllocateId();
	const auto lane      = state.builder.AllocateId();
	const auto lane8     = state.builder.AllocateId();
	const auto shift     = state.builder.AllocateId();
	const auto upper     = state.builder.AllocateId();
	const auto selected  = state.builder.AllocateId();
	const auto shifted   = state.builder.AllocateId();
	const auto index     = state.builder.AllocateId();
	const auto target    = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), row, subid,
	                          ConstantU32(state, 0xfffffff0u));
	if (flags.x16) {
		state.builder.AddFunction(spv::OpBitwiseXor, TypeU32(state), row_value, row,
		                          ConstantU32(state, 16));
	} else {
		state.builder.AddFunction(spv::OpCopyObject, TypeU32(state), row_value, row);
	}
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), lane, subid,
	                          ConstantU32(state, 15));
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), lane8, lane,
	                          ConstantU32(state, 7));
	state.builder.AddFunction(spv::OpShiftLeftLogical, TypeU32(state), shift, lane8,
	                          ConstantU32(state, 2));
	state.builder.AddFunction(spv::OpUGreaterThanEqual, TypeBool(state), upper, lane,
	                          ConstantU32(state, 8));
	state.builder.AddFunction(spv::OpSelect, TypeU32(state), selected, upper, ctx.Arg(inst, 2),
	                          ctx.Arg(inst, 1));
	state.builder.AddFunction(spv::OpShiftRightLogical, TypeU32(state), shifted, selected, shift);
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), index, shifted,
	                          ConstantU32(state, 15));
	state.builder.AddFunction(spv::OpBitwiseOr, TypeU32(state), target, row_value, index);
	const auto launched = EmitLaunchedLaneAtOrBelow(state, target);
	const auto shuffled = ctx.Shuffle(inst, 0, launched);
	uint32_t   result   = shuffled;
	if (!flags.fetch_inactive) {
		// A lane the host did not launch is active if the guest switched it on, which only the
		// scalar EXEC copy records.
		const auto in_high = state.builder.AllocateId();
		state.builder.AddFunction(
		    spv::OpLogicalAnd, TypeBool(state), in_high,
		    ConstantBool(state, state.program.wave_size == 64u),
		    Binary(state, spv::OpINotEqual, TypeBool(state),
		                   EmitBinaryU32(state, spv::OpBitwiseAnd, target, ConstantU32(state, 32)),
		                   ConstantU32(state, 0)));
		const auto exec_word = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpSelect, TypeU32(state), exec_word, in_high,
		                          ctx.Arg(inst, 5), ctx.Arg(inst, 4));
		const auto exec_bit = EmitBinaryU32(
		    state, spv::OpBitwiseAnd,
		    EmitBinaryU32(state, spv::OpShiftRightLogical, exec_word,
		                  EmitBinaryU32(state, spv::OpBitwiseAnd, target, ConstantU32(state, 31))),
		    ConstantU32(state, 1));
		const auto was_launched = state.builder.AllocateId();
		const auto source_exec  = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpIEqual, TypeBool(state), was_launched, launched, target);
		state.builder.AddFunction(
		    spv::OpSelect, TypeBool(state), source_exec, was_launched, ctx.Shuffle(inst, 3, launched),
		    Binary(state, spv::OpINotEqual, TypeBool(state), exec_bit, ConstantU32(state, 0)));
		result = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpSelect, TypeU32(state), result, source_exec, shuffled,
		                          ConstantU32(state, 0));
	}
	return result;
}

uint32_t EmitGetAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	return EmitAttribute(ctx.state, inst.Arg(0).U32(), inst.Arg(1).U32());
}

uint32_t EmitGetInterpolationParameter(ValueEmitContext& ctx, const IR::Inst& inst) {
	return EmitInterpolationParameter(ctx, inst.Arg(0).U32(), inst.Arg(1).U32(), inst.Arg(2).U32());
}

uint32_t EmitGetShaderBase(ValueEmitContext& ctx) {
	// Guest S_GETPC values stay shader-relative in SPIR-V, matching the runtime ABI. The
	// runtime descriptor evaluator supplies the mapped shader base for host-side planning.
	return ctx.Def(IR::Value(uint64_t {0}));
}

void EmitUnreachable(ValueEmitContext& ctx, const IR::Inst& inst) {
	ctx.Fail(inst, "must be lowered before SPIR-V emission");
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
