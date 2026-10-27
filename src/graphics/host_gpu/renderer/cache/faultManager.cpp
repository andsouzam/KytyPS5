#include "graphics/host_gpu/renderer/cache/faultManager.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/switches.h"
#include "gpu_tiler_shaders/fault_buffer_process_spv.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/drainStats.h"
#include "graphics/host_gpu/renderer/gpuZones.h"
#include "graphics/host_gpu/renderer/vertexExplode.h"
#include "graphics/shader/recompiler/ir/VertexExplodeLog.h"
#include "graphics/shader/recompiler/ir/VertexValueTrace.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace Libs::Graphics {

namespace {

constexpr size_t MaxPageFaults    = 1024;
constexpr size_t PageFaultAreaSize = MaxPageFaults * sizeof(uint64_t);

namespace IR = ShaderRecompiler::IR;
constexpr uint64_t FaultBitmapSize  = BufferCache::CACHING_NUMPAGES / 8;
static_assert(FaultBitmapSize == uint64_t {IR::ExplodeLogBaseWord} * sizeof(uint32_t),
              "the explode log region starts right after the fault bitmap");
constexpr uint64_t ExplodeLogOffset = FaultBitmapSize;
constexpr uint64_t ExplodeLogSize   = uint64_t {IR::ExplodeLogWords} * sizeof(uint32_t);
// Header and counters: cleared after each copy (the records are only read where a counter is set).
constexpr uint64_t ExplodeLogClearSize = uint64_t {IR::ExplodeLogRecordWord} * sizeof(uint32_t);

uint64_t DiagnosticLogSize() {
	return ExplodeLogSize + (IR::VertexValueTrace().enabled ? uint64_t {IR::VertexTraceWords} * 4u : 0u);
}

} // namespace

FaultManager::FaultManager(GraphicContext& graphics, CommandScheduler& scheduler,
                           BufferCache& buffer_cache)
    : m_graphics(graphics), m_scheduler(scheduler), m_buffer_cache(buffer_cache),
      m_fault_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 0, AllFlags,
                     FaultBitmapSize + (VertexExplode::Enabled() ? DiagnosticLogSize() : 0)),
      m_download_buffer(graphics, scheduler, MemoryUsage::Download, 0, AllFlags,
                        MaxPendingFaults * PageFaultAreaSize) {
	SetVulkanObjectNameF(m_graphics.device, m_fault_buffer.Handle(), "Fault Buffer");
	if (VertexExplode::Enabled()) {
		m_explode_download = std::make_unique<Buffer>(graphics, scheduler, MemoryUsage::Download, 0,
		                                              AllFlags, MaxPendingFaults * DiagnosticLogSize());
		std::printf("VtxExplode: KYTY_VTX_EXPLODE_CHECK active: vertex and mesh shaders check their "
		            "positions (nan/inf, |w| < 1e-6, w > 0 and |x/w| or |y/w| > %g), CPU check of index "
		            "ranges, %u log bytes after the fault bitmap\n",
		            static_cast<double>(IR::VertexExplodeCheck().limit),
		            static_cast<uint32_t>(ExplodeLogSize));
		std::fflush(stdout);
	}

	const vk::DescriptorSetLayoutBinding bindings[] {
	    {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	    {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	};
	vk::DescriptorSetLayoutCreateInfo layout_info {};
	layout_info.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	layout_info.bindingCount = std::size(bindings);
	layout_info.pBindings    = bindings;
	RequireVulkanSuccess(
	    m_graphics.device.createDescriptorSetLayout(&layout_info, nullptr,
	                                                &m_fault_process_desc_layout),
	    "create fault-buffer descriptor layout");

	const auto module = CompileSPV(FAULT_BUFFER_PROCESS_SPV, m_graphics.device);

	vk::PipelineLayoutCreateInfo pipeline_layout_info {};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts    = &m_fault_process_desc_layout;
	RequireVulkanSuccess(
	    m_graphics.device.createPipelineLayout(&pipeline_layout_info, nullptr,
	                                           &m_fault_process_pipeline_layout),
	    "create fault-buffer pipeline layout");

	vk::PipelineShaderStageCreateInfo stage {};
	stage.stage  = vk::ShaderStageFlagBits::eCompute;
	stage.module = module;
	stage.pName  = "main";
	vk::ComputePipelineCreateInfo pipeline_info {};
	pipeline_info.stage  = stage;
	pipeline_info.layout = m_fault_process_pipeline_layout;
	const auto result = m_graphics.device.createComputePipelines(
	    nullptr, 1, &pipeline_info, nullptr, &m_fault_process_pipeline);
	m_graphics.device.destroyShaderModule(module, nullptr);
	RequireVulkanSuccess(result, "create fault-buffer pipeline");
	SetVulkanObjectNameF(m_graphics.device, m_fault_process_pipeline, "Fault Buffer Parser");
}

FaultManager::~FaultManager() {
	m_graphics.device.destroyPipeline(m_fault_process_pipeline, nullptr);
	m_graphics.device.destroyPipelineLayout(m_fault_process_pipeline_layout, nullptr);
	m_graphics.device.destroyDescriptorSetLayout(m_fault_process_desc_layout, nullptr);
}

Buffer* FaultManager::GetFaultBuffer() noexcept {
	if (!m_bitmap_initialized) {
		EXIT_IF(!m_scheduler.Active() || m_scheduler.Current().IsInvalid());
		// Clear only the bitmap; diagnostics after it have their own initialization.
		// Fill records the transfer and makes it visible to subsequent shader readers/writers.
		m_fault_buffer.Fill(0, FaultBitmapSize, 0);
		m_bitmap_initialized = true;
	}
	return &m_fault_buffer;
}

void FaultManager::ProcessFaultBuffer() {
	if (!m_bitmap_initialized) {
		// No GPU binding has exposed this allocation, so no current producer exists.
		return;
	}
	if (const auto wait_tick = m_fault_areas[m_current_area]; wait_tick != 0) {
		DrainStats::ReasonScope reason(DrainStats::Reason::FaultBuffer);
		m_scheduler.Wait(wait_tick);
		m_scheduler.PopPendingOperations();
	}

	const auto offset = m_current_area * PageFaultAreaSize;
	auto*      mapped = m_download_buffer.Mapped().data() + offset;
	std::memset(mapped, 0, PageFaultAreaSize);
	m_download_buffer.Flush(offset, PageFaultAreaSize);

	vk::BufferMemoryBarrier2 pre_barrier {};
	pre_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	pre_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
	pre_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
	pre_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
	pre_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.buffer        = m_fault_buffer.Handle();
	pre_barrier.offset        = 0;
	pre_barrier.size           = m_fault_buffer.Size();
	auto post_barrier         = pre_barrier;
	post_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
	post_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
	post_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	post_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

	const vk::DescriptorBufferInfo infos[] {
	    {m_fault_buffer.Handle(), 0, m_fault_buffer.Size()},
	    {m_download_buffer.Handle(), offset, PageFaultAreaSize},
	};
	std::array<vk::WriteDescriptorSet, 2> writes {};
	for (uint32_t index = 0; index < writes.size(); ++index) {
		writes[index].dstBinding      = index;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType  = vk::DescriptorType::eStorageBuffer;
		writes[index].pBufferInfo     = &infos[index];
	}

	m_scheduler.EndRendering();
	auto command = m_scheduler.Current().Handle();
	GpuZones::Mark(command, DrainStats::Zone::FaultBuffer);
	vk::DependencyInfo dependency {};
	dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &pre_barrier;
	command.pipelineBarrier2(dependency);
	command.bindPipeline(vk::PipelineBindPoint::eCompute, m_fault_process_pipeline);
	command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,
	                             m_fault_process_pipeline_layout, 0, writes);
	const auto num_threads    = BufferCache::CACHING_NUMPAGES / 32;
	const auto num_workgroups = (num_threads + 63) / 64;
	command.dispatch(static_cast<uint32_t>(num_workgroups), 1, 1);
	// The deferred host reader waits for this submission, then invalidates this slice.
	// Make the parser's count and records available to that host read before signaling it.
	auto output_barrier                = post_barrier;
	output_barrier.dstStageMask         = vk::PipelineStageFlagBits2::eHost;
	output_barrier.dstAccessMask        = vk::AccessFlagBits2::eHostRead;
	output_barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
	output_barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
	output_barrier.buffer              = m_download_buffer.Handle();
	output_barrier.offset              = offset;
	output_barrier.size                = PageFaultAreaSize;
	const std::array post_barriers {post_barrier, output_barrier};
	dependency.bufferMemoryBarrierCount = post_barriers.size();
	dependency.pBufferMemoryBarriers    = post_barriers.data();
	command.pipelineBarrier2(dependency);

	const auto area = m_current_area;
	m_scheduler.DeferOperation([this, mapped, offset, area] {
		m_download_buffer.Invalidate(offset, PageFaultAreaSize);
		RangeSet    fault_ranges;
		const auto* faults = std::bit_cast<const uint64_t*>(mapped);
		const auto  count  = static_cast<uint32_t>(faults[0]);
		if (count >= MaxPageFaults) {
			EXIT("FaultManager: fault count %u exceeds record capacity %zu\n", count, MaxPageFaults - 1);
		}
		for (uint32_t index = 1; index <= count; ++index) {
			const auto address = BufferCache::GuestAddress(faults[index]);
			fault_ranges.Add(address, BufferCache::CACHING_PAGESIZE);
			LOGF("Accessed non-GPU cached memory at 0x%016" PRIx64 "\n", address);
			// KYTY_DEBUG_BDA_FAULTS=1 prints the pages shaders found missing in the BDA page table.
			static const bool print   = KYTY_GETENV("KYTY_DEBUG_BDA_FAULTS") != nullptr;
			static uint32_t   printed = 0;
			if (print && (printed < 200u || printed % 500u == 0u)) {
				std::printf("bda-fault: #%u 0x%016" PRIx64 " (%u this pass)\n", printed, address, count);
				std::fflush(stdout);
			}
			printed += print ? 1u : 0u;
		}
		fault_ranges.ForEach([this](uint64_t start, uint64_t end) {
			EXIT_IF(end - start > std::numeric_limits<uint32_t>::max());
			(void)m_buffer_cache.FindBuffer(start, end - start);
		});
		m_fault_areas[area] = 0;
	});

	m_fault_areas[m_current_area++] = m_scheduler.CurrentTick();
	m_current_area %= MaxPendingFaults;
}

void FaultManager::ProcessExplodeLog() {
	if (m_explode_download == nullptr) {
		return;
	}
	const auto frame = m_graphics.presented_frames.load(std::memory_order_relaxed);
	if (frame == m_explode_frame) {
		return;
	}
	if (const auto wait_tick = m_explode_areas[m_explode_area]; wait_tick != 0) {
		// The copy of MaxPendingFaults frames ago is still in flight; try at the next frame.
		return;
	}
	m_explode_frame = frame;
	m_scheduler.EndRendering();
	auto command = m_scheduler.Current().Handle();

	vk::BufferMemoryBarrier2 pre_barrier {};
	pre_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	pre_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
	pre_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eTransfer;
	pre_barrier.dstAccessMask =
	    vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
	pre_barrier.buffer = m_fault_buffer.Handle();
	pre_barrier.offset = ExplodeLogOffset;
	pre_barrier.size   = DiagnosticLogSize();
	auto post_barrier          = pre_barrier;
	post_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
	post_barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
	post_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	post_barrier.dstAccessMask =
	    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
	vk::DependencyInfo dependency {};
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &pre_barrier;
	command.pipelineBarrier2(dependency);
	if (!m_explode_cleared) {
		// Device-local memory starts undefined: clear the whole region once.
		m_explode_cleared = true;
		// Clear both the legacy explode log and the optional value-trace slots. The trace
		// region uses atomic zero-to-one slot claims; leaving device-local bytes undefined can
		// make every claim fail and silently produce empty trace records.
		command.fillBuffer(m_fault_buffer.Handle(), ExplodeLogOffset, DiagnosticLogSize(), 0);
		dependency.pBufferMemoryBarriers = &post_barrier;
		command.pipelineBarrier2(dependency);
		return;
	}
	const auto       offset = m_explode_area * DiagnosticLogSize();
	const vk::BufferCopy copy {ExplodeLogOffset, offset, DiagnosticLogSize()};
	command.copyBuffer(m_fault_buffer.Handle(), m_explode_download->Handle(), 1, &copy);
	command.fillBuffer(m_fault_buffer.Handle(), ExplodeLogOffset, ExplodeLogClearSize, 0);
	dependency.pBufferMemoryBarriers = &post_barrier;
	command.pipelineBarrier2(dependency);

	const auto area   = m_explode_area;
	auto*      mapped = m_explode_download->Mapped().data() + offset;
	m_scheduler.DeferOperation([this, mapped, offset, area, frame] {
		m_explode_download->Invalidate(offset, DiagnosticLogSize());
		VertexExplode::Report(std::bit_cast<const uint32_t*>(mapped), frame);
		if (IR::VertexValueTrace().enabled)
			VertexExplode::ReportValueTrace(std::bit_cast<const uint32_t*>(mapped) + IR::ExplodeLogWords, frame);
		m_explode_areas[area] = 0;
	});
	m_explode_areas[m_explode_area++] = m_scheduler.CurrentTick();
	m_explode_area %= MaxPendingFaults;
}

} // namespace Libs::Graphics
