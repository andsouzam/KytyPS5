#include "graphics/host_gpu/renderer/functionProfile.h"
#include "graphics/host_gpu/renderer/labelCausalTrace.h"
#include "graphics/host_gpu/renderer/waitPollCausalDiagnostic.h"

#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/cache/bonePaletteStagingPolicy.h"

#include "common/alignment.h"
#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/switches.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/readbackQueue.h"
#include "graphics/host_gpu/renderer/cache/textureCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/drainStats.h"
#include "graphics/host_gpu/renderer/gpuZones.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/vulkanCommon.h"
#include "kernel/memory.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <immintrin.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Libs::Graphics {

namespace {

constexpr uint64_t MiB           = 1024 * 1024;
constexpr uint64_t GdsBufferSize = 64 * 1024;
// Buffers registered in extended memory (1 TiB page-table hole diagnostic, see ChangeRegister).
std::atomic<uint64_t> g_extended_registers {0};

// copyBuffer is not bounds-checked by the device: a region past either VkBuffer reads or writes
// past its allocation. Such regions are shortened to end inside both (dropped when nothing is
// left) and logged. A size of 0 for `source_size` skips the source check (unknown buffer).
void ClampBufferCopies(std::vector<vk::BufferCopy>& copies, uint64_t source_size,
                       uint64_t destination_size, const char* operation, uint64_t guest) {
	bool dropped = false;
	for (auto& copy: copies) {
		uint64_t limit = destination_size > copy.dstOffset ? destination_size - copy.dstOffset : 0;
		if (source_size != 0) {
			limit = std::min(limit, source_size > copy.srcOffset ? source_size - copy.srcOffset : 0);
		}
		if (copy.size <= limit) {
			continue;
		}
		std::printf("copy-overrun-clamped: %s guest=0x%016" PRIx64 " src_offset=0x%" PRIx64
		            " dst_offset=0x%" PRIx64 " size=0x%" PRIx64 " source_size=0x%" PRIx64
		            " destination_size=0x%" PRIx64 " kept=0x%" PRIx64 "\n",
		            operation, guest, static_cast<uint64_t>(copy.srcOffset),
		            static_cast<uint64_t>(copy.dstOffset), static_cast<uint64_t>(copy.size),
		            source_size, destination_size, limit);
		std::fflush(stdout);
		copy.size = limit;
		dropped |= limit == 0;
	}
	if (dropped) {
		std::erase_if(copies, [](const vk::BufferCopy& copy) { return copy.size == 0; });
	}
}

} // namespace

// KYTY_DEBUG_BDA_STATS=1: every 5 s, how the GPU thread's buffer synchronization spends its time,
// for PrepareBda's dirty-page passes and for the other callers (mostly bound buffers): the tracker
// walk, the copies into the staging buffer and the recording of the copy commands, and how many
// uploads ended a render pass.
namespace {
struct SyncStats {
	static bool Enabled() {
		static const bool enabled = KYTY_GETENV("KYTY_DEBUG_BDA_STATS") != nullptr;
		return enabled;
	}
	static uint64_t Now() {
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		                                 std::chrono::steady_clock::now().time_since_epoch())
		                                 .count());
	}
	uint64_t passes  = 0;
	uint64_t pass_ns = 0;
	// [0]: PrepareBda's passes, [1]: other callers.
	std::array<uint64_t, 2> calls {};
	std::array<uint64_t, 2> uploads {};
	std::array<uint64_t, 2> copies {};
	std::array<uint64_t, 2> bytes {};
	std::array<uint64_t, 2> walk_ns {};
	std::array<uint64_t, 2> copy_ns {};
	std::array<uint64_t, 2> record_ns {};
	std::array<uint64_t, 2> breaks {};
	uint64_t                window_start = 0;
};
thread_local SyncStats t_sync_stats;
// Guest-thread side: InvalidateMemory calls (write faults and kernel writes) and their time.
std::atomic<uint64_t> g_invalidate_calls {0};
std::atomic<uint64_t> g_invalidate_ns {0};
} // namespace

void BufferCache::WriteDataBuffer(Buffer& buffer, uint64_t address, const void* source,
                                  uint64_t size) {
	auto* bytes = static_cast<const uint8_t*>(source);
	while (size != 0) {
		const auto chunk  = std::min(size, m_staging_buffer.Size());
		// Copy waits for ring reuse and flushes the fresh host data before submission.
		const auto offset = m_staging_buffer.Copy(bytes, chunk, 4);
		const auto destination_offset = buffer.Offset(address);
		EXIT_IF(destination_offset > buffer.Size() || chunk > buffer.Size() - destination_offset);
		m_scheduler.EndRendering();
		const auto command = m_scheduler.Current().Handle();
		vk::BufferMemoryBarrier2 before {};
		before.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
		before.srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
		before.dstStageMask  = vk::PipelineStageFlagBits2::eTransfer;
		before.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer        = buffer.Handle();
		before.offset        = destination_offset;
		before.size          = chunk;
		vk::DependencyInfo dependency {};
		dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
		dependency.bufferMemoryBarrierCount = 1;
		dependency.pBufferMemoryBarriers    = &before;
		command.pipelineBarrier2(dependency);
		const vk::BufferCopy copy {offset, destination_offset, chunk};
		command.copyBuffer(m_staging_buffer.Handle(), buffer.Handle(), 1, &copy);
		auto after          = before;
		after.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
		after.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
		after.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
		after.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
		dependency.pBufferMemoryBarriers = &after;
		command.pipelineBarrier2(dependency);
		bytes += chunk;
		address += chunk;
		size -= chunk;
	}
}

void BufferCache::Register(BufferId id) {
	ChangeRegister<true>(id);
}

void BufferCache::Unregister(BufferId id) {
	ChangeRegister<false>(id);
}

// The last CreateBuffer on this thread: the request and the range after joining overlaps, printed
// when a buffer with an invalid range reaches ChangeRegister (an EXIT seen while Ghost of Yotei
// loads).
struct LastBufferCreate {
	uint64_t vaddr = 0, size = 0, begin = 0, end = 0;
	uint32_t joined = 0;
};
static thread_local LastBufferCreate t_last_create;

template <bool insert>
void BufferCache::ChangeRegister(BufferId id) {
	auto& buffer = m_slot_buffers[id];
	PageTable::PageRange pages {};
	if (!(GuestRange {buffer.CpuAddress(), buffer.Size()}.Valid()) ||
	    !PageTable::TryGetPageRange(buffer.CpuAddress(), buffer.Size(), pages)) [[unlikely]] {
#if defined(_WIN32)
		void*      frames[8] {};
		const auto count = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
#endif
		std::printf("\x1b[91mbuffer-register: %s an invalid range: buffer %u address=0x%016llx size=0x%llx; "
		            "last create on this thread: request 0x%016llx+0x%llx -> joined 0x%016llx..0x%016llx "
		            "(%u buffers joined); stack",
		            insert ? "register" : "unregister", static_cast<uint32_t>(id.index),
		            static_cast<unsigned long long>(buffer.CpuAddress()),
		            static_cast<unsigned long long>(buffer.Size()),
		            static_cast<unsigned long long>(t_last_create.vaddr),
		            static_cast<unsigned long long>(t_last_create.size),
		            static_cast<unsigned long long>(t_last_create.begin),
		            static_cast<unsigned long long>(t_last_create.end), t_last_create.joined);
#if defined(_WIN32)
		for (USHORT i = 0; i < count; i++) {
			std::printf(" %p", frames[i]);
		}
#endif
		std::printf("\x1b[0m\n");
		std::fflush(stdout);
	}
	EXIT_IF(!(GuestRange {buffer.CpuAddress(), buffer.Size()}.Valid()) ||
	        !PageTable::TryGetPageRange(buffer.CpuAddress(), buffer.Size(), pages));
	for (size_t page = pages.first; page < pages.last_exclusive; ++page) {
		if constexpr (insert) {
			m_page_table[page] = id;
		} else {
			m_page_table[page] = {};
		}
	}
	const auto size_pages = pages.last_exclusive - pages.first;
	const auto table_offset = PageIndex(buffer.CpuAddress()) * sizeof(vk::DeviceAddress);
	if constexpr (insert) {
		const auto [it, inserted] = m_buffers.emplace(buffer.CpuAddress(), id);
		(void)it;
		EXIT_IF(!inserted);
		m_total_used_memory += buffer.Size();
		// Diagnostic for the 1 TiB page-table hole: buffers in extended memory (their page-table
		// slots alias guest addresses in [1 TiB, 1.5 TiB)); logged for the first few.
		if (buffer.CpuAddress() >= LibKernel::Memory::kExtendedMemoryBase) {
			const auto count = g_extended_registers.fetch_add(1, std::memory_order_relaxed) + 1;
			if (count <= 16 || (count % 1024) == 0) {
				std::printf("bda-extended-buffer: #%" PRIu64 " guest=0x%016" PRIx64 " size=0x%" PRIx64
				            "\n",
				            count, buffer.CpuAddress(), buffer.Size());
			}
		}
		buffer.lru_id   = m_lru_cache.Insert(id, m_gc_tick);
		buffer.lru_tick = m_gc_tick;
		std::vector<vk::DeviceAddress> addresses;
		addresses.reserve(size_pages);
		for (uint64_t i = 0; i < size_pages; ++i) {
			addresses.push_back(buffer.BufferDeviceAddress() + (i << CACHING_PAGEBITS));
		}
		WriteDataBuffer(m_bda_pagetable_buffer, table_offset,
		                addresses.data(), addresses.size() * sizeof(vk::DeviceAddress));
		// Publish the full resulting owner, including pages newly reachable after a merge. The new
		// page-table entries make it readable through addresses before its next upload.
		m_memory_tracker.PublishBdaHints(buffer.CpuAddress(), buffer.Size());
		m_scheduler.Context().AdvanceBdaEpoch();
	} else {
		const auto found = m_buffers.find(buffer.CpuAddress());
		EXIT_IF(found == m_buffers.end() || found->second != id);
		m_buffers.erase(found);
		EXIT_IF(buffer.Size() > m_total_used_memory);
		m_total_used_memory -= buffer.Size();
		m_lru_cache.Free(buffer.lru_id);
		m_bda_pagetable_buffer.Fill(table_offset,
		                            size_pages * sizeof(vk::DeviceAddress), 0);
		buffer.is_deleted = true;
	}
}

void BufferCache::TouchBuffer(const Buffer& buffer) {
	// Draws touch the same buffers many times per GC tick: the LRU holds this tick already.
	if (!buffer.is_deleted && buffer.lru_tick != m_gc_tick) {
		buffer.lru_tick = m_gc_tick;
		m_lru_cache.Touch(buffer.lru_id, m_gc_tick);
	}
}

static const BufferCache* g_describe_cache = nullptr;

void BufferCache::DescribeDeviceAddress(uint64_t address) const {
	// Also the 64 KiB past a buffer: a window or element run that walks off its last page lands
	// there (the next guest page is not necessarily the next device page).
	const auto within = [address](uint64_t base, uint64_t size) {
		return address >= base && address < base + size + 0x10000;
	};
	bool found = false;
	m_slot_buffers.ForEach([&](BufferId id, const Buffer& buffer) {
		if (buffer.HasDeviceAddress() && within(buffer.BufferDeviceAddress(), buffer.Size())) {
			std::printf("device-fault-owner: live buffer id=%u guest=0x%016llx size=0x%llx deleted=%d "
			            "offset=0x%llx%s\n",
			            static_cast<uint32_t>(id.index),
			            static_cast<unsigned long long>(buffer.CpuAddress()),
			            static_cast<unsigned long long>(buffer.Size()), buffer.is_deleted ? 1 : 0,
			            static_cast<unsigned long long>(address - buffer.BufferDeviceAddress()),
			            address >= buffer.BufferDeviceAddress() + buffer.Size() ? " (PAST THE END)" : "");
			found = true;
		}
	});
	for (const auto& freed: m_freed_buffers) {
		if (freed.bda != 0 && freed.size != 0 && within(freed.bda, freed.size)) {
			std::printf("device-fault-owner: freed buffer guest=0x%016llx size=0x%llx deleted at tick %llu "
			            "(completed %llu) offset=0x%llx%s\n",
			            static_cast<unsigned long long>(freed.cpu),
			            static_cast<unsigned long long>(freed.size),
			            static_cast<unsigned long long>(freed.tick),
			            static_cast<unsigned long long>(m_scheduler.GetMasterSemaphore().KnownGpuTick()),
			            static_cast<unsigned long long>(address - freed.bda),
			            address >= freed.bda + freed.size ? " (PAST THE END)" : "");
			found = true;
		}
	}
	// The cache's own buffers: a store past the end of a ring lands just behind one of these.
	const std::pair<const char*, const Buffer*> internal[] = {
	    {"device buffer", &m_device_buffer},     {"stream buffer", &m_stream_buffer},
	    {"staging buffer", &m_staging_buffer},   {"download buffer", &m_download_buffer},
	    {"draw record buffer", &m_draw_record_buffer}, {"GDS buffer", &m_gds_buffer},
	    {"fault buffer", m_fault_manager.PeekFaultBuffer()},
	    {"BDA zero page", &m_bda_zero_page},
	};
	for (const auto& [name, buffer]: internal) {
		if (buffer->HasDeviceAddress() && within(buffer->BufferDeviceAddress(), buffer->Size())) {
			std::printf("device-fault-owner: %s size=0x%llx offset=0x%llx%s\n", name,
			            static_cast<unsigned long long>(buffer->Size()),
			            static_cast<unsigned long long>(address - buffer->BufferDeviceAddress()),
			            address >= buffer->BufferDeviceAddress() + buffer->Size() ? " (PAST THE END)" : "");
			found = true;
		}
	}
	if (m_bda_pagetable_buffer.HasDeviceAddress() &&
	    within(m_bda_pagetable_buffer.BufferDeviceAddress(), m_bda_pagetable_buffer.Size())) {
		std::printf("device-fault-owner: BDA page table\n");
		found = true;
	}
	if (!found) {
		std::printf("device-fault-owner: no game buffer (page table, stream or image memory)\n");
	}
	std::printf("device-fault-owner: %" PRIu64
	            " buffers registered at or above the extended memory base\n",
	            g_extended_registers.load(std::memory_order_relaxed));
	std::fflush(stdout);
	m_texture_cache.DescribeRecentImages();
}

void BufferCache::DeleteBuffer(BufferId id) {
	if (IsBufferInvalid(id)) {
		return;
	}
	{
		const auto& buffer = m_slot_buffers[id];
		auto&       freed  = m_freed_buffers[m_freed_next++ % m_freed_buffers.size()];
		freed = {buffer.HasDeviceAddress() ? buffer.BufferDeviceAddress() : 0, buffer.Size(),
		         buffer.CpuAddress(), m_scheduler.Active() ? m_scheduler.CurrentTick() : 0};
	}
	Unregister(id);
	if (m_scheduler.Active()) {
		m_scheduler.DeferOperation([this, id] { m_slot_buffers.erase(id); });
	} else {
		m_slot_buffers.erase(id);
	}
}

namespace {
[[nodiscard]] bool TargetedReadbackPublicationEnabled() {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_READBACK_TARGETED_PUBLICATION");
		return text != nullptr && std::strcmp(text, "1") == 0;
	}();
	return enabled;
}
} // namespace

struct BufferCache::OrdinaryReadbackPublication {
	uint64_t token = 0, tick = 0;
	std::vector<std::pair<uint64_t, uint64_t>> pages;
	Common::UniqueFunction<void> operation;
	std::mutex mutex;
	std::atomic_bool published {false};
	bool foreground_safe = false;
};

void BufferCache::PublishOrdinaryReadback(OrdinaryReadbackPublication& request) {
	std::scoped_lock lock(request.mutex);
	if (request.published.load(std::memory_order_relaxed)) return;
	EXIT_IF(!request.operation);
	// Serialize only real download publication, never an unrelated FIFO callback.
	std::scoped_lock publication_lock(m_ordinary_publication_mutex);
	request.operation(); // Original invalidate/copy/token-finalize/notify, unchanged.
	request.published.store(true, std::memory_order_release);
	request.operation = {}; // Retire captured dedicated staging after actual publication.
}

void BufferCache::QueueOrdinaryReadbackPublication(uint64_t token, uint64_t tick,
    std::vector<std::pair<uint64_t, uint64_t>> pages, bool foreground_safe,
    Common::UniqueFunction<void>&& operation) {
	if (!TargetedReadbackPublicationEnabled()) {
		m_scheduler.DeferPriorityOperation(std::move(operation));
		return;
	}
	auto request = std::make_shared<OrdinaryReadbackPublication>();
	request->token = token;request->tick = tick;
	request->foreground_safe = foreground_safe;
	request->pages = std::move(pages);request->operation = std::move(operation);
	{
		std::scoped_lock lock(m_ordinary_readback_mutex);
		std::erase_if(m_ordinary_readbacks, [](const auto& weak) { return weak.expired(); });
		m_ordinary_readbacks.emplace_back(request);
	}
	m_scheduler.DeferPriorityOperation([this, request] { PublishOrdinaryReadback(*request); });
}

bool BufferCache::TryPublishCompletedOrdinaryReadback(uint64_t vaddr, uint64_t size, uint64_t tick) {
	if (!TargetedReadbackPublicationEnabled() || m_readback_queue != nullptr ||
	    vaddr == 0 || !GuestRange{vaddr, size}.Valid()) return false;
	static const bool packed = [] {
		const char* value = KYTY_GETENV("KYTY_READBACK_PACKED_BATCH");
		return value != nullptr && std::strcmp(value, "1") == 0;
	}();
	if (packed) return false; // Other publication families are not in this registry.
	const auto begin = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
	const auto end = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
	const auto bytes = end - begin;
	if (!GuestRange{begin, bytes}.Valid() || !m_scheduler.Context().IsMapped(begin, bytes) ||
	    m_texture_cache.HasPendingDownload(begin, bytes) ||
	    m_texture_cache.IsRegionGpuModified(begin, bytes)) return false;
	std::shared_ptr<OrdinaryReadbackPublication> selected;
	{
		std::scoped_lock lock(m_ordinary_readback_mutex);
		bool older_overlap = false;
		for (const auto& weak : m_ordinary_readbacks) {
			auto request = weak.lock();
			if (request == nullptr) continue;
			// Conservative first case: one exact contiguous tracker-page span.
			if (request->foreground_safe && request->tick == tick && request->pages.size() == 1 &&
			    request->pages[0] == std::pair<uint64_t, uint64_t>{begin, bytes} &&
			    m_memory_tracker.IsReadbackArmedBy(begin, bytes, request->token, request->tick)) {
				// An older wider/different-token publisher must never write stale
				// backing after this newer request made its pages accessible.
				if (older_overlap) return false;
				selected = std::move(request);break;
			}
			if (!request->published.load(std::memory_order_acquire)) {
				for (const auto& [address, length] : request->pages) {
					older_overlap = older_overlap || (address < end && begin < address + length);
				}
			}
		}
	}
	if (selected == nullptr || !m_scheduler.GetMasterSemaphore().IsFree(selected->tick)) return false;
	PublishOrdinaryReadback(*selected);
	// A concurrent new writer or image alias retains the original conservative path.
	return !m_memory_tracker.QueryReadback(begin, bytes).gpu_dirty &&
	    !m_texture_cache.HasPendingDownload(begin, bytes) &&
	    !m_texture_cache.IsRegionGpuModified(begin, bytes) &&
	    !m_memory_tracker.QueryReadback(begin, bytes).gpu_dirty;
}

bool BufferCache::DownloadBufferMemory(Buffer& buffer, uint64_t vaddr, uint64_t size,
                                      std::span<const uint8_t> ordered_bytes) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::DownloadBufferMemory);
	EXIT_IF(!ordered_bytes.empty() && ordered_bytes.size() != size);
	std::vector<vk::BufferCopy>                copies;
	std::vector<std::pair<uint64_t, uint64_t>> pages;
	std::vector<std::pair<uint64_t, uint64_t>> pending_ranges;
	uint64_t                                   total_size     = 0;
	const auto                                 buffer_address = buffer.CpuAddress();
	uint64_t                                   known_size     = 0;
	const auto add_copy = [&](uint64_t begin, uint64_t end) {
		if (begin == end) return;
		copies.emplace_back(begin - buffer_address, total_size, end - begin);
		// Keep packed ranges on separate cache lines, as in shadPS4.
		total_size += Common::AlignUp(end - begin, 64);
	};
	// Pages armed by an earlier download already have their bytes in flight.
	m_memory_tracker.ForEachUnarmedDownloadRange(
	    vaddr, size, [&](uint64_t address, uint64_t bytes) noexcept {
		    m_memory_tracker.ValidateGpuDirtyPages(m_gpu_modified_ranges, address, bytes,
		                                           "buffer download");
		    pages.emplace_back(address, bytes);
		    m_gpu_modified_ranges.ForEachInRange(address, bytes, [&](uint64_t start, uint64_t end) {
			    pending_ranges.emplace_back(start, end - start);
			    if (ordered_bytes.empty() || end <= vaddr || start >= vaddr + size) {
				    add_copy(start, end);
				    return;
			    }
			    const auto known_begin = std::max(start, vaddr);
			    const auto known_end   = std::min(end, vaddr + size);
			    known_size += known_end - known_begin;
			    add_copy(start, known_begin);
			    add_copy(known_end, end);
		    });
		    m_gpu_modified_ranges.Subtract(address, bytes);
	    });
	if (pages.empty()) {
		return false;
	}
	// Only the immediate ordered native store supplies these bytes. Unknown GPU
	// writes elsewhere on the same page still use their normal download copies.
	EXIT_IF(!ordered_bytes.empty() && known_size != ordered_bytes.size());
	std::vector<uint8_t> known_bytes(ordered_bytes.begin(), ordered_bytes.end());
	const bool causal_ordered = LabelCausalTrace::Enabled() && !ordered_bytes.empty();
	uint64_t causal_value = 0;
	if (causal_ordered && ordered_bytes.size() <= sizeof(causal_value))
		std::memcpy(&causal_value, ordered_bytes.data(), ordered_bytes.size());
	// Arm after any ring wrap (which can submit and move the tick) and before queueing the
	// publication, which may run inline during shutdown. Waiters only wait on submitted ticks,
	// so they cannot observe an arm before its publication is queued.
	const auto token = ++m_readback_token;
	const auto notification = GuestGpu::CaptureReadbackNotification();
	auto publication_pages = TargetedReadbackPublicationEnabled() ? pages
	    : std::vector<std::pair<uint64_t, uint64_t>>{};
	const auto arm   = [this, &pages, token] {
		for (const auto& [address, bytes]: pages) {
			m_memory_tracker.ArmReadback(address, bytes, token, m_scheduler.CurrentTick());
		}
	};
	if (copies.empty() && known_bytes.empty()) {
		// Dirty pages without recorded bytes carry nothing to copy; their publication only
		// ends their GPU ownership.
		arm();
		QueueOrdinaryReadbackPublication(token, m_scheduler.CurrentTick(), std::move(publication_pages), true,
		    [this, token, notification, pages = std::move(pages)] {
			FunctionProfile::Scope function_profile(FunctionProfile::Site::BufferPublish);
			for (const auto& [address, bytes]: pages) {
				m_memory_tracker.FinalizeReadback(address, bytes, token);
			}
			if (notification != nullptr) notification->Notify();
		});
		return true;
	}
	uint8_t* mapped = nullptr;
	uint64_t offset = 0;
	std::unique_ptr<Buffer> temporary;
	Buffer* download = nullptr;
	if (!copies.empty()) {
		DrainStats::Record(DrainStats::Kind::Readback, total_size);
		const auto reservation = m_download_buffer.Map(total_size, 64);
		mapped = reservation.first;
		offset = reservation.second;
		download = &m_download_buffer;
		if (mapped == nullptr) {
			// Ring wraps already wait for retirement. A reservation larger than the ring needs
			// separate host staging, kept alive until the priority callback publishes the bytes.
			temporary = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Download, 0,
			                                     vk::BufferUsageFlagBits::eTransferDst, total_size);
			download  = temporary.get();
			mapped    = download->Mapped().data();
			offset    = 0;
			EXIT_IF(mapped == nullptr);
		} else {
			m_download_buffer.Commit();
		}
		for (auto& copy: copies) {
			copy.dstOffset += offset;
		}

		auto& command = m_scheduler.Current();
		command.EndRendering();
		const auto              native = command.Handle();
		GpuZones::Mark(native, DrainStats::Zone::BufferCopy);
		vk::BufferMemoryBarrier before {};
		before.srcAccessMask       = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		before.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer              = buffer.Handle();
		before.offset              = 0;
		before.size                = buffer.Size();
		native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                       vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 1, &before, 0,
		                       nullptr);
		ClampBufferCopies(copies, buffer.Size(), download->Size(), "download", buffer.CpuAddress());
		if (!copies.empty()) {
			native.copyBuffer(buffer.Handle(), download->Handle(), static_cast<uint32_t>(copies.size()),
			                  copies.data());
		}

		auto after          = before;
		after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		after.dstAccessMask = vk::AccessFlagBits::eHostRead;
		after.buffer        = download->Handle();
		after.offset        = offset;
		after.size          = total_size;
		native.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                       vk::PipelineStageFlagBits::eAllCommands |
		                           vk::PipelineStageFlagBits::eHost,
		                       {}, 0, nullptr, 1, &after, 0, nullptr);
	}
	arm();
	const auto causal_tick = m_scheduler.CurrentTick();
	if (causal_ordered)
		LabelCausalTrace::Record(LabelCausalTrace::Stage::ReadbackArm,
		    reinterpret_cast<uint64_t>(&m_scheduler), causal_tick, token,
		    vaddr, ordered_bytes.size(), causal_value);
	PruneInFlight();
	m_gpu_dirty_generation++;
	auto& inflight = m_inflight_downloads.emplace_back();
	inflight.tick  = m_scheduler.CurrentTick();
	// Known bytes also remain GPU-owned until this tick is actually published.
	// Otherwise a clean-backing read could observe the old CPU label before the fence.
	inflight.ranges = std::move(pending_ranges);
	QueueOrdinaryReadbackPublication(token, causal_tick, std::move(publication_pages),
	    download == nullptr || download->IsCoherent(),
	    [this, token, notification, pages = std::move(pages), download,
	                                    owner = std::move(temporary), mapped, offset, total_size,
	                                    buffer_address, copies = std::move(copies), vaddr,
	                                    known_bytes = std::move(known_bytes), causal_tick, causal_value, causal_ordered] {
		if (causal_ordered)
			LabelCausalTrace::Record(LabelCausalTrace::Stage::PublishBegin,
			    reinterpret_cast<uint64_t>(&m_scheduler), causal_tick, token,
			    vaddr, known_bytes.size(), causal_value);
		FunctionProfile::Scope function_profile(FunctionProfile::Site::BufferPublish);
		if (download != nullptr) download->Invalidate(offset, total_size);
		for (const auto& copy: copies) {
			Libs::LibKernel::Memory::WriteBacking(buffer_address + copy.srcOffset,
			                                      mapped + (copy.dstOffset - offset), copy.size);
		}
		if (!known_bytes.empty()) {
			Libs::LibKernel::Memory::WriteBacking(vaddr, known_bytes.data(), known_bytes.size());
			if (causal_ordered)
				LabelCausalTrace::Record(LabelCausalTrace::Stage::BackingWritten,
				    reinterpret_cast<uint64_t>(&m_scheduler), causal_tick, token,
				    vaddr, known_bytes.size(), causal_value);
		}
		(void)owner; // Retain dedicated staging through the final backing write.
		// The backing holds the GPU bytes now; pages still armed by this download become
		// accessible. A page re-dirtied meanwhile keeps its protection for the next download.
		for (const auto& [address, bytes]: pages) {
			m_memory_tracker.FinalizeReadback(address, bytes, token);
		}
		if (causal_ordered)
			LabelCausalTrace::Record(LabelCausalTrace::Stage::Finalized,
			    reinterpret_cast<uint64_t>(&m_scheduler), causal_tick, token,
			    vaddr, known_bytes.size(), causal_value);
		if (notification != nullptr) notification->Notify();
	});
	return true;
}

// The stream ring takes the render thread's per-draw copies: small CPU-written buffers, flattened
// SRTs, shader data and index data. In cached host memory the copies stay in the CPU's caches. In
// write-combined device memory (ReBAR) they cost Sky Garden 0.32 us a draw and the overworld 0.26,
// mostly by stalling the memory accesses after them; the GPU reads host copies about as fast (1%
// more GPU time in the overworld, none measurable in Sky Garden). KYTY_DEBUG_STREAM_DEVICE=1
// keeps the ring in device memory.
static bool StreamRingInHostMemory() {
	return KYTY_GETENV("KYTY_DEBUG_STREAM_DEVICE") == nullptr;
}

// KYTY_TABLE_MODE=1: table programs read stream copies by device address (their table block and
// the small buffers ObtainBuffer streams).
static vk::BufferUsageFlags StreamRingFlags() {
	const char* table = KYTY_GETENV("KYTY_TABLE_MODE");
	return table != nullptr && std::strcmp(table, "1") == 0
	           ? vk::BufferUsageFlags {vk::BufferUsageFlagBits::eShaderDeviceAddress}
	           : vk::BufferUsageFlags {};
}

BufferCache::BufferCache(GraphicContext& graphics, CommandScheduler& scheduler,
                         PageManager& page_manager, TextureCache& texture_cache)
    : m_graphics(graphics), m_scheduler(scheduler), m_fault_manager(graphics, scheduler, *this),
      m_gds_buffer(graphics, scheduler, MemoryUsage::Stream, 0, AllFlags, GdsBufferSize),
      m_bda_pagetable_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 0, AllFlags,
                             BDA_PAGETABLE_SIZE),
      m_bda_zero_page(graphics, scheduler, MemoryUsage::DeviceLocal, 0,
                      AllFlags | vk::BufferUsageFlagBits::eShaderDeviceAddress, CACHING_PAGESIZE),
      m_memory_tracker(page_manager),
      m_staging_buffer(graphics, scheduler, MemoryUsage::Upload, 512 * MiB),
      m_stream_buffer(graphics, scheduler, MemoryUsage::Stream, 64 * MiB, StreamRingFlags(),
                      StreamRingInHostMemory()),
      m_download_buffer(graphics, scheduler, MemoryUsage::Download, 64 * MiB),
      m_device_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 128 * MiB),
      m_draw_record_buffer(graphics, scheduler, MemoryUsage::Stream, 4 * MiB,
                           vk::BufferUsageFlagBits::eShaderDeviceAddress),
      m_texture_cache(texture_cache) {
	std::memset(m_gds_buffer.Mapped().data(), 0, static_cast<size_t>(m_gds_buffer.Size()));
	m_gds_buffer.Flush(0, m_gds_buffer.Size());
	SetVulkanObjectNameF(m_graphics.device, m_bda_pagetable_buffer.Handle(),
	                     "BDA Page Table Buffer");
	SetVulkanObjectNameF(m_graphics.device, m_bda_zero_page.Handle(), "BDA Zero Page");
	// Both live for the whole session; their addresses place device faults (DescribeDeviceAddress).
	std::printf("bda-page-table: device=0x%016llx size=0x%llx\n",
	            static_cast<unsigned long long>(m_bda_pagetable_buffer.HasDeviceAddress()
	                                                ? m_bda_pagetable_buffer.BufferDeviceAddress()
	                                                : 0),
	            static_cast<unsigned long long>(m_bda_pagetable_buffer.Size()));
	g_describe_cache                     = this;
	GpuZones::g_describe_device_address = [](uint64_t address) {
		if (g_describe_cache != nullptr) {
			g_describe_cache->DescribeDeviceAddress(address);
		}
	};
	{
		// Storage buffer bounds facts for the device-loss hunt: whether range checks are left to
		// the hardware, the device limit, and the range the page table is bound with.
		const auto limit = m_graphics.GetPhysicalDeviceProperties().limits.maxStorageBufferRange;
		std::printf("storage-bounds: hardware_storage_buffer_bounds=%d maxStorageBufferRange=0x%llx "
		            "pagetable-descriptor-range=0x%llx%s\n",
		            m_graphics.hardware_storage_buffer_bounds ? 1 : 0,
		            static_cast<unsigned long long>(limit),
		            static_cast<unsigned long long>(m_bda_pagetable_buffer.Size()),
		            m_bda_pagetable_buffer.Size() > limit ? " (EXCEEDS THE LIMIT)" : "");
		std::fflush(stdout);
		if (const char* probe = KYTY_GETENV("KYTY_DEBUG_PAGETABLE_PROBE"); probe != nullptr) {
			unsigned long long entry = 0;
			unsigned long long value = 0;
			if (std::sscanf(probe, "%llu:%llx", &entry, &value) == 2) {
				SetPageTableProbe(entry, value);
			}
		}
	}
	const auto null_id =
	    m_slot_buffers.insert(m_graphics, m_scheduler, MemoryUsage::DeviceLocal, 0, AllFlags, 16);
	EXIT_IF(null_id != NULL_BUFFER_ID);
	SetVulkanObjectNameF(m_graphics.device, GetBuffer(null_id).Handle(), "Kyty.NullBuffer");
	if (m_graphics.readback_queue != nullptr) {
		m_readback_queue = std::make_unique<ReadbackQueue>(m_graphics);
	}
	if (AbSelected("streamhost")) {
		m_stream_device = std::make_unique<StreamBuffer>(graphics, scheduler, MemoryUsage::Stream,
		                                                 64 * MiB, StreamRingFlags(), false);
		// Vulkan memory property bits: 1 device-local, 2 host-visible, 4 coherent, 8 cached.
		std::printf("streamhost A/B: ring memory 0x%x, device ring memory 0x%x\n",
		            m_stream_buffer.MemoryProperties(), m_stream_device->MemoryProperties());
		std::fflush(stdout);
	}
	if (!m_graphics.CanReportMemoryUsage()) {
		return;
	}
	constexpr int64_t GiB              = 1024ll * 1024 * 1024;
	constexpr int64_t target_threshold = 8 * GiB;
	const auto        budget =
	    static_cast<int64_t>(std::min<uint64_t>(m_graphics.GetTotalMemoryBudget(), INT64_MAX));
	const auto threshold = std::min(budget, target_threshold);
	const auto expected  = std::min(budget - 6 * threshold / 10, budget - GiB);
	const auto critical  = std::min(budget - 2 * threshold / 10, budget - GiB / 2);
	m_trigger_gc_memory  = static_cast<uint64_t>(std::max<int64_t>(expected, GiB));
	m_critical_gc_memory = static_cast<uint64_t>(std::max<int64_t>(critical, 2 * GiB));
}

BufferCache::~BufferCache() {
	GpuZones::g_describe_device_address = nullptr;
	g_describe_cache                     = nullptr;
	m_readback_queue.reset();
	if (!m_gpu_modified_ranges.Empty()) {
		EXIT("BufferCache: destroyed with pending GPU-modified ranges\n");
	}
	for (const auto& [vaddr, id]: m_buffers) {
		(void)vaddr;
		const auto& buffer = m_slot_buffers[id];
		if (m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size())) {
			EXIT("BufferCache: destroyed with GPU-modified buffer\n");
		}
	}
	m_buffers.clear();
}

void BufferCache::InvalidateMemory(uint64_t vaddr, uint64_t size, MemoryReadOrigin origin) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::InvalidateMemory);
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid memory-invalidation range\n");
	}
	struct Timer {
		bool                                  on;
		std::chrono::steady_clock::time_point start;
		~Timer() {
			if (on) {
				g_invalidate_calls.fetch_add(1, std::memory_order_relaxed);
				g_invalidate_ns.fetch_add(
				    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				                              std::chrono::steady_clock::now() - start)
				                              .count()),
				    std::memory_order_relaxed);
			}
		}
	} timer {SyncStats::Enabled(), SyncStats::Enabled() ? std::chrono::steady_clock::now()
	                                                     : std::chrono::steady_clock::time_point {}};
	// A CPU write needs the whole range CPU-dirty. GPU-dirty pages are published first; a GPU
	// write recorded while this thread waits can re-dirty a page, so repeat until all are marked.
	for (;;) {
		bool gpu_dirty = false;
		m_memory_tracker.InvalidateRegion(vaddr, size, [&gpu_dirty] { gpu_dirty = true; });
		if (!gpu_dirty) {
			return;
		}
		ReadMemory(vaddr, size, true, origin);
	}
}

static uint64_t FaultAheadWindow() {
	static const uint64_t window = [] {
		const char* text  = KYTY_GETENV("KYTY_FAULT_AHEAD_PAGES");
		const auto  pages = text != nullptr ? std::strtoull(text, nullptr, 10) : 16ull;
		return std::has_single_bit(pages) && pages <= TRACKER_REGION_PAGES ? pages * TRACKER_PAGE_SIZE
		                                                                   : TRACKER_PAGE_SIZE;
	}();
	return window;
}

static bool CpuWriteFaultWindowEnabled() {
	// Locality is a performance policy. Preserve the old default until world
	// throughput, as well as native data preservation, demonstrates a benefit.
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_CPU_WRITE_FAULT_WINDOW");
		return text != nullptr && std::strcmp(text, "1") == 0;
	}();
	return enabled;
}

void BufferCache::ExpandWriteFault(uint64_t vaddr) {
	const auto window = FaultAheadWindow();
	if (window > TRACKER_PAGE_SIZE) {
		m_memory_tracker.ExpandWriteFault(vaddr, window);
	}
}

void BufferCache::RecordReadback(uint64_t vaddr, uint64_t size, bool is_write, MemoryReadOrigin origin) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::RecordReadback);
	if (is_write && !IsRegionRegistered(vaddr, size)) {
		if (m_memory_tracker.IsRegionGpuModified(vaddr, size)) {
			EXIT("BufferCache: GPU-dirty memory has no buffer owner, addr=0x%016" PRIx64
			     " size=0x%016" PRIx64 "\n",
			     vaddr, size);
		}
		return;
	}
	// Unknown or partial owners keep normal discovery and its conservative window.
	// Inspect the live owner before FindBuffer can create or merge one.
	bool write_fault_window = CpuWriteFaultWindowEnabled() && is_write &&
	                          origin == MemoryReadOrigin::WriteFault;
	if (write_fault_window) {
		const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
		write_fault_window = owner != nullptr && *owner && !IsBufferInvalid(*owner) &&
		                     m_slot_buffers[*owner].IsInBounds(vaddr, size);
	}
	auto& buffer = m_slot_buffers[FindBuffer(vaddr, size)];

	// Widen nearby CPU reads so they share one publication.
	constexpr uint64_t WindowSize   = 512 * 1024;
	const auto         buffer_begin = buffer.CpuAddress();
	const auto         buffer_end   = buffer_begin + buffer.Size();
	auto window_begin = std::max(Common::AlignDown(vaddr, WindowSize), buffer_begin);
	auto window_end = std::min(std::max(window_begin + WindowSize, vaddr + size), buffer_end);
	if (write_fault_window) {
		const auto local_window = std::min(FaultAheadWindow(), WindowSize);
		const auto begin = std::max(Common::AlignDown(vaddr, local_window), buffer_begin);
		const auto end = std::min(std::max(begin + local_window, vaddr + size), buffer_end);
		const auto page_begin = Common::AlignDown(begin, TRACKER_PAGE_SIZE);
		const auto page_end = Common::AlignUp(end, TRACKER_PAGE_SIZE);
		if (m_scheduler.Context().IsMapped(page_begin, page_end - page_begin) &&
		    !m_texture_cache.HasPendingDownload(page_begin, page_end - page_begin) &&
		    !m_texture_cache.IsRegionGpuModified(page_begin, page_end - page_begin)) {
			window_begin = begin;
			window_end = end;
		}
	}
	if (!DownloadBufferMemory(buffer, window_begin, window_end - window_begin)) {
		DrainStats::Record(DrainStats::Kind::ReadbackClean, 0);
	}
}

bool BufferCache::PollReadMemory(uint64_t vaddr, uint64_t size) {
	EXIT_IF(!GuestGpu::IsGpuThread() || CommandScheduler::InDeferredOperation());
	// Host-only PM4 fixtures and addresses outside GPU mappings retain their old read path.
	// The actual dereference still performs the existing validation/fault behavior.
	if (vaddr == 0 || !GuestRange {vaddr, size}.Valid() ||
	    !m_scheduler.Context().IsMapped(vaddr, size)) {
		WaitPollDiagnostic::NotePoll(WaitPollDiagnostic::PollPath::FallbackReady);
		return true;
	}
	auto state = m_memory_tracker.QueryReadback(vaddr, size);
	if (!state.gpu_dirty) {
		WaitPollDiagnostic::NotePoll(WaitPollDiagnostic::PollPath::TrackerClean);
		return true;
	}
	if (state.unarmed) {
		// CPU readers batch neighboring pages for locality. A GPU label poll instead
		// publishes only the touched tracker pages, including every dirty byte on them.
		// Retain normal owner discovery, barriers, token and staging retirement.
		auto& buffer = m_slot_buffers[FindBuffer(vaddr, size)];
		if (!DownloadBufferMemory(buffer, vaddr, size)) {
			DrainStats::Record(DrainStats::Kind::ReadbackClean, 0);
		}
		state = m_memory_tracker.QueryReadback(vaddr, size);
	}
	if (!state.gpu_dirty) {
		WaitPollDiagnostic::NotePoll(WaitPollDiagnostic::PollPath::DownloadNowClean);
		return true;
	}
	EXIT_IF(state.unarmed || state.tick > m_scheduler.CurrentTick());
	// A publication queued on the open buffer cannot progress unless it is submitted.
	// Submission preserves queue order; the command parser does not wait for its fence.
	const bool wait_diag_flushed = state.tick == m_scheduler.CurrentTick();
	if (wait_diag_flushed) m_scheduler.Flush();
	WaitPollDiagnostic::NotePoll(wait_diag_flushed ? WaitPollDiagnostic::PollPath::UnpublishedFlushed :
	    WaitPollDiagnostic::PollPath::UnpublishedSubmitted, state.tick);
	return false;
}

static bool ExplicitGpuReadPagesEnabled() {
	// Exact pages reduce transfer bytes, but locality may still benefit the next
	// consumer. Keep the experiment opt-in until matched world throughput improves.
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_GPU_READ_EXACT_PAGES");
		return text != nullptr && std::strcmp(text, "1") == 0;
	}();
	return enabled;
}

void BufferCache::ReadMemory(uint64_t vaddr, uint64_t size, bool is_write, MemoryReadOrigin origin) {
	if (!GuestGpu::IsGpuThread() && CommandScheduler::InDeferredOperation()) {
		EXIT("unsupported buffer readback from an asynchronous GPU completion, "
		     "addr=0x%016" PRIx64 " size=0x%016" PRIx64 "\n",
		     vaddr, size);
	}
	if (!GuestGpu::IsGpuThread()) {
		ReadMemoryAsync(vaddr, size, is_write, origin);
		return;
	}
	// Thread_Gpu consumes the bytes itself, so it waits here. Pages armed by an earlier
	// (already submitted) download only need their own tick, which is not a full drain.
	// Its reads do not mark pages hot: they read shader code sharing a page with GPU data, or
	// arguments written just before, where an eager download only delays the next wait.
	ReadbackState state;
	// Only an existing, fully registered owner can take the ready-range path.
	// Keep discovery and its validation for new, null or invalid requests.
	bool tracked = false;
	if (vaddr != 0 && GuestRange {vaddr, size}.Valid() && m_scheduler.Context().IsMapped(vaddr, size)) {
		const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
		tracked = owner != nullptr && *owner && !IsBufferInvalid(*owner) &&
		          m_slot_buffers[*owner].IsInBounds(vaddr, size);
	}
	if (tracked) {
		state = m_memory_tracker.QueryReadback(vaddr, size);
		if (state.unarmed) {
			bool exact_pages = ExplicitGpuReadPagesEnabled() && !is_write && origin == MemoryReadOrigin::Explicit;
			if (exact_pages) {
				// Releasing a tracker page requires all its actual dirty bytes. Keep the
				// conservative image/publication path for any alias on these same pages.
				const auto page_begin = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
				const auto page_end = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
				exact_pages = !m_texture_cache.HasPendingDownload(page_begin, page_end - page_begin) &&
				              !m_texture_cache.IsRegionGpuModified(page_begin, page_end - page_begin);
			}
			Buffer* exact_owner = nullptr;
			if (exact_pages) {
				const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
				if (owner != nullptr && *owner && !IsBufferInvalid(*owner)) {
					auto& buffer = m_slot_buffers[*owner];
					const auto begin = buffer.CpuAddress();
					if (vaddr >= begin && vaddr - begin <= buffer.Size() &&
					    size <= buffer.Size() - (vaddr - begin)) exact_owner = &buffer;
				}
			}
			if (exact_owner != nullptr) {
				if (!DownloadBufferMemory(*exact_owner, vaddr, size)) {
					DrainStats::Record(DrainStats::Kind::ReadbackClean, 0);
				}
			} else {
				RecordReadback(vaddr, size, is_write, origin);
			}
			state = m_memory_tracker.QueryReadback(vaddr, size);
		}
	} else {
		RecordReadback(vaddr, size, is_write, origin);
		state = m_memory_tracker.QueryReadback(vaddr, size);
	}
	if (state.gpu_dirty) {
		EXIT_IF(state.unarmed);
		m_scheduler.Wait(state.tick);
		m_scheduler.WaitPriorityOperations(state.tick);
	}
	if (is_write) {
		m_memory_tracker.MarkRegionAsCpuModified(vaddr, size);
		m_scheduler.Context().AdvanceBdaEpoch();
	}
}

void BufferCache::NoteGpuWrite(uint64_t vaddr, uint64_t size) {
	// Starting, or no point between commands for very long: forget what is not stamped and
	// refuse to answer until everything recorded so far has completed.
	constexpr size_t Limit = size_t {1} << 20u;
	if (!m_gpu_writes_on || m_gpu_writes.size() - m_gpu_writes_head >= Limit) {
		m_gpu_writes_on = true;
		m_gpu_writes.clear();
		m_gpu_writes_head = m_gpu_writes_stamped = 0;
		m_gpu_writes_from = UINT64_MAX;
	}
	m_gpu_writes.push_back({vaddr, vaddr + size, 0});
}

uint64_t BufferCache::InflightWriteTick(uint64_t begin, uint64_t end, uint64_t completed) {
	const uint64_t current = m_scheduler.CurrentTick();
	for (; m_gpu_writes_stamped < m_gpu_writes.size(); ++m_gpu_writes_stamped) {
		m_gpu_writes[m_gpu_writes_stamped].tick = current;
	}
	if (m_gpu_writes_from == UINT64_MAX) {
		m_gpu_writes_from = current;
	}
	// Stamps never decrease: the completed writes are a prefix.
	while (m_gpu_writes_head < m_gpu_writes.size() &&
	       m_gpu_writes[m_gpu_writes_head].tick <= completed) {
		++m_gpu_writes_head;
	}
	if (m_gpu_writes_head >= 4096 && m_gpu_writes_head * 2 >= m_gpu_writes.size()) {
		m_gpu_writes.erase(m_gpu_writes.begin(),
		                   m_gpu_writes.begin() + static_cast<std::ptrdiff_t>(m_gpu_writes_head));
		m_gpu_writes_stamped -= m_gpu_writes_head;
		m_gpu_writes_head = 0;
	}
	uint64_t tick = 0;
	for (size_t i = m_gpu_writes_head; i < m_gpu_writes.size(); ++i) {
		const auto& write = m_gpu_writes[i];
		if (write.begin < end && begin < write.end) {
			tick = std::max(tick, write.tick);
		}
	}
	return tick;
}

namespace {
// KYTY_DEBUG_READBACK_AGE=1: guest threads' waits for GPU-written bytes, by the state of the
// bytes' last GPU write when the download was recorded: finished, submitted, still recording,
// or unknown (the write log is incomplete), plus waits for an already recorded download.
struct ReadbackAgeStats {
	enum Class : uint32_t { Done, Submitted, Open, Unknown, Armed, Queued, Count };
	std::mutex                  mutex;
	std::array<uint64_t, Count> n {};
	std::array<double, Count>   ms {};
	uint64_t                    total = 0;
	void Add(uint32_t c, double wait_ms) {
		std::scoped_lock lock(mutex);
		n[c]++;
		ms[c] += wait_ms;
		if (++total % 500u == 0u) {
			std::printf("readback-age: %" PRIu64 " waits | done n=%" PRIu64 " %.0fms | submitted n=%" PRIu64
			            " %.0fms | open n=%" PRIu64 " %.0fms | unknown n=%" PRIu64
			            " %.0fms | armed n=%" PRIu64 " %.0fms | readback queue n=%" PRIu64
			            " %.0fms\n",
			            total, n[Done], ms[Done], n[Submitted], ms[Submitted], n[Open], ms[Open],
			            n[Unknown], ms[Unknown], n[Armed], ms[Armed], n[Queued], ms[Queued]);
			std::fflush(stdout);
		}
	}
};
ReadbackAgeStats g_readback_age;
} // namespace

struct BufferCache::QueuedReadback {
	vk::Buffer                                 source;
	std::unique_ptr<Buffer>                    download;
	std::vector<vk::BufferCopy>                copies;
	std::vector<std::pair<uint64_t, uint64_t>> pages;
	uint64_t                                   buffer_address = 0;
	uint64_t                                   token          = 0;
	uint64_t                                   total_size     = 0;
	uint64_t                                   wait_tick      = 0;
	uint64_t                                   queue_value    = 0;
	bool                                       published      = false;
	std::shared_ptr<GuestGpu::ReadbackNotification> notification;
	std::mutex                                 mutex;
};

std::shared_ptr<BufferCache::QueuedReadback> BufferCache::QueueReadback(uint64_t vaddr,
                                                                        uint64_t size,
                                                                        bool     is_write) {
	if (is_write && !IsRegionRegistered(vaddr, size)) {
		return nullptr;
	}
	auto& buffer = m_slot_buffers[FindBuffer(vaddr, size)];
	// The window RecordReadback would download.
	constexpr uint64_t WindowSize   = 512 * 1024;
	const auto         buffer_begin = buffer.CpuAddress();
	const auto         buffer_end   = buffer_begin + buffer.Size();
	auto window_begin = std::max(Common::AlignDown(vaddr, WindowSize), buffer_begin);
	auto window_end   = std::min(std::max(window_begin + WindowSize, vaddr + size), buffer_end);
	auto& master      = m_scheduler.GetMasterSemaphore();
	master.Refresh();
	const uint64_t completed = master.KnownGpuTick();
	// KYTY_DEBUG_READBACK_AGE: outcomes (queued window, queued pages, writes in flight, log
	// incomplete, nothing unarmed).
	static std::array<uint64_t, 5> outcomes {};
	const auto                     note = [](size_t outcome) {
        if (ReadbackAgeEnabled() && ++outcomes[outcome] % 1000u == 0u) {
            std::printf("readback queue: window=%" PRIu64 " pages=%" PRIu64 " in-flight=%" PRIu64
			                                " log=%" PRIu64 " clean=%" PRIu64 "\n",
			                                outcomes[0], outcomes[1], outcomes[2], outcomes[3], outcomes[4]);
            std::fflush(stdout);
        }
	};
	if (!GpuWriteLogComplete(completed)) {
		note(3);
		return nullptr;
	}
	size_t   outcome  = 0;
	uint64_t producer = InflightWriteTick(window_begin, window_end, completed);
	if (producer != 0) {
		// The window is still being written elsewhere: download only the requested pages, after
		// their last writer, when that one is submitted (the open command buffer's would need a
		// flush, which the graphics queue path does anyway).
		window_begin = std::max(Common::AlignDown(vaddr, TRACKER_PAGE_SIZE), buffer_begin);
		window_end   = std::min(Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE), buffer_end);
		producer     = InflightWriteTick(window_begin, window_end, completed);
		if (producer >= m_scheduler.CurrentTick()) {
			note(2);
			return nullptr;
		}
		outcome = 1;
	}
	auto request            = std::make_shared<QueuedReadback>();
	request->buffer_address = buffer_begin;
	request->notification = GuestGpu::CaptureReadbackNotification();
	m_memory_tracker.ForEachUnarmedDownloadRange(
	    window_begin, window_end - window_begin, [&](uint64_t address, uint64_t bytes) noexcept {
		    m_memory_tracker.ValidateGpuDirtyPages(m_gpu_modified_ranges, address, bytes,
		                                           "queued buffer download");
		    request->pages.emplace_back(address, bytes);
		    m_gpu_modified_ranges.ForEachInRange(address, bytes, [&](uint64_t start, uint64_t end) {
			    request->copies.emplace_back(start - buffer_begin, request->total_size, end - start);
			    request->total_size += Common::AlignUp(end - start, 64);
		    });
		    m_gpu_modified_ranges.Subtract(address, bytes);
	    });
	if (request->pages.empty()) {
		note(4);
		return nullptr;
	}
	note(outcome);
	if (!request->copies.empty()) {
		DrainStats::Record(DrainStats::Kind::Readback, request->total_size);
		request->download =
		    std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Download, 0,
		                             vk::BufferUsageFlagBits::eTransferDst, request->total_size);
		EXIT_IF(request->download->Mapped().empty());
		request->source = buffer.Handle();
	}
	request->wait_tick = std::max(completed, producer);
	request->token     = ++m_readback_token;
	for (const auto& [address, bytes]: request->pages) {
		m_memory_tracker.ArmReadback(address, bytes, request->token, m_scheduler.CurrentTick());
	}
	PruneInFlight();
	m_gpu_dirty_generation++;
	auto& inflight = m_inflight_downloads.emplace_back();
	inflight.tick  = m_scheduler.CurrentTick();
	inflight.ranges.reserve(request->copies.size());
	for (const auto& copy: request->copies) {
		inflight.ranges.emplace_back(buffer_begin + copy.srcOffset, copy.size);
	}
	m_scheduler.DeferPriorityOperation([this, request] { PublishQueuedReadback(*request); });
	return request;
}

void BufferCache::PublishQueuedReadback(QueuedReadback& request) {
	std::scoped_lock lock(request.mutex);
	if (request.published) {
		return;
	}
	if (!request.copies.empty()) {
		if (request.queue_value == 0) {
			request.queue_value = m_readback_queue->Copy(
			    request.source, request.download->Handle(), request.copies,
			    m_scheduler.GetMasterSemaphore().Handle(), request.wait_tick);
		}
		m_readback_queue->Wait(request.queue_value);
		request.download->Invalidate(0, request.total_size);
		const auto* mapped = request.download->Mapped().data();
		for (const auto& copy: request.copies) {
			Libs::LibKernel::Memory::WriteBacking(request.buffer_address + copy.srcOffset,
			                                      mapped + copy.dstOffset, copy.size);
		}
	}
	for (const auto& [address, bytes]: request.pages) {
		m_memory_tracker.FinalizeReadback(address, bytes, request.token);
	}
	request.published = true;
	request.download.reset();
	if (request.notification != nullptr) request.notification->Notify();
}

static bool ReadbackReadyAfterWaitEnabled() {
	// Keep this dependency optimization optional until clean world measurements.
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_READBACK_READY_AFTER_WAIT");
		return text != nullptr && std::strcmp(text, "1") == 0;
	}();
	return enabled;
}

void BufferCache::ReadMemoryAsync(uint64_t vaddr, uint64_t size, bool is_write, MemoryReadOrigin origin) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::ReadMemoryAsync);
	// Thread_Gpu only records and submits the download; this thread waits for its own
	// publication while Thread_Gpu keeps working. The caller retries the access afterwards:
	// a page the GPU re-dirtied meanwhile simply faults again.
	auto     state = m_memory_tracker.QueryReadback(vaddr, size);
	uint64_t tick  = state.gpu_dirty ? state.tick : 0;
	if (!state.gpu_dirty) {
		return;
	}
	// Relaxed readback: a download already carries the GPU's new bytes, so the read may see the
	// previous ones meanwhile, as on hardware when the CPU reads before the GPU has written.
	if (!is_write && !state.unarmed && Config::RelaxedReadbackEnabled() &&
	    m_memory_tracker.GrantStaleRead(vaddr, size)) {
		DrainStats::Record(DrainStats::Kind::StaleRead, 1);
		return;
	}
	uint32_t                        age = ReadbackAgeStats::Armed;
	std::shared_ptr<QueuedReadback> queued;
	if (state.unarmed || tick >= m_scheduler.CurrentTick()) {
		const auto reason = DrainStats::CurrentReason();
		m_scheduler.Context().GetGpu().SendCommandSync([this, vaddr, size, is_write, origin, reason, &tick,
		                                                &age, &queued] {
			FunctionProfile::Scope function_profile(FunctionProfile::Site::ReadbackGpuService);
			DrainStats::ReasonScope reason_scope(reason);
			// The foreign snapshot preceded queueing; publication or another reader
			// may have resolved/armed this range before the GPU callback runs.
			const auto queued_state = m_memory_tracker.QueryReadback(vaddr, size);
			if (!queued_state.gpu_dirty) {
				tick = 0;
				return;
			}
			if (!queued_state.unarmed) {
				if (!is_write) MarkReadbackHot(vaddr);
				tick = queued_state.tick;
				if (tick >= m_scheduler.CurrentTick()) m_scheduler.Flush();
				return;
			}
			if (ReadbackAgeEnabled()) [[unlikely]] {
				auto& master = m_scheduler.GetMasterSemaphore();
				master.Refresh();
				const uint64_t completed = master.KnownGpuTick();
				const uint64_t producer  = InflightWriteTick(vaddr, vaddr + size, completed);
				age = !GpuWriteLogComplete(completed)        ? ReadbackAgeStats::Unknown
				      : producer == 0                         ? ReadbackAgeStats::Done
				      : producer < m_scheduler.CurrentTick() ? ReadbackAgeStats::Submitted
				                                              : ReadbackAgeStats::Open;
			}
			if (m_readback_queue != nullptr) {
				queued = QueueReadback(vaddr, size, is_write);
				if (queued != nullptr) {
					if (!is_write) {
						MarkReadbackHot(vaddr);
					}
					tick = 0;
					return;
				}
			}
			if (!is_write) {
				MarkReadbackHot(vaddr);
			}
			RecordReadback(vaddr, size, is_write, origin);
			const auto current = m_memory_tracker.QueryReadback(vaddr, size);
			if (!current.gpu_dirty) {
				tick = 0;
				return;
			}
			EXIT_IF(current.unarmed);
			tick = current.tick;
			// A foreign thread may only wait on submitted ticks: nothing else is guaranteed
			// to submit the open command buffer while guest work waits on this thread.
			if (tick >= m_scheduler.CurrentTick()) {
				m_scheduler.Flush();
			}
		});
	}
	if (queued != nullptr) {
		DrainStats::WaitTimer wait(DrainStats::Kind::TickWait);
		const auto            start = ReadbackAgeEnabled() ? std::chrono::steady_clock::now()
		                                                   : std::chrono::steady_clock::time_point {};
		PublishQueuedReadback(*queued);
		if (ReadbackAgeEnabled()) [[unlikely]] {
			g_readback_age.Add(ReadbackAgeStats::Queued,
			                   std::chrono::duration<double, std::milli>(
			                       std::chrono::steady_clock::now() - start)
			                       .count());
		}
		return;
	}
	if (tick == 0) {
		return;
	}
	{
		DrainStats::WaitTimer wait(DrainStats::Kind::TickWait);
		const auto            start = ReadbackAgeEnabled() ? std::chrono::steady_clock::now()
		                                                   : std::chrono::steady_clock::time_point {};
		m_scheduler.GetMasterSemaphore().Wait(tick);
		if (ReadbackAgeEnabled()) [[unlikely]] {
			g_readback_age.Add(age, std::chrono::duration<double, std::milli>(
			                            std::chrono::steady_clock::now() - start)
			                            .count());
		}
	}
	if (TryPublishCompletedOrdinaryReadback(vaddr, size, tick)) return;
	// A completed native tick is not enough: its requested backing publication
	// must also have finished. Image aliases on the full tracker pages remain
	// conservative. A newer buffer writer during the image scan rejects the exit.
	if (ReadbackReadyAfterWaitEnabled() && vaddr != 0 && GuestRange {vaddr, size}.Valid() &&
	    !m_memory_tracker.QueryReadback(vaddr, size).gpu_dirty) {
		const auto page_begin = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
		const auto page_end = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
		const auto page_size = page_end - page_begin;
		if (GuestRange {page_begin, page_size}.Valid() &&
		    m_scheduler.Context().IsMapped(page_begin, page_size) &&
		    !m_texture_cache.HasPendingDownload(page_begin, page_size) &&
		    !m_texture_cache.IsRegionGpuModified(page_begin, page_size) &&
		    !m_memory_tracker.QueryReadback(vaddr, size).gpu_dirty) {
			return;
		}
	}
	m_scheduler.WaitPriorityOperations(tick);
}

void BufferCache::MarkReadbackHot(uint64_t vaddr) {
	const auto page = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
	const auto tick = m_scheduler.CurrentTick();
	if (auto it = m_hot_pages.find(page); it != m_hot_pages.end()) {
		it->second = tick;
		return;
	}
	if (m_hot_pages.size() >= HotReadbackPages) {
		// Replace the page whose latest fault is the oldest.
		const auto oldest = std::min_element(
		    m_hot_pages.begin(), m_hot_pages.end(),
		    [](const auto& a, const auto& b) { return a.second < b.second; });
		m_memory_tracker.SetReadbackHot(oldest->first, TRACKER_PAGE_SIZE, false);
		m_hot_pages.erase(oldest);
	}
	m_hot_pages.emplace(page, tick);
	m_memory_tracker.SetReadbackHot(page, TRACKER_PAGE_SIZE, true);
}

void BufferCache::QueueEagerReadback(uint64_t vaddr, uint64_t size) {
	const auto end = vaddr + size;
	for (const auto& [page, tick]: m_hot_pages) {
		(void)tick;
		if (page < end && page + TRACKER_PAGE_SIZE > vaddr) {
			m_eager_pending.push_back(page);
		}
	}
}

void BufferCache::OnCommandRecorded() {
	if (m_eager_pending.empty()) {
		return;
	}
	m_eager_ready.insert(m_eager_ready.end(), m_eager_pending.begin(), m_eager_pending.end());
	m_eager_pending.clear();
}

void BufferCache::RecordEagerReadbacks() {
	if (m_eager_ready.empty()) {
		return;
	}
	DrainStats::ReasonScope reason(DrainStats::Reason::EagerReadback);
	std::vector<uint64_t>   pages;
	pages.swap(m_eager_ready);
	std::sort(pages.begin(), pages.end());
	pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
	for (size_t index = 0; index < pages.size();) {
		const auto page = pages[index++];
		// The written buffer may have been retired since; its pages then have no GPU owner.
		if (!IsRegionRegistered(page, TRACKER_PAGE_SIZE)) {
			continue;
		}
		// Buffers are CACHING_PAGESIZE-aligned, so one buffer owns the whole tracker page.
		const auto id = FindBuffer(page, TRACKER_PAGE_SIZE);
		auto& buffer  = m_slot_buffers[id];
		uint64_t bytes = TRACKER_PAGE_SIZE;
		if (!IsBufferInvalid(id)) {
			// Only consecutive SELECTED pages in this complete live owner share a batch.
			// An unselected gap or another owner retains its own publication boundary.
			while (index < pages.size() && pages[index] == page + bytes) {
				const auto* next_owner = m_page_table.Find(pages[index] >> PageTable::kPageBits);
				if (next_owner == nullptr || !*next_owner || *next_owner != id ||
				    !buffer.IsInBounds(page, bytes + TRACKER_PAGE_SIZE)) {
					break;
				}
				bytes += TRACKER_PAGE_SIZE;
				++index;
			}
		}
		// The existing downloader collects every dirty byte of these selected pages,
		// allocates staging before arming the actual tick, and publishes its token.
		(void)DownloadBufferMemory(buffer, page, bytes);
	}
}

std::pair<vk::Buffer, uint64_t> BufferCache::PeekPage(uint64_t vaddr) {
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner == nullptr || !*owner) {
		return {};
	}
	auto&      buffer = m_slot_buffers[*owner];
	const auto page   = vaddr & ~(CACHING_PAGESIZE - 1u);
	if (buffer.is_deleted || !buffer.IsInBounds(page, CACHING_PAGESIZE)) {
		return {};
	}
	return {buffer.Handle(), page - buffer.CpuAddress()};
}

void BufferCache::DebugDescribePage(uint64_t vaddr) {
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner == nullptr || !*owner) {
		std::printf("bda-page: 0x%016llx has no cache buffer (table index %llu)\n",
		            static_cast<unsigned long long>(vaddr),
		            static_cast<unsigned long long>(PageIndex(vaddr)));
	} else {
		auto& buffer = m_slot_buffers[*owner];
		std::printf("bda-page: 0x%016llx owner cpu=0x%016llx size=0x%llx bda=0x%016llx table index "
		            "%llu (first %llu) deleted=%d\n",
		            static_cast<unsigned long long>(vaddr),
		            static_cast<unsigned long long>(buffer.CpuAddress()),
		            static_cast<unsigned long long>(buffer.Size()),
		            static_cast<unsigned long long>(buffer.BufferDeviceAddress()),
		            static_cast<unsigned long long>(PageIndex(vaddr)),
		            static_cast<unsigned long long>(PageIndex(buffer.CpuAddress())),
		            buffer.is_deleted ? 1 : 0);
	}
	std::fflush(stdout);
}

BufferId BufferCache::FindBuffer(uint64_t vaddr, uint64_t size) {
	if (vaddr == 0) {
		return NULL_BUFFER_ID;
	}
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid buffer discovery request\n");
	}
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner != nullptr && *owner) {
		auto& buffer = m_slot_buffers[*owner];
		if (buffer.IsInBounds(vaddr, size)) {
			return *owner;
		}
	}
	return CreateBuffer(vaddr, size);
}

BufferCache::OverlapResult BufferCache::ResolveOverlaps(uint64_t vaddr, uint64_t size) {
	static constexpr int      StreamLeapThreshold = 16;
	static constexpr uint64_t StreamLeapSize      = CACHING_PAGESIZE * 128;

	auto       begin      = vaddr;
	auto       end        = vaddr + size;
	const auto find_first = [&](uint64_t address) {
		auto first = m_buffers.lower_bound(address);
		if (first != m_buffers.begin()) {
			const auto  previous = std::prev(first);
			const auto& buffer   = m_slot_buffers[previous->second];
			if (buffer.CpuAddress() + buffer.Size() > address) {
				first = previous;
			}
		}
		return first;
	};
	auto first           = find_first(begin);
	auto last            = first;
	int  stream_score    = 0;
	bool has_stream_leap = false;
	for (; last != m_buffers.end() && last->first < end; ++last) {
		const auto& buffer        = m_slot_buffers[last->second];
		const auto  buffer_begin  = buffer.CpuAddress();
		const auto  buffer_end    = buffer_begin + buffer.Size();
		const bool  expands_left  = buffer_begin < begin;
		const bool  expands_right = buffer_end > end;
		begin                     = std::min(begin, buffer_begin);
		end                       = std::max(end, buffer_end);
		if (!has_stream_leap && (stream_score += buffer.StreamScore()) > StreamLeapThreshold) {
			has_stream_leap = true;
			// Reserve space in the incoming stream's direction of growth.
			// The old buffer extending left of the request predicts growth to the right, and vice versa.
			if (expands_left) {
				end += std::min(StreamLeapSize, (vaddr < LOWER_ADDRESS_SIZE ? LOWER_ADDRESS_SIZE
				                                       : LibKernel::Memory::kExtendedMemoryBase +
				                                             LibKernel::Memory::kExtendedMemorySize) - end);
			}
			if (expands_right) {
				const auto minimum = vaddr < LOWER_ADDRESS_SIZE
				                         ? CACHING_PAGESIZE * 2
				                         : LibKernel::Memory::kExtendedMemoryBase;
				if (begin > minimum) {
					begin -= std::min(StreamLeapSize, begin - minimum);
				}
				first = find_first(begin);
				begin = std::min(begin, first->first);
			}
		}
	}
	return {first, last, begin, end, has_stream_leap};
}

void BufferCache::JoinOverlap(BufferId new_id, BufferId overlap_id, bool accumulate_stream_score) {
	auto& new_buffer = m_slot_buffers[new_id];
	auto& overlap    = m_slot_buffers[overlap_id];
	if (accumulate_stream_score) {
		new_buffer.IncreaseStreamScore(overlap.StreamScore() + 1);
	}
	new_buffer.CopyFrom(m_scheduler.Current(), overlap, 0,
	                    overlap.CpuAddress() - new_buffer.CpuAddress(), overlap.Size());
	g_allocation_counters.game_buffers_joined.fetch_add(1, std::memory_order_relaxed);
	DeleteBuffer(overlap_id);
}

BufferId BufferCache::CreateBuffer(uint64_t vaddr, uint64_t size) {
	EXIT_IF(m_scheduler.Current().IsInvalid());
	const auto end = Common::AlignUp(vaddr + size, CACHING_PAGESIZE);
	vaddr = Common::AlignDown(vaddr, CACHING_PAGESIZE);
	size               = end - vaddr;
	const auto overlap = ResolveOverlaps(vaddr, size);
	g_allocation_counters.game_buffers_created.fetch_add(1, std::memory_order_relaxed);

	const auto id = m_slot_buffers.insert(
	    m_graphics, m_scheduler, MemoryUsage::DeviceLocal, overlap.begin,
	    AllFlags | vk::BufferUsageFlagBits::eShaderDeviceAddress, overlap.end - overlap.begin);
	const auto& buffer = m_slot_buffers[id];
	SetVulkanObjectNameF(m_graphics.device, buffer.Handle(),
	                     "Kyty.GameBuffer[guest=0x{:016x} size=0x{:x}]", overlap.begin,
	                     overlap.end - overlap.begin);
	t_last_create = {vaddr, size, overlap.begin, overlap.end, 0};
	for (auto it = overlap.first; it != overlap.last;) {
		const auto old_id = (it++)->second;
		JoinOverlap(id, old_id, !overlap.has_stream_leap);
		t_last_create.joined++;
	}
	Register(id);
	return id;
}


bool BufferCache::BdaStatsEnabled() {
	return SyncStats::Enabled();
}

void BufferCache::NoteBdaPass(uint64_t elapsed_ns) {
	auto& stats = t_sync_stats;
	stats.passes++;
	stats.pass_ns += elapsed_ns;
	const auto now = SyncStats::Now();
	if (stats.window_start == 0) {
		stats.window_start = now;
		return;
	}
	const auto window = now - stats.window_start;
	if (window < 5'000'000'000ull) {
		return;
	}
	const double seconds = static_cast<double>(window) / 1e9;
	const auto   per_s   = [&](uint64_t value) { return static_cast<double>(value) / seconds; };
	const auto   ms_s    = [&](uint64_t ns) { return static_cast<double>(ns) / 1e6 / seconds; };
	std::printf("bda-stats: passes/s=%.0f pass-ms/s=%.1f", per_s(stats.passes), ms_s(stats.pass_ns));
	for (int kind = 0; kind < 2; kind++) {
		std::printf(" | %s calls/s=%.0f uploads/s=%.0f copies/s=%.0f MB/s=%.1f walk-ms/s=%.1f "
		            "copy-ms/s=%.1f record-ms/s=%.1f breaks/s=%.0f",
		            kind == 0 ? "bda" : "other", per_s(stats.calls[kind]), per_s(stats.uploads[kind]),
		            per_s(stats.copies[kind]), per_s(stats.bytes[kind]) / 1e6,
		            ms_s(stats.walk_ns[kind]), ms_s(stats.copy_ns[kind]),
		            ms_s(stats.record_ns[kind]), per_s(stats.breaks[kind]));
	}
	const auto protect     = PageManager::TakeProtectStats();
	const auto invalidates = g_invalidate_calls.exchange(0, std::memory_order_relaxed);
	const auto invalid_ns  = g_invalidate_ns.exchange(0, std::memory_order_relaxed);
	std::printf(" | gpu-thread protects/s=%.0f protect-ms/s=%.1f | invalidates/s=%.0f "
	            "invalidate-ms/s=%.1f\n",
	            per_s(protect.calls), ms_s(protect.ns), per_s(invalidates), ms_s(invalid_ns));
	std::fflush(stdout);
	stats              = {};
	stats.window_start = now;
}

static bool DeferProtectsEnabled();

bool BufferCache::SynchronizeBuffer(Buffer& buffer, uint64_t vaddr, uint64_t size, bool is_written,
                                    bool is_texel_buffer) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::SynchronizeBuffer);
	std::vector<vk::BufferCopy> copies;
	uint64_t                    total_size = 0;
	vk::Buffer                  source;
	const bool     stats_on = SyncStats::Enabled();
	const int      kind     = t_upload_source == UploadSource::Bda ? 0 : 1;
	const uint64_t start    = stats_on ? SyncStats::Now() : 0;
	uint64_t       copy_ns  = 0;
	// A read-only upload leaves its write protections to the upload helper, which makes them
	// before it copies the pages (see PageManager::DeferProtects).
	const bool defer = !is_written && DeferProtectsEnabled() && m_staging_buffer.IsCoherent();
	PageManager::DeferProtects(defer);
	const auto pushes = defer ? PageManager::DeferredPushes() : 0;
	m_memory_tracker.ForEachUploadRange(
	    vaddr, size, is_written,
	    [&](uint64_t address, uint64_t bytes) noexcept {
		    copies.emplace_back(total_size, buffer.Offset(address), bytes);
		    total_size += bytes;
	    },
	    [&]() noexcept {
		    const uint64_t copy_start = stats_on ? SyncStats::Now() : 0;
		    source = UploadCopies(buffer, copies, total_size,
		                          defer && PageManager::DeferredPushes() != pushes);
		    if (stats_on) {
			    copy_ns = SyncStats::Now() - copy_start;
		    }
	    },
	    m_scheduler.Context().CurrentBdaEpoch());
	PageManager::DeferProtects(false);
	const uint64_t walked = stats_on ? SyncStats::Now() : 0;
	if (stats_on) {
		auto& stats = t_sync_stats;
		stats.calls[kind]++;
		stats.walk_ns[kind] += walked - start - copy_ns;
		stats.copy_ns[kind] += copy_ns;
		if (source) {
			stats.uploads[kind]++;
			stats.copies[kind] += copies.size();
			stats.bytes[kind] += total_size;
			stats.breaks[kind] += m_scheduler.Current().IsRendering() ? 1 : 0;
		}
	}
	if (source) {
		auto& command = m_scheduler.Current();
		if (!is_written && !is_texel_buffer && command.UploadBatchActive()) {
			ClampBufferCopies(copies, source == m_staging_buffer.Handle() ? m_staging_buffer.Size() : 0,
			                  buffer.Size(), "upload", buffer.CpuAddress());
			if (command.RecordBdaUploadCopy(source, buffer.Handle(), copies)) {
				if (stats_on) t_sync_stats.record_ns[kind] += SyncStats::Now() - walked;
				return false;
			}
		}
		command.EndRendering();
		const auto native = command.Handle();
		GpuZones::Mark(native, DrainStats::Zone::BufferCopy);
		vk::BufferMemoryBarrier before {};
		before.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite |
		                       vk::AccessFlagBits::eTransferRead |
		                       vk::AccessFlagBits::eTransferWrite;
		before.dstAccessMask       = vk::AccessFlagBits::eTransferWrite;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer              = buffer.Handle();
		before.offset              = 0;
		before.size                = buffer.Size();
		native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                       vk::PipelineStageFlagBits::eTransfer,
		                       vk::DependencyFlagBits::eByRegion, 0, nullptr, 1, &before, 0, nullptr);
		ClampBufferCopies(copies, source == m_staging_buffer.Handle() ? m_staging_buffer.Size() : 0,
		                  buffer.Size(), "upload", buffer.CpuAddress());
		if (!copies.empty()) {
			native.copyBuffer(source, buffer.Handle(), static_cast<uint32_t>(copies.size()),
			                  copies.data());
		}
		auto after          = before;
		after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		after.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		native.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                       vk::PipelineStageFlagBits::eAllCommands,
		                       vk::DependencyFlagBits::eByRegion, 0, nullptr, 1, &after, 0, nullptr);
		if (stats_on) {
			t_sync_stats.record_ns[kind] += SyncStats::Now() - walked;
		}
	}
	if (is_texel_buffer && !is_written) {
		return SynchronizeBufferFromImage(buffer, vaddr, size);
	}
	return false;
}

// Large uploads (the BDA synchronization copies about 600 MiB a second in ASTRO BOT's Sky Garden,
// a few hundred KiB at a time) are copied into the staging buffer by a helper thread while the GPU
// thread goes on recording; CommandScheduler::Submit waits for them (JoinAsyncUploads) before the
// commands that read the staging memory reach the GPU. The helper reads the backing, which cannot
// fault. KYTY_ASYNC_UPLOADS=0 copies on the GPU thread as before.
namespace {
// The helper thread runs outside the guest fault handling: memory the game unmapped after the
// upload was queued (seen in Ghost of Yotei after the calligraphy scene) must not be touched.
// Readable pages are copied, the rest of the range reads as zero.
void CopyReadableOrZero(uint64_t address, uint8_t* destination, uint64_t size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	uint64_t done = 0;
	while (done < size) {
		MEMORY_BASIC_INFORMATION info {};
		const auto current = address + done;
		if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) == 0) {
			std::memset(destination + done, 0, static_cast<size_t>(size - done));
			return;
		}
		const auto region_end = reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
		const auto chunk      = std::min(size - done, region_end - current);
		const bool readable =
		    info.State == MEM_COMMIT && (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0 &&
		    (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
		if (readable) {
			// The game can unmap the range between the query and the copy (Ghost of Yotei after the
			// first calligraphy name): ReadProcessMemory fails instead of faulting, and what it
			// could not read reads as zero.
			SIZE_T got = 0;
			if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(current),
			                      destination + done, static_cast<size_t>(chunk), &got) == 0 &&
			    got < chunk) {
				std::memset(destination + done + got, 0, static_cast<size_t>(chunk - got));
			}
		} else {
			std::memset(destination + done, 0, static_cast<size_t>(chunk));
		}
		done += chunk;
	}
#else
	std::memcpy(destination, reinterpret_cast<const void*>(address), size);
#endif
}

static inline void SanitizeBonePaletteStaging(uint8_t* destination, uint64_t dst_offset_in_buffer,
                                              uint64_t copy_size, uint64_t buffer_size) {
	// This game-specific data rewrite is an experiment, not emulated guest behavior. Keep it
	// disabled unless a developer explicitly opts in for an A/B diagnostic run.
	if (!BonePaletteStagingPolicy::Enabled(KYTY_GETENV("KYTY_GPU_PALETTE_SANITIZE"))) {
		return;
	}
	if (buffer_size != 124224 && copy_size != 124224) {
		return;
	}
	const uint32_t total_bones = 2588u;
	static const float s_identity_3x4[12] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	auto is_degenerate = [](const float* m) {
		float len0 = m[0]*m[0] + m[1]*m[1] + m[2]*m[2];
		float len1 = m[4]*m[4] + m[5]*m[5] + m[6]*m[6];
		float len2 = m[8]*m[8] + m[9]*m[9] + m[10]*m[10];
		return !(len0 > 1e-10f && len1 > 1e-10f && len2 > 1e-10f);
	};

	uint32_t restored = 0;
	for (uint32_t start_row = 1050u; start_row + 64u <= total_bones; start_row += 512u) {
		const uint32_t parent_bone = start_row - 1u;
		const uint64_t parent_byte = parent_bone * 48u;
		const void* fallback_matrix = s_identity_3x4;
		if (parent_byte >= dst_offset_in_buffer && parent_byte + 48u <= dst_offset_in_buffer + copy_size) {
			const float* parent_m = reinterpret_cast<const float*>(destination + (parent_byte - dst_offset_in_buffer));
			if (!is_degenerate(parent_m)) {
				fallback_matrix = parent_m;
			}
		}

		for (uint32_t r = start_row; r < start_row + 64u; ++r) {
			const uint64_t child_byte = r * 48u;
			if (child_byte >= dst_offset_in_buffer && child_byte + 48u <= dst_offset_in_buffer + copy_size) {
				float* child_m = reinterpret_cast<float*>(destination + (child_byte - dst_offset_in_buffer));
				if (is_degenerate(child_m)) {
					std::memcpy(child_m, fallback_matrix, 48u);
					restored++;
				}
			}
		}
	}

	if (restored > 0) {
		static uint32_t s_log_count = 0;
		if (s_log_count++ < 20) {
			std::printf("\x1b[92m[PALETTE-STAGING-RESTORE] Repaired %u degenerate/zero bone matrices (including leaf bones 2097 & 2098)\x1b[0m\n",
			            restored);
			std::fflush(stdout);
		}
	}
}

struct AsyncUploads {
	struct Item {
		uint64_t address;
		uint8_t* destination;
		uint64_t size;
		uint64_t dst_offset;
		uint64_t buffer_size;
	};
	std::mutex              mutex;
	std::condition_variable wake;
	std::vector<Item>       queue;
	std::atomic<uint64_t>   pending {0};
	std::atomic<bool>       hint {false};
	std::thread             thread;
	bool                    started = false;

	static bool Enabled() {
		static const bool enabled = [] {
			const char* text = KYTY_GETENV("KYTY_ASYNC_UPLOADS");
			return text == nullptr || std::strcmp(text, "0") != 0;
		}();
		return enabled;
	}

	void Push(std::span<const Item> items) {
		pending.fetch_add(items.size(), std::memory_order_acq_rel);
		{
			std::scoped_lock lock {mutex};
			queue.insert(queue.end(), items.begin(), items.end());
			if (!started) {
				started = true;
				thread  = std::thread([this] { Run(); });
				thread.detach();
			}
		}
		hint.store(true, std::memory_order_release);
		wake.notify_one();
	}

	void Run() {
		std::vector<Item> taken;
		for (;;) {
			// Spin briefly for the next upload: they come in bursts, a few per synchronization.
			for (uint32_t i = 0; i < 20000 && !hint.load(std::memory_order_acquire); i++) {
				_mm_pause();
			}
			{
				std::unique_lock lock {mutex};
				wake.wait(lock, [this] { return !queue.empty(); });
				taken.swap(queue);
				hint.store(false, std::memory_order_release);
			}
			// The protections of the pages first: a write landing before them is in the copy.
			PageManager::RunDeferredProtects();
			for (const auto& item: taken) {
				if (!Libs::LibKernel::Memory::TryReadBacking(item.address, item.destination,
				                                             item.size)) {
					CopyReadableOrZero(item.address, item.destination, item.size);
				}
				SanitizeBonePaletteStaging(item.destination, item.dst_offset, item.size, item.buffer_size);
				pending.fetch_sub(1, std::memory_order_acq_rel);
			}
			taken.clear();
		}
	}

	void Join() noexcept {
		for (uint32_t i = 1; pending.load(std::memory_order_acquire) != 0; i++) {
			_mm_pause();
			if (i % 4096 == 0) {
				std::this_thread::yield();
			}
		}
	}
};
AsyncUploads g_async_uploads;
} // namespace

// KYTY_DEFER_PROTECTS=0 protects pages on the GPU thread as before.
static bool DeferProtectsEnabled() {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_DEFER_PROTECTS");
		return (text == nullptr || std::strcmp(text, "0") != 0) && AsyncUploads::Enabled();
	}();
	return enabled;
}

void JoinAsyncUploads() noexcept {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::JoinAsyncUploads);
	g_async_uploads.Join();
}

// KYTY_DEBUG_HOST_IMPORT_TEST=1: once, tries to import 64 KiB of guest memory at `address` as the
// memory of a storage buffer (VK_EXT_external_memory_host), both as a host allocation and as
// mapped foreign memory, and prints what the driver answers: whether guest memory could back
// buffers directly instead of being copied.
static void DebugHostImportTest(GraphicContext& graphics, uint64_t address) {
#if defined(_WIN32)
	static bool done = KYTY_GETENV("KYTY_DEBUG_HOST_IMPORT_TEST") == nullptr;
	if (done) {
		return;
	}
	done = true;
	constexpr uint64_t Size = 64 * 1024;
	// Pass 0: guest memory as it is. Pass 1: the same after making it writable. Pass 2: private
	// memory of the emulator, to see whether the driver imports anything at all.
	for (int pass = 0; pass < 3; pass++) {
	auto* pointer = reinterpret_cast<void*>(address & ~uint64_t {0xfff});
	if (pass == 1) {
		DWORD old = 0;
		(void)VirtualProtect(pointer, Size, PAGE_READWRITE, &old);
	} else if (pass == 2) {
		pointer = VirtualAlloc(nullptr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	}
	MEMORY_BASIC_INFORMATION info {};
	(void)VirtualQuery(pointer, &info, sizeof(info));
	std::printf("host-import: pass=%d protect=0x%lx type=0x%lx state=0x%lx\n", pass, info.Protect,
	            info.Type, info.State);
	// Variants: with and without a device address, and each memory type the driver offers.
	for (int variant = 0; variant < 4; variant++) {
		const auto type           = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT;
		const bool device_address = (variant & 1) == 0;
		const bool second_type    = (variant & 2) != 0;
		vk::MemoryHostPointerPropertiesEXT properties {};
		const auto queried =
		    graphics.device.getMemoryHostPointerPropertiesEXT(type, pointer, &properties);
		vk::ExternalMemoryBufferCreateInfo external {};
		external.handleTypes = type;
		vk::BufferCreateInfo create {};
		create.pNext = &external;
		create.size  = Size;
		create.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
		               vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;
		if (device_address) {
			create.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
		}
		graphics.ShareBuffer(create);
		vk::Buffer buffer;
		const auto created = graphics.device.createBuffer(&create, nullptr, &buffer);
		vk::Result allocated = vk::Result::eErrorUnknown;
		vk::Result bound     = vk::Result::eErrorUnknown;
		uint32_t   required  = 0;
		int        index     = -1;
		if (created == vk::Result::eSuccess) {
			const auto requirements = graphics.device.getBufferMemoryRequirements(buffer);
			required                = requirements.memoryTypeBits;
			const auto usable       = requirements.memoryTypeBits & properties.memoryTypeBits;
			if (queried == vk::Result::eSuccess && usable != 0) {
				index = std::countr_zero(usable);
				if (second_type && (usable & (usable - 1u)) != 0) {
					index = std::countr_zero(usable & (usable - 1u));
				}
				vk::ImportMemoryHostPointerInfoEXT import {};
				import.handleType   = type;
				import.pHostPointer = pointer;
				vk::MemoryAllocateFlagsInfo flags {};
				flags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
				flags.pNext = &import;
				vk::MemoryAllocateInfo allocate {};
				allocate.pNext = device_address ? static_cast<const void*>(&flags)
				                                : static_cast<const void*>(&import);
				allocate.allocationSize  = Size;
				allocate.memoryTypeIndex = static_cast<uint32_t>(index);
				vk::DeviceMemory memory;
				allocated = graphics.device.allocateMemory(&allocate, nullptr, &memory);
				if (allocated == vk::Result::eSuccess) {
					bound = graphics.device.bindBufferMemory(buffer, memory, 0);
				}
			}
		}
		std::printf("host-import: variant=%d address=0x%016" PRIx64 " type=%s query=%s types=0x%x buffer=%s "
		            "required=0x%x index=%d allocate=%s bind=%s\n",
		            variant, reinterpret_cast<uint64_t>(pointer), vk::to_string(type).c_str(),
		            vk::to_string(queried).c_str(), properties.memoryTypeBits,
		            vk::to_string(created).c_str(), required, index, vk::to_string(allocated).c_str(),
		            vk::to_string(bound).c_str());
		std::fflush(stdout);
	}
	}
#else
	(void)graphics;
	(void)address;
#endif
}

vk::Buffer BufferCache::UploadCopies(Buffer& buffer, std::span<vk::BufferCopy> copies,
                                     uint64_t total_size, bool deferred) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::UploadCopies);
	DebugHostImportTest(m_graphics, buffer.CpuAddress());
	if (copies.empty()) {
		if (deferred) {
			// No copy will run them: a queued protection must not wait for some later upload.
			PageManager::RunDeferredProtects();
		}
		return nullptr;
	}
	RecordUpload(t_upload_source, buffer.CpuAddress() + copies.front().dstOffset, total_size);

	auto [mapped, base_offset] = m_staging_buffer.Map(total_size, 4);
	constexpr uint64_t AsyncUploadMinimum = 64 * 1024;
	if (mapped != nullptr && (total_size >= AsyncUploadMinimum || deferred) &&
	    m_staging_buffer.IsCoherent() && AsyncUploads::Enabled()) {
		thread_local std::vector<AsyncUploads::Item> items;
		items.clear();
		for (auto& copy: copies) {
			items.push_back({buffer.CpuAddress() + copy.dstOffset, mapped + copy.srcOffset, copy.size, copy.dstOffset, buffer.Size()});
			copy.srcOffset += base_offset;
		}
		g_async_uploads.Push(items);
		m_staging_buffer.Commit();
		return m_staging_buffer.Handle();
	}
	if (deferred) {
		PageManager::RunDeferredProtects();
	}
	if (mapped != nullptr) {
		// Copying on several threads measured no faster (A/B 2026-09-28): the staging memory's
		// write bandwidth, not one thread's latency, bounds it.
		DrawPhaseTimer::ProbeScope probe(g_draw_phases, DrawPhaseTimer::UploadCopy);
		// A range the game unmapped since the tracker saw it, or a PRT aperture page that was never
		// committed (Yotei, 0x50d3400000 on the first start): read as zero instead of faulting
		// outside the guest fault handler.
		for (auto& copy: copies) {
			const auto address = buffer.CpuAddress() + copy.dstOffset;
			CopyReadableOrZero(address, mapped + copy.srcOffset, copy.size);
			SanitizeBonePaletteStaging(mapped + copy.srcOffset, copy.dstOffset, copy.size, buffer.Size());
			copy.srcOffset += base_offset;
		}
		m_staging_buffer.Commit();
		return m_staging_buffer.Handle();
	}

	auto temporary = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Upload, 0,
	                                         vk::BufferUsageFlagBits::eTransferSrc, total_size);
	for (const auto& copy: copies) {
		const auto address = buffer.CpuAddress() + copy.dstOffset;
		CopyReadableOrZero(address, temporary->Mapped().data() + copy.srcOffset, copy.size);
		SanitizeBonePaletteStaging(temporary->Mapped().data() + copy.srcOffset, copy.dstOffset, copy.size, buffer.Size());
	}
	temporary->Flush(0, total_size);
	const auto handle = temporary->Handle();
	m_scheduler.DeferOperation([owner = std::move(temporary)]() mutable { owner.reset(); });
	return handle;
}

// KYTY_DEBUG_AB=streamhost: 'off' windows copy into a second ring in device memory. The tiler keeps
// the ring it got at startup.
StreamBuffer& BufferCache::ActiveStream() noexcept {
	return m_stream_device != nullptr && AbFeatureOff() ? *m_stream_device : m_stream_buffer;
}

// KYTY_DEBUG_STREAM_REUSE=0 copies every stream range every time; KYTY_DEBUG_AB=streamreuse
// alternates.
static bool StreamReuseEnabled() {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_DEBUG_STREAM_REUSE");
		return text == nullptr || std::strcmp(text, "0") != 0;
	}();
	static const bool ab = AbSelected("streamreuse");
	return enabled && !(ab && AbFeatureOff());
}

// With KYTY_VERIFY_STREAM_REUSE=1 a copy that would be bound again is compared with the guest
// memory it came from, and the range is copied again as without reuse. It reports how often the
// bytes changed within the epoch: the draws that reuse would have shown older CPU writes.
bool BufferCache::VerifyStreamReuse(const StreamCopy& copy) {
	static const bool verify = KYTY_GETENV("KYTY_VERIFY_STREAM_REUSE") != nullptr;
	if (!verify) {
		return false;
	}
	thread_local std::vector<uint8_t> bytes;
	bytes.resize(copy.size);
	const bool  read = Libs::LibKernel::Memory::TryReadBacking(copy.vaddr, bytes.data(), copy.size);
	const auto* kept = copy.stream->Mapped().data() + copy.offset;
	static std::atomic<uint64_t> checked {0};
	static std::atomic<uint64_t> changed {0};
	const auto count = checked.fetch_add(1, std::memory_order_relaxed) + 1;
	if ((!read || std::memcmp(bytes.data(), kept, copy.size) != 0) &&
	    changed.fetch_add(1, std::memory_order_relaxed) < 16) {
		std::printf("stream-reuse verify: 0x%016" PRIx64 " size 0x%" PRIx64
		            " changed within the epoch\n",
		            copy.vaddr, copy.size);
	}
	if (count % 100000 == 0) {
		std::printf("stream-reuse verify: reuses=%" PRIu64 " changed=%" PRIu64 "\n", count,
		            changed.load(std::memory_order_relaxed));
		std::fflush(stdout);
	}
	return true;
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBuffer(uint64_t vaddr, uint64_t size,
                                                       bool is_written, bool is_texel_buffer,
                                                       BufferId id) {
	if (m_scheduler.Current().IsInvalid() || !GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: buffer request requires a recording command buffer\n");
	}
	if (is_written) {
		// Publication can replace a partial owner. Run before retaining its
		// slot reference and before the caller records its buffer overwrite.
		m_texture_cache.PrepareBlockBufferWrite(vaddr, size);
	}
	return ObtainBufferImpl(vaddr, size, is_written, is_texel_buffer, id);
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBufferForImagePublication(uint64_t vaddr,
                                                                      uint64_t size) {
	return ObtainBufferImpl(vaddr, size, true, false, {}, true);
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBufferImpl(uint64_t vaddr, uint64_t size,
                                                          bool is_written, bool is_texel_buffer,
                                                          BufferId id, bool image_publication) {
	auto& command = m_scheduler.Current();
	if (command.IsInvalid() || !GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: buffer request requires a recording command buffer\n");
	}

	// Draws bind most buffers read-only and unchanged since their last upload: the lock-free
	// summary then answers what the locked tracker passes below would find (nothing to copy).
	const bool cpu_clean = !is_written && m_memory_tracker.IsRegionCpuCleanHint(vaddr, size);
	// Draws of one BDA epoch need not see CPU writes made during it (see
	// RenderContext::PrepareBda), so a range this epoch already copied into the stream ring is
	// bound again rather than copied again (Sky Garden binds most such ranges several times an
	// epoch): while the copy's command buffer is current and no buffer or image became
	// GPU-written since. KYTY_DEBUG_STREAM_REUSE=0 copies every time.
	StreamCopy* copy_slot = nullptr;
	uint64_t    epoch     = 0;
	if (!is_written && !cpu_clean && size <= CACHING_PAGESIZE && StreamReuseEnabled()) {
		epoch     = m_scheduler.Context().CurrentBdaEpoch();
		copy_slot = &m_stream_copies[((vaddr >> 4u) ^ (vaddr >> 16u) ^ size) % StreamCopySlots];
		const auto& copy = *copy_slot;
		if (epoch != 0 && copy.epoch == epoch && copy.vaddr == vaddr && copy.size == size &&
		    copy.tick == m_scheduler.CurrentTick() && copy.gpu_writes == m_gpu_dirty_generation &&
		    copy.image_writes == m_texture_cache.GpuModifiedGeneration() &&
		    copy.stream == &ActiveStream()) {
			if (!VerifyStreamReuse(copy)) {
				return {copy.stream, copy.offset};
			}
		}
	}
	if (!is_written && !cpu_clean && size <= CACHING_PAGESIZE &&
	    m_memory_tracker.IsRegionOnlyCpuModified(vaddr, size)) {
		const auto alignment = std::max<uint64_t>(
		    m_graphics.physical_device_properties.limits.minUniformBufferOffsetAlignment, 1);
		DrawPhaseTimer::ProbeScope probe(g_draw_phases, DrawPhaseTimer::StreamCopy);
		auto& stream          = ActiveStream();
		auto [mapped, offset] = stream.Map(size, alignment, false);
		if (mapped != nullptr) {
			// No GPU-written bytes in the range (IsRegionOnlyCpuModified), so no read protection
			// either: a plain copy from guest memory, without the backing-store lookup.
			std::memcpy(mapped, reinterpret_cast<const void*>(vaddr), size);
			stream.Commit();
			RecordUpload(UploadSource::Stream, vaddr, size);
			if (copy_slot != nullptr && epoch != 0) {
				*copy_slot = {.vaddr        = vaddr,
				              .size         = size,
				              .epoch        = epoch,
				              .tick         = m_scheduler.CurrentTick(),
				              .gpu_writes   = m_gpu_dirty_generation,
				              .image_writes = m_texture_cache.GpuModifiedGeneration(),
				              .stream       = &stream,
				              .offset       = offset};
			}
			return {&stream, offset};
		}
	}

	if (IsBufferInvalid(id) || !m_slot_buffers[id].IsInBounds(vaddr, size)) {
		id = FindBuffer(vaddr, size);
	}
	auto& buffer = m_slot_buffers[id];
	TouchBuffer(buffer);
	if (!cpu_clean) {
		(void)SynchronizeBuffer(buffer, vaddr, size, is_written, is_texel_buffer);
	} else if (is_texel_buffer) {
		// SynchronizeBuffer's image half: the CPU upload has nothing to copy.
		(void)SynchronizeBufferFromImage(buffer, vaddr, size);
	}
	if (is_written) {
		if (m_gpu_modified_ranges.Add(vaddr, size)) {
			m_gpu_dirty_generation++;
		}
		m_texture_cache.OnBufferGpuWrite(vaddr, size);
		if (m_readback_queue != nullptr || ReadbackAgeEnabled()) [[unlikely]] {
			NoteGpuWrite(vaddr, size);
		}
		if (!m_hot_pages.empty()) {
			QueueEagerReadback(vaddr, size);
		}
		if (DrainStats::Enabled()) {
			DrainStats::RecordGpuWrite(vaddr, size);
			if (image_publication ? m_texture_cache.IsKnownDccMetadataLocked(vaddr, size)
			                      : m_texture_cache.IsKnownDccMetadata(vaddr, size)) {
				DrainStats::Record(DrainStats::Kind::DccMetaWrite, size);
			}
		}
	}
	return {&buffer, buffer.Offset(vaddr)};
}

// Streamed textures (Sky Garden: 2048x2048 and 4096x4096 BC textures, 5-22 MB) are written by
// the CPU a part at a time, and each draw that finds one changed re-uploads it whole, several
// times a second, though only ~1-6% of its bytes changed (KYTY_DEBUG_UPLOADS, 2026-09-28). From
// the second staged upload of a range within two seconds on, a buffer holds the range instead:
// the memory tracker then copies only the pages written since, and the image uploads from the
// buffer. KYTY_DEBUG_AB=imagebuf stages every upload in alternate windows.
bool BufferCache::IsRepeatedImageStage(uint64_t vaddr, uint64_t size) {
	static constexpr uint64_t MinSize = 1024 * 1024;
	static constexpr auto     Window  = std::chrono::seconds(2);
	static const bool         ab      = AbSelected("imagebuf");
	if (size < MinSize || (ab && AbFeatureOff())) {
		return false;
	}
	const auto now   = std::chrono::steady_clock::now();
	auto&      stage = m_image_stages[(vaddr >> 16u) % m_image_stages.size()];
	const bool hit   = stage.vaddr == vaddr && stage.size == size && now - stage.time <= Window;
	stage            = {.vaddr = vaddr, .size = size, .time = now};
	return hit;
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBufferForImage(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid image source\n");
	}
	const UploadSourceScope upload_source(UploadSource::Image);
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner != nullptr && *owner) {
		auto& buffer = m_slot_buffers[*owner];
		if (buffer.IsInBounds(vaddr, size)) {
			TouchBuffer(buffer);
			(void)SynchronizeBuffer(buffer, vaddr, size, false, false);
			return {&buffer, buffer.Offset(vaddr)};
		}
	}
	if (IsRegionGpuModified(vaddr, size) || IsRepeatedImageStage(vaddr, size)) {
		return ObtainBuffer(vaddr, size, false, false);
	}

	auto [staging, stage_offset] = m_staging_buffer.Map(size, 16);
	if (staging == nullptr) {
		EXIT("BufferCache: failed to map staging for guest image backing\n");
	}
	if (!Libs::LibKernel::Memory::TryReadSparseBacking(vaddr, staging, size)) {
		// Part of the range is unmapped (Ghost of Yotei: a texture whose layout reaches past its
		// allocation). Read the mapped 4 KiB pages and leave the rest zero.
		static constexpr uint64_t Page = 0x1000;
		auto* bytes = static_cast<uint8_t*>(staging);
		std::memset(bytes, 0, size);
		uint64_t missing = 0;
		for (uint64_t done = 0; done < size;) {
			const auto count = std::min(size - done, Page - ((vaddr + done) & (Page - 1u)));
			if (!Libs::LibKernel::Memory::TryReadSparseBacking(vaddr + done, bytes + done, count)) {
				std::memset(bytes + done, 0, count);
				missing += count;
			}
			done += count;
		}
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 8u) {
			std::printf("Warning: image source 0x%016" PRIx64 " size=0x%" PRIx64
			            " is partly unmapped (0x%" PRIx64 " bytes read as zero)\n",
			            vaddr, size, missing);
		}
	}
	m_staging_buffer.Commit();
	RecordUpload(UploadSource::Image, vaddr, size);
	RecordImageChunks(vaddr, size);
	return {&m_staging_buffer, stage_offset};
}

void BufferCache::FillBuffer(uint64_t vaddr, uint64_t size, uint32_t value, bool is_gds) {
	if ((vaddr & 3u) != 0 || size == 0 || (size & 3u) != 0 || size > UINT64_MAX - vaddr) {
		EXIT("BufferCache: fill range must be dword aligned\n");
	}
	if (is_gds) {
		if (vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - vaddr) {
			EXIT("BufferCache: GDS fill range is out of bounds\n");
		}
		m_gds_buffer.Fill(vaddr, size, value);
		return;
	}
	if (vaddr == 0) {
		EXIT("BufferCache: invalid fill memory address\n");
	}
	(void)m_texture_cache.ClearMeta(vaddr);
	if (!IsRegionGpuModified(vaddr, size)) {
		// Access the guest mapping so write faults invalidate cached buffers and images. A page
		// that is already CPU-dirty does not fault: start a BDA epoch either way.
		auto* destination = reinterpret_cast<uint32_t*>(vaddr);
		std::fill(destination, destination + size / sizeof(uint32_t), value);
		m_scheduler.Context().AdvanceBdaEpochForGpuWrite();
		return;
	}

	m_texture_cache.InvalidateMemoryFromGPU(vaddr, size, TextureCache::GpuWriteSource::Fill);
	auto [dst, dst_offset] = ObtainBuffer(vaddr, size, true, true);
	dst->Fill(dst_offset, size, value);
	OnCommandRecorded();
}

void BufferCache::WriteOrderedMemory(uint64_t vaddr, std::span<const uint8_t> bytes) {
	if (bytes.empty() || !GuestRange {vaddr, bytes.size()}.Valid() ||
	    ((vaddr | bytes.size()) & 3u) != 0) {
		EXIT("BufferCache: invalid ordered memory write\n");
	}
	if (LabelCausalTrace::Enabled()) {
		uint64_t causal_value = 0;
		if (bytes.size() <= sizeof(causal_value)) std::memcpy(&causal_value, bytes.data(), bytes.size());
		LabelCausalTrace::Record(LabelCausalTrace::Stage::OrderedBegin,
		    reinterpret_cast<uint64_t>(&m_scheduler), m_scheduler.CurrentTick(), 0,
		    vaddr, bytes.size(), causal_value);
	}
	m_texture_cache.InvalidateMemoryFromGPU(vaddr, bytes.size(), TextureCache::GpuWriteSource::Copy);
	auto [destination, offset] = ObtainBuffer(vaddr, bytes.size(), true, true);
	(void)offset;
	// The staging copy includes an ALL_COMMANDS execution dependency before the transfer,
	// then makes the stored bytes visible to subsequent commands on this queue.
	WriteDataBuffer(*destination, vaddr, bytes.data(), bytes.size());
	OnCommandRecorded();
	// Readbacks publish through Kernel backing rather than a protected guest VA. Their
	// owner/token checks keep a later GPU write dirty until its own publication completes.
	EXIT_IF(!DownloadBufferMemory(*destination, vaddr, bytes.size(), bytes));
}

void BufferCache::CopyBuffer(uint64_t dst_vaddr, uint64_t src_vaddr, uint64_t size, bool dst_gds,
                             bool src_gds) {
	const bool dst_memory = !dst_gds;
	const bool src_memory = !src_gds;
	if ((dst_memory && dst_vaddr == 0) || (src_memory && src_vaddr == 0) || size == 0 ||
	    ((dst_gds || src_gds) && ((dst_vaddr | src_vaddr | size) & 3u) != 0) ||
	    size > UINT64_MAX - dst_vaddr || size > UINT64_MAX - src_vaddr || (dst_gds && src_gds) ||
	    (dst_gds && (dst_vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - dst_vaddr)) ||
	    (src_gds && (src_vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - src_vaddr))) {
		EXIT("BufferCache: invalid copy range, src=0x%016" PRIx64 " dst=0x%016" PRIx64
		     " size=0x%016" PRIx64 " src_gds=%d dst_gds=%d\n",
		     src_vaddr, dst_vaddr, size, static_cast<int>(src_gds), static_cast<int>(dst_gds));
	}
	if (src_memory && dst_memory && !IsRegionGpuModified(dst_vaddr, size) &&
	    !IsRegionGpuModified(src_vaddr, size) && !m_texture_cache.FindImageFromRange(src_vaddr, size)) {
		std::memcpy(reinterpret_cast<void*>(dst_vaddr), reinterpret_cast<const void*>(src_vaddr),
		            size);
		m_scheduler.Context().AdvanceBdaEpochForGpuWrite(); // As FillBuffer.
		return;
	}

	auto& command = m_scheduler.Current();
	if (dst_memory) {
		m_texture_cache.InvalidateMemoryFromGPU(dst_vaddr, size, TextureCache::GpuWriteSource::Copy);
	}
	const auto src_id      = src_memory ? FindBuffer(src_vaddr, size) : BufferId {};
	const auto dst_id      = dst_memory ? FindBuffer(dst_vaddr, size) : BufferId {};
	auto [src, src_offset] = src_memory ? ObtainBuffer(src_vaddr, size, false, true, src_id)
	                                    : std::pair {&m_gds_buffer, src_vaddr};
	auto [dst, dst_offset] = dst_memory ? ObtainBuffer(dst_vaddr, size, true, true, dst_id)
	                                    : std::pair {&m_gds_buffer, dst_vaddr};
	dst->CopyFrom(command, *src, src_offset, dst_offset, size);
	OnCommandRecorded();
}

bool BufferCache::IsRegionRegistered(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid registered-region query\n");
	}
	// Cached buffers are ordered and non-overlapping. The last buffer beginning before the query
	// end is therefore the only possible intersection.
	const auto candidate = m_buffers.lower_bound(vaddr + size);
	if (candidate == m_buffers.begin()) {
		return false;
	}
	const auto& [address, id] = *std::prev(candidate);
	return address + m_slot_buffers[id].Size() > vaddr;
}

bool BufferCache::IsRegionGpuModified(uint64_t vaddr, uint64_t size) {
	return m_memory_tracker.IsRegionGpuModified(vaddr, size);
}

bool BufferCache::HasGpuDirtyBytes(uint64_t vaddr, uint64_t size) {
	// Downloaded bytes have left the byte set, but their backing is valid only once published.
	// Other bytes of an armed page are valid already: a window download around a CPU fault
	// arms whole pages, and Thread_Gpu reads game tables that share such pages every frame.
	// Strict SRT reads ask this for the same small tables every draw: remember clean pages.
	// KYTY_DEBUG_AB=cleanpages asks the byte sets every time in every other window.
	static const bool ab       = AbSelected("cleanpages");
	const auto        page     = vaddr / TRACKER_PAGE_SIZE;
	const bool        one_page = (vaddr + size - 1) / TRACKER_PAGE_SIZE == page &&
	                      !(ab && AbFeatureOff());
	auto& slot = m_clean_pages[page % m_clean_pages.size()];
	if (one_page && slot.page == page && slot.generation == m_gpu_dirty_generation) {
		return false;
	}
	if (one_page) {
		// Taken before the scan, as TextureCache::IsRegionGpuModified does.
		const auto generation = m_gpu_dirty_generation;
		const auto begin      = page * TRACKER_PAGE_SIZE;
		if (!m_gpu_modified_ranges.Intersects(begin, TRACKER_PAGE_SIZE) &&
		    !InFlightIntersects(begin, TRACKER_PAGE_SIZE)) {
			slot = {page, generation};
			return false;
		}
	}
	return m_gpu_modified_ranges.Intersects(vaddr, size) || InFlightIntersects(vaddr, size);
}

void BufferCache::PruneInFlight() {
	while (!m_inflight_downloads.empty() &&
	       m_scheduler.IsPublished(m_inflight_downloads.front().tick)) {
		m_inflight_downloads.pop_front();
	}
}

bool BufferCache::InFlightIntersects(uint64_t vaddr, uint64_t size) {
	PruneInFlight();
	const auto end = vaddr + size;
	for (const auto& download: m_inflight_downloads) {
		for (const auto& [address, bytes]: download.ranges) {
			if (address < end && address + bytes > vaddr) {
				return true;
			}
		}
	}
	return false;
}

bool BufferCache::IsRegionCpuModified(uint64_t vaddr, uint64_t size) {
	return m_memory_tracker.IsRegionCpuModified(vaddr, size);
}

void BufferCache::TickFrame() {
	++m_gc_tick;
	ProbePageTable();
}

void BufferCache::SetPageTableProbe(uint64_t entry, uint64_t expected) {
	if (KYTY_GETENV("KYTY_DEBUG_DEVICE_FAULT") == nullptr ||
	    (entry + 1) * sizeof(vk::DeviceAddress) > m_bda_pagetable_buffer.Size()) {
		return;
	}
	m_probe_entry    = entry;
	m_probe_expected = expected;
	std::printf("pagetable-probe: entry %llu expected 0x%016llx, read back once per frame\n",
	            static_cast<unsigned long long>(entry), static_cast<unsigned long long>(expected));
	std::fflush(stdout);
}

void BufferCache::ProbePageTable() {
	if (m_probe_entry == UINT64_MAX) [[likely]] {
		return;
	}
	if (m_probe_buffer == nullptr) {
		m_probe_buffer = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Download, 0,
		                                          AllFlags,
		                                          sizeof(uint64_t) * m_probe_ticks.size());
	}
	for (size_t slot = 0; slot < m_probe_ticks.size(); slot++) {
		auto& tick = m_probe_ticks[slot];
		if (tick == 0 || !m_scheduler.IsFree(tick)) {
			continue;
		}
		m_probe_buffer->Invalidate(slot * sizeof(uint64_t), sizeof(uint64_t));
		uint64_t value = 0;
		std::memcpy(&value, m_probe_buffer->Mapped().data() + slot * sizeof(uint64_t), sizeof(value));
		m_probe_checked++;
		if (value != m_probe_expected) {
			m_probe_mismatches++;
			if (m_probe_mismatches <= 64) {
				std::printf("pagetable-probe: MISMATCH entry %llu = 0x%016llx, expected 0x%016llx "
				            "(written at tick %llu, frame %llu)\n",
				            static_cast<unsigned long long>(m_probe_entry),
				            static_cast<unsigned long long>(value),
				            static_cast<unsigned long long>(m_probe_expected),
				            static_cast<unsigned long long>(tick),
				            static_cast<unsigned long long>(m_probe_frames));
				std::fflush(stdout);
			}
		}
		tick = 0;
	}
	const auto slot = m_probe_frames % m_probe_ticks.size();
	if (m_probe_ticks[slot] == 0) {
		m_probe_buffer->CopyFrom(m_scheduler.Current(), m_bda_pagetable_buffer,
		                         m_probe_entry * sizeof(vk::DeviceAddress), slot * sizeof(uint64_t),
		                         sizeof(uint64_t), vk::AccessFlagBits::eMemoryWrite,
		                         vk::AccessFlagBits::eHostRead,
		                         vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
		                         vk::AccessFlagBits::eHostRead);
		m_probe_ticks[slot] = m_scheduler.CurrentTick();
	}
	m_probe_frames++;
	if ((m_probe_frames % 600) == 0) {
		std::printf("pagetable-probe: %llu frames, %llu readbacks checked, %llu mismatches\n",
		            static_cast<unsigned long long>(m_probe_frames),
		            static_cast<unsigned long long>(m_probe_checked),
		            static_cast<unsigned long long>(m_probe_mismatches));
		std::fflush(stdout);
	}
}

void BufferCache::RunGarbageCollector() {
	// Ages are counted in presented frames. Collection runs after every submission, often dozens
	// per frame, so counting submissions would retire buffers the game reuses every few frames.
	const auto tick = m_gc_tick;
	if (m_graphics.CanReportMemoryUsage()) {
		m_total_used_memory = m_graphics.GetDeviceMemoryUsage();
	}
	if (m_total_used_memory < m_trigger_gc_memory) {
		return;
	}
	DrainStats::ReasonScope reason(DrainStats::Reason::BufferGc);

	const bool     aggressive = m_total_used_memory >= m_critical_gc_memory;
	// Ages count presented frames (TickFrame): the former submission ages divided by 8.
	const uint64_t age        = std::min<uint64_t>(aggressive ? 10 : 20, tick);
	const size_t   limit      = aggressive ? 64 : 32;

	std::vector<BufferId> dirty_buffers;
	size_t                retire_count = 0;
	m_lru_cache.ForEachItemBelow(tick - age, [&](BufferId id) {
		auto& buffer = m_slot_buffers[id];
		EXIT_IF(buffer.is_deleted);
		m_memory_tracker.ValidateGpuDirtyOwnership(m_gpu_modified_ranges, buffer.CpuAddress(),
		                                           buffer.Size(), "garbage collection");
		const bool dirty = m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size());
		if (dirty && !aggressive) {
			return false;
		}
		if (dirty) {
			// Pages armed by an in-flight readback publish with the wait below.
			EXIT_IF(!DownloadBufferMemory(buffer, buffer.CpuAddress(), buffer.Size()) &&
			        !m_memory_tracker.HasArmedPages(buffer.CpuAddress(), buffer.Size()));
			dirty_buffers.push_back(id);
		} else {
			m_memory_tracker.UntrackMemory(buffer.CpuAddress(), buffer.Size());
			g_allocation_counters.game_buffers_collected.fetch_add(1, std::memory_order_relaxed);
			DeleteBuffer(id);
		}
		return ++retire_count == limit;
	});
	if (dirty_buffers.empty()) {
		return;
	}

	// Publish all queued downloads before releasing their tracked pages and owners.
	const auto completion_tick = m_scheduler.CurrentTick();
	m_scheduler.Wait(completion_tick);
	m_scheduler.WaitPriorityOperations(completion_tick);
	for (const auto id: dirty_buffers) {
		auto& buffer = m_slot_buffers[id];
		m_memory_tracker.UnmarkRegionAsGpuModified(buffer.CpuAddress(), buffer.Size());
		if (m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size()) ||
		    m_gpu_modified_ranges.Intersects(buffer.CpuAddress(), buffer.Size())) {
			EXIT("BufferCache: garbage collection retained GPU ownership\n");
		}
		m_memory_tracker.UntrackMemory(buffer.CpuAddress(), buffer.Size());
		g_allocation_counters.game_buffers_collected.fetch_add(1, std::memory_order_relaxed);
		// As DeleteBuffer: the page-table clear Unregister records runs with the open command
		// buffer, and a draw or dispatch prepared around this collection may already hold the
		// buffer's device address, so the buffer itself goes once that buffer's tick completes.
		// Erasing it here freed memory a recorded shader still read (Ghost of Yotei: device lost
		// with ReadInvalid at a freed buffer's address, the same instruction in every run).
		DeleteBuffer(id);
	}
}

void BufferCache::ProcessFaultBuffer() {
	m_fault_manager.ProcessFaultBuffer();
}

void BufferCache::SynchronizeBuffersInRange(uint64_t vaddr, uint64_t size) {
	const auto end = vaddr + size;
	auto       it  = m_buffers.upper_bound(vaddr);
	if (it != m_buffers.begin()) {
		--it;
	}
	for (; it != m_buffers.end() && it->first < end; ++it) {
		auto&      buffer = m_slot_buffers[it->second];
		const auto start  = std::max(buffer.CpuAddress(), vaddr);
		const auto finish = std::min(buffer.CpuAddress() + buffer.Size(), end);
		if (start < finish) {
			(void)SynchronizeBuffer(buffer, start, finish - start, false, false);
		}
	}
}

void BufferCache::PublishBdaHints(uint64_t vaddr, uint64_t size) noexcept {
	m_memory_tracker.PublishBdaHints(vaddr, size);
}

// The zero page and its table entry, before the first draw or dispatch that uses the table: the
// first PrepareBda always synchronizes (its epoch starts behind), through this or the selective
// pass. The scheduler is not recording yet when the cache is constructed.
void BufferCache::EnsureBdaZeroPage() {
	if (m_bda_zero_page_written) {
		return;
	}
	m_bda_zero_page_written = true;
	m_bda_zero_page.Fill(0, m_bda_zero_page.Size(), 0);
	const vk::DeviceAddress address = m_bda_zero_page.BufferDeviceAddress();
	EXIT_IF(address == 0);
	std::printf("bda-zero-page: device=0x%016llx size=0x%llx (table entry %u)\n",
	            static_cast<unsigned long long>(address),
	            static_cast<unsigned long long>(m_bda_zero_page.Size()),
	            static_cast<unsigned>(BDA_ZERO_PAGE_ENTRY));
	WriteDataBuffer(m_bda_pagetable_buffer, BDA_ZERO_PAGE_ENTRY * sizeof(vk::DeviceAddress), &address,
	                sizeof(address));
	SetPageTableProbe(BDA_ZERO_PAGE_ENTRY, address);
}

namespace {
bool BdaUploadBatchEnabled() {
	// Native cost is reduced, but a useful game throughput gain is still unproved.
	// Keep the experiment opt-in for same-executable comparisons.
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_BDA_UPLOAD_BATCH");
		return text != nullptr && std::strcmp(text, "1") == 0;
	}();
	return enabled;
}
class BdaUploadBatchScope {
public:
	explicit BdaUploadBatchScope(CommandBuffer& command): m_command(command), m_enabled(BdaUploadBatchEnabled()) {
		if (m_enabled) m_command.BeginUploadBatch();
	}
	~BdaUploadBatchScope() { if (m_enabled) m_command.EndUploadBatch(); }
	BdaUploadBatchScope(const BdaUploadBatchScope&) = delete;
	BdaUploadBatchScope& operator=(const BdaUploadBatchScope&) = delete;
private:
	CommandBuffer& m_command;
	bool m_enabled;
};
} // namespace

void BufferCache::SynchronizeBdaLegacy(const RangeSet& mapped) {
	const UploadSourceScope upload_source(UploadSource::Bda);
	EnsureBdaZeroPage();
	const BdaUploadBatchScope upload_batch(m_scheduler.Current());
	// Clear before the full walk so writes racing with the walk stay pending. Summaries go
	// first: a hint published in between re-sets its summary bit for the next pass.
	for (size_t summary = 0; summary < MemoryTracker::BDA_SUMMARY_WORDS; ++summary) {
		(void)m_memory_tracker.ConsumeBdaSummaryWord(summary);
	}
	for (size_t word = 0; word < MemoryTracker::BDA_HINT_WORDS; ++word) {
		(void)m_memory_tracker.ConsumeBdaHintWord(word);
	}
	mapped.ForEach(
	    [this](uint64_t begin, uint64_t end) { SynchronizeBuffersInRange(begin, end - begin); });
}

bool BufferCache::SynchronizeBdaSelective(const RangeSet& mapped) {
	const UploadSourceScope upload_source(UploadSource::Bda);
	EnsureBdaZeroPage();
	const BdaUploadBatchScope upload_batch(m_scheduler.Current());
	for (size_t summary = 0; summary < MemoryTracker::BDA_SUMMARY_WORDS; ++summary) {
		uint64_t words = m_memory_tracker.ConsumeBdaSummaryWord(summary);
		// On failure, words not yet visited keep their hint bits; restore their summary bits.
		struct SummaryClaim {
			MemoryTracker& tracker;
			size_t         summary;
			uint64_t&      remaining;
			~SummaryClaim() { tracker.RestoreBdaSummary(summary, remaining); }
		} summary_claim {m_memory_tracker, summary, words};
		while (words != 0) {
			const auto word = summary * 64 + static_cast<size_t>(std::countr_zero(words));
			if (!SynchronizeBdaWord(word, mapped)) {
				return false;
			}
			words &= words - 1;
		}
	}
	return true;
}

bool BufferCache::SynchronizeBdaWord(size_t word, const RangeSet& mapped) {
	uint64_t bits = m_memory_tracker.ConsumeBdaHintWord(word);
	struct Claim {
		MemoryTracker& tracker;
		size_t         word;
		uint64_t&      remaining;
		~Claim() { tracker.RestoreBdaHints(word, remaining); }
	} claim {m_memory_tracker, word, bits};
	while (bits != 0) {
		const auto region = word * 64 + static_cast<size_t>(std::countr_zero(bits));
		if (!SynchronizeBdaRegion(region, mapped)) {
			return false;
		}
		// Do not touch the shared word again: a concurrent write may have republished it.
		bits &= bits - 1;
	}
	return true;
}

bool BufferCache::SynchronizeBdaRegion(uint64_t region, const RangeSet& mapped) {
	const auto region_begin = region * TRACKER_REGION_SIZE;
	auto*      manager      = m_memory_tracker.FindRegion(region);
	if (manager == nullptr) {
		// A mapping or registration can precede tracker creation. The current consumer must
		// see those bytes now; the full walk creates (or waits for) the initially dirty manager.
		mapped.ForEachInRange(region_begin, TRACKER_REGION_SIZE,
		                      [this](uint64_t begin, uint64_t end) {
			                      SynchronizeBuffersInRange(begin, end - begin);
		                      });
		return true;
	}
	const auto dirty = m_memory_tracker.SnapshotCpuDirty(*manager);
	if (dirty.None()) {
		return true;
	}
	bool consistent = true;
	mapped.ForEachInRange(region_begin, TRACKER_REGION_SIZE, [&](uint64_t begin, uint64_t end) {
		if (consistent) {
			consistent = SynchronizeDirtyOwners(dirty, region_begin, begin, end);
		}
	});
	return consistent;
}

static bool BdaRunSyncEnabled() {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_BDA_RUN_SYNC");
		return text == nullptr || std::strcmp(text, "0") != 0;
	}();
	return enabled;
}

bool BufferCache::SynchronizeDirtyOwners(const RegionBits& dirty, uint64_t region_begin,
                                         uint64_t begin, uint64_t end) {
	uint64_t   synced_end = begin;
	auto       page       = static_cast<size_t>((begin - region_begin) / TRACKER_PAGE_SIZE);
	const auto limit =
	    static_cast<size_t>((end - region_begin + TRACKER_PAGE_SIZE - 1) / TRACKER_PAGE_SIZE);
	while (page < limit) {
		const auto [first, last] = dirty.FirstRangeFrom(page);
		if (first >= limit) {
			break;
		}
		const auto run_end = std::min(region_begin + last * TRACKER_PAGE_SIZE, end);
		for (auto cursor = std::max(region_begin + first * TRACKER_PAGE_SIZE, synced_end);
		     cursor < run_end;) {
			const auto* owner = m_page_table.Find(cursor >> CACHING_PAGEBITS);
			if (owner == nullptr || !*owner) {
				cursor = Common::AlignDown(cursor, CACHING_PAGESIZE) + CACHING_PAGESIZE;
				continue;
			}
			auto* buffer = m_slot_buffers.try_get(*owner);
			if (buffer == nullptr || buffer->is_deleted || !buffer->IsInBounds(cursor, 1)) {
				return false;
			}
			// Only the dirty run: the rest of the owner had no dirty page in the snapshot, and a
			// write since publishes the region's hint again. Walking the whole owner (as before;
			// KYTY_BDA_RUN_SYNC=0) cost Sky Garden ~18 us per synchronized owner.
			const auto owner_begin = BdaRunSyncEnabled() ? cursor
			                                             : std::max(buffer->CpuAddress(), begin);
			const auto owner_end   = std::min(buffer->CpuAddress() + buffer->Size(),
                                            BdaRunSyncEnabled() ? run_end : end);
			(void)SynchronizeBuffer(*buffer, owner_begin, owner_end - owner_begin, false, false);
			synced_end = owner_end;
			cursor     = owner_end;
		}
		page = last;
	}
	return true;
}

bool BufferCache::CheckBdaHintInvariant(const RangeSet& mapped) {
	bool covered = true;
	for (const auto& [address, id]: m_buffers) {
		const auto& buffer = m_slot_buffers[id];
		mapped.ForEachInRange(address, buffer.Size(), [&](uint64_t begin, uint64_t end) {
			covered = covered && m_memory_tracker.BdaHintsCoverCpuDirty(begin, end - begin);
		});
		if (!covered) {
			break;
		}
	}
	return covered;
}

} // namespace Libs::Graphics
