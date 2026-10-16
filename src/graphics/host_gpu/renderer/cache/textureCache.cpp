#include "graphics/host_gpu/renderer/functionProfile.h"

#include "graphics/host_gpu/renderer/cache/textureCache.h"

#include "common/alignment.h"
#include "common/assert.h"
#include "graphics/host_gpu/renderer/labelCausalTrace.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/switches.h"
#include "graphics/guest_gpu/gpu_format.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/guest_gpu/tile.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/drainStats.h"
#include "graphics/host_gpu/renderer/gpuZones.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/image/textureCommon.h"
#include "graphics/host_gpu/renderer/image/tiler.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "kernel/memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vulkan/vulkan_format_traits.hpp>

namespace Libs::Graphics {

namespace {

constexpr uint64_t NumFramesBeforeRemoval = 32;

// KYTY_GPU_ZONES: describe an image the first time it is refreshed for each cause, so the guest
// addresses that key its zone rows can be matched to a size, format, and writer.
// KYTY_GPU_ZONES: the binding whose lookup is refreshing an image, for the refresh log.
thread_local const char* t_refresh_via = "other";

class RefreshVia final {
public:
	explicit RefreshVia(const char* via) noexcept: m_previous(t_refresh_via) { t_refresh_via = via; }
	~RefreshVia() { t_refresh_via = m_previous; }
	RefreshVia(const RefreshVia&)            = delete;
	RefreshVia& operator=(const RefreshVia&) = delete;

private:
	const char* m_previous;
};

void LogZoneRefresh(const Image& image) {
	static std::mutex                   mutex;
	static std::unordered_set<uint64_t> seen;
	const bool     buffer_modified = image.IsBufferModified();
	const auto&    info            = image.info;
	const uint64_t key             = info.data.address ^ (buffer_modified ? 1u : 0u) ^
	                     (static_cast<uint64_t>(t_refresh_via[0]) << 56u);
	{
		std::scoped_lock lock {mutex};
		if (!seen.insert(key).second) {
			return;
		}
	}
	std::printf("gpu-zones: refresh image 0x%016" PRIx64 " size=0x%" PRIx64
	            " %ux%ux%u guest_format=%u tile=%u levels=%u layers=%u cause=%s via=%s\n",
	            info.data.address, info.data.size, info.extent.width, info.extent.height,
	            info.extent.depth, static_cast<uint32_t>(info.guest_format),
	            static_cast<uint32_t>(info.tile_mode), info.resources.levels, info.resources.layers,
	            buffer_modified ? "gpu-buffer-write" : "cpu-write", t_refresh_via);
}

// KYTY_GPU_ZONES: the first time each kind of GPU buffer write lands on an image, print it with
// how much of the image it covers and the PM4 packet that caused it.
void LogZoneBufferWrite(const Image& image, uint64_t address, uint64_t size,
                        TextureCache::GpuWriteSource source) {
	static std::mutex                   mutex;
	static std::unordered_set<uint64_t> seen;
	const auto&                         info = image.info;
	const uint64_t key = info.data.address ^ ((static_cast<uint64_t>(source) + 1u) << 60u);
	{
		std::scoped_lock lock {mutex};
		if (!seen.insert(key).second) {
			return;
		}
	}
	const char* kind = source == TextureCache::GpuWriteSource::Fill   ? "fill"
	                   : source == TextureCache::GpuWriteSource::Copy ? "copy"
	                                                                  : "shader-store";
	const bool  whole = address <= info.data.address &&
	                   address + size >= info.data.address + info.data.size;
	std::printf("gpu-zones: buffer %s on image 0x%016" PRIx64 " %ux%u guest_format=%u tile=%u "
	            "write=0x%016" PRIx64 "+0x%" PRIx64 " %s pm4=0x%x image_gpu_modified=%d\n",
	            kind, info.data.address, info.extent.width, info.extent.height,
	            static_cast<uint32_t>(info.guest_format), static_cast<uint32_t>(info.tile_mode),
	            address, size, whole ? "whole-image" : "partial", DrainStats::t_pm4_op,
	            image.IsGpuModified() ? 1 : 0);
}

[[nodiscard]] bool DecodeColorClear(const TextureCache::ImageDesc& desc, uint8_t code,
                                  vk::ClearColorValue& clear) {
	const auto& metadata = desc.info.metadata;
	const auto  format   = desc.view_info.format;
	const bool  cmask    = metadata.kind == ImageMetadataKind::Cmask;
	if (cmask ? code == 0 : code == 0x20) {
		// Register clears belong to the color buffer; the texture pipe cannot decode them.
		return desc.type == TextureCache::BindingType::RenderTarget &&
		       metadata.clear_register_valid &&
		       (DecodePackedColorClear(format, metadata.clear_word, clear) ||
		        DecodePackedColorClear64(format, metadata.clear_word, metadata.clear_word_hi, clear));
	}
	if (cmask) {
		return false;
	}
	switch (code) {
		case 0x00:
		case 0x40:
		case 0x80:
		case 0xc0: break;
		default: return false;
	}
	clear = {};
	if (code == 0x00) {
		return true;
	}
	switch (format) {
		case vk::Format::eR8Unorm:
		case vk::Format::eR8G8Unorm:
		case vk::Format::eR8G8B8A8Unorm:
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eB8G8R8A8Unorm:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eA2B10G10R10UnormPack32:
		case vk::Format::eA2R10G10B10UnormPack32:
		case vk::Format::eR5G6B5UnormPack16:
		case vk::Format::eA1R5G5B5UnormPack16:
		case vk::Format::eR4G4B4A4UnormPack16:
		case vk::Format::eR16Unorm:
		case vk::Format::eR16G16Unorm:
		case vk::Format::eR16G16B16A16Unorm:
		case vk::Format::eR16Sfloat:
		case vk::Format::eR16G16Sfloat:
		case vk::Format::eR16G16B16A16Sfloat:
		case vk::Format::eR32Sfloat:
		case vk::Format::eR32G32Sfloat:
		case vk::Format::eR32G32B32A32Sfloat:
		case vk::Format::eB10G11R11UfloatPack32: break;
		default: return false;
	}
	const float          rgb   = (code & 0x80u) != 0 ? 1.0f : 0.0f;
	const float          alpha = (code & 0x40u) != 0 ? 1.0f : 0.0f;
	std::array<float, 4> channels {rgb, rgb, rgb, alpha};
	if (!metadata.dcc_alpha_msb) {
		std::swap(channels[0], channels[3]);
	}
	// DCC clear decoding clamps missing lanes before applying the format swizzle.
	const auto components = vk::componentCount(format);
	if (components == 1) {
		channels[0] = channels[3];
	} else if (components == 2) {
		channels[1] = channels[3];
	}
	switch (format) {
		case vk::Format::eB8G8R8A8Unorm:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eA2R10G10B10UnormPack32:
		case vk::Format::eA1R5G5B5UnormPack16:
		case vk::Format::eR5G6B5UnormPack16: std::swap(channels[0], channels[2]); break;
		case vk::Format::eR4G4B4A4UnormPack16:
			std::reverse(channels.begin(), channels.end());
			break;
		default: break;
	}
	clear.float32 = channels;
	return true;
}

[[nodiscard]] std::vector<vk::BufferImageCopy> BuildDepthCopies(const ImageInfo& info,
                                                              uint64_t slice_stride,
                                                              vk::ImageAspectFlags aspect) {
	std::vector<vk::BufferImageCopy> copies(info.resources.layers);
	for (uint32_t layer = 0; layer < info.resources.layers; ++layer) {
		auto& copy             = copies[layer];
		copy.bufferOffset      = slice_stride * layer;
		copy.bufferRowLength   = info.pitch;
		copy.bufferImageHeight = info.extent.height;
		copy.imageSubresource  = {aspect, 0, layer, 1};
		copy.imageExtent       = {info.extent.width, info.extent.height, 1};
	}
	return copies;
}

[[nodiscard]] std::vector<GpuTileInfo> BuildDepthTiles(const ImageInfo& info) {
	TileBlockLayout block {};
	EXIT_NOT_IMPLEMENTED(
	    !TileGetBlockLayout(TileBlockFamily::Depth64KB, info.bytes_per_block, block));
	const auto               full_slice_size = info.data.size / info.resources.layers;
	std::vector<GpuTileInfo> tiles;
	tiles.reserve(info.resources.layers);
	for (uint32_t layer = 0; layer < info.resources.layers; ++layer) {
		const auto offset = full_slice_size * layer;
		tiles.push_back({block.family, block.bytes_per_element, offset, full_slice_size, offset,
		                 full_slice_size, 0, info.extent.width, info.extent.height, 1, info.pitch});
		tiles.back().surface_z = layer;
	}
	return tiles;
}

} // namespace

TextureCache::TextureCache(GraphicContext& graphics, CommandScheduler& scheduler,
                           PageManager& page_manager, BufferCache& buffer_cache)
    : m_graphics(graphics), m_scheduler(scheduler), m_page_manager(page_manager),
      m_blit_helper(graphics, scheduler),
      m_tiler(graphics, scheduler, buffer_cache.GetUtilityBuffer(MemoryUsage::Stream)),
      m_buffer_cache(buffer_cache), m_dcc_resolver(graphics, scheduler), m_htile_writer(graphics),
      m_readback_linear_images(Config::ReadbackLinearImagesEnabled()) {
	m_dcc_gpu_clear = Config::DccGpuClearEnabled() && m_dcc_resolver.Available();
	if (m_graphics.CanReportMemoryUsage()) {
		constexpr int64_t GiB = 1024ll * 1024 * 1024;
		const auto        budget =
		    static_cast<int64_t>(std::min<uint64_t>(m_graphics.GetTotalMemoryBudget(), INT64_MAX));
		const auto threshold = std::min<int64_t>(budget, 8 * GiB);
		m_pressure_gc_memory = static_cast<uint64_t>(
		    std::max<int64_t>(std::min(budget - 6 * threshold / 10, budget - GiB), GiB + GiB / 2));
		m_critical_gc_memory = static_cast<uint64_t>(
		    std::max<int64_t>(std::min(budget - 2 * threshold / 10, budget - GiB / 2), 3 * GiB));
		m_trigger_gc_memory = static_cast<uint64_t>(std::max<int64_t>((budget - threshold) / 2, 0));
	}
}

TextureCache::~TextureCache() {
	m_slot_images.ForEach([&](ImageId id, const Image& image) {
		if (image.registered) {
			UnregisterImage(id);
		}
	});
}

bool TextureCache::SameBacking(const ImageInfo& cached, const ImageInfo& requested,
                               bool exact_format) {
	if (cached.data.address != requested.data.address) {
		return false;
	}
	if (cached.data.size != requested.data.size) {
		return false;
	}
	if (cached.extent != requested.extent) {
		return false;
	}
	if (cached.resources.levels < requested.resources.levels ||
	    cached.resources.layers < requested.resources.layers) {
		return false;
	}
	if (cached.samples != requested.samples) {
		return false;
	}
	if (cached.bytes_per_block != requested.bytes_per_block) {
		return false;
	}
	if (cached.tile_mode != requested.tile_mode) {
		return false;
	}
	if (cached.GetColorTransform() != requested.GetColorTransform()) {
		return false;
	}
	if (!ImageViewOps::FormatsCompatible(cached.pixel_format, requested.pixel_format) ||
	    cached.type != requested.type) {
		return false;
	}
	if (exact_format && cached.pixel_format != requested.pixel_format) {
		return false;
	}
	return true;
}

TextureCache::BindingType TextureCache::UploadBinding(const Image& image) {
	if (image.info.IsDepth()) {
		if (image.info.tile_mode == Prospero::TileMode::kDepth ||
		    image.info.tile_mode == Prospero::TileMode::kLinear) {
			return BindingType::DepthTarget;
		}
		return BindingType::Texture;
	}
	if (image.usage.render_target) {
		return BindingType::RenderTarget;
	}
	if (image.usage.video_out) {
		return BindingType::VideoOut;
	}
	return image.usage.storage ? BindingType::Storage : BindingType::Texture;
}

// Debugging aid: KYTY_DEBUG_IMAGE_CHURN=1 prints every image the cache creates or frees, with the
// path that did it (ChurnReason), to find images recreated in steady state.
namespace {
thread_local const char* t_churn_reason = "other";
struct ChurnReason {
	explicit ChurnReason(const char* reason): previous(t_churn_reason) { t_churn_reason = reason; }
	~ChurnReason() { t_churn_reason = previous; }
	ChurnReason(const ChurnReason&)            = delete;
	ChurnReason& operator=(const ChurnReason&) = delete;
	const char*  previous;
};
void LogImageChurn(const char* what, const Image& image) {
	static const bool enabled = KYTY_GETENV("KYTY_DEBUG_IMAGE_CHURN") != nullptr;
	if (!enabled) {
		return;
	}
	const auto& info = image.info;
	std::printf("image-churn: %s %s 0x%016" PRIx64 " size=0x%" PRIx64
	            " %ux%ux%u fmt=%u guest=%u type=%u tile=%u mips=%u layers=%u samples=%u"
	            " usage=%s%s%s%s\n",
	            what, t_churn_reason, info.data.address, info.data.size, info.extent.width,
	            info.extent.height, info.extent.depth, static_cast<uint32_t>(info.pixel_format),
	            static_cast<uint32_t>(info.guest_format), static_cast<uint32_t>(info.type),
	            static_cast<uint32_t>(info.tile_mode), info.resources.levels, info.resources.layers,
	            info.samples, image.usage.texture ? "T" : "", image.usage.render_target ? "R" : "",
	            image.usage.depth_target ? "D" : "", image.usage.storage ? "S" : "");
}
} // namespace


// The last images created and freed, for the device-fault report (DescribeRecentImages). The
// memory block comes from VMA: an image's device address is not known, but a fault at a block
// offset can be matched against the block handle and the sizes.
struct RecentImageEvent {
	bool        freed        = false;
	uint64_t    tick         = 0;
	uint64_t    address      = 0;
	uint64_t    size         = 0;
	uint32_t    width        = 0;
	uint32_t    height       = 0;
	uint32_t    layers       = 0;
	uint32_t    levels       = 0;
	uint32_t    format       = 0;
	uint64_t    image        = 0;
	uint64_t    memory       = 0;
	uint64_t    memory_offset = 0;
	uint64_t    memory_size  = 0;
	uint32_t    usage        = 0;
};
constexpr size_t RecentImageRing = 96;
static std::mutex                                  g_recent_images_mutex;
static std::array<RecentImageEvent, RecentImageRing> g_recent_images {};
static uint64_t                                    g_recent_images_count = 0;

uint64_t RecentImageCreatedTick(uint64_t vk_image) {
	std::scoped_lock lock {g_recent_images_mutex};
	const auto       kept = std::min<uint64_t>(g_recent_images_count, RecentImageRing);
	for (uint64_t back = 1; back <= kept; ++back) {
		const auto& event = g_recent_images[(g_recent_images_count - back) % RecentImageRing];
		if (event.image == vk_image && !event.freed) {
			return event.tick;
		}
	}
	return 0;
}

void NoteRecentImage(const GraphicContext& graphics, const Image& image, bool freed, uint64_t tick) {
	RecentImageEvent event {};
	event.freed   = freed;
	event.tick    = tick;
	event.address = image.info.data.address;
	event.size    = image.info.data.size;
	event.width   = image.info.extent.width;
	event.height  = image.info.extent.height;
	event.layers  = image.info.resources.layers;
	event.levels  = image.info.resources.levels;
	event.format  = static_cast<uint32_t>(image.info.pixel_format);
	event.image   = reinterpret_cast<uint64_t>(static_cast<VkImage>(image.backing.image));
	event.usage   = (image.usage.texture ? 1u : 0u) | (image.usage.render_target ? 2u : 0u) |
	              (image.usage.depth_target ? 4u : 0u) | (image.usage.storage ? 8u : 0u);
	if (image.backing.allocation != nullptr && graphics.allocator != nullptr) {
		VmaAllocationInfo info {};
		vmaGetAllocationInfo(graphics.allocator, image.backing.allocation, &info);
		event.memory        = reinterpret_cast<uint64_t>(info.deviceMemory);
		event.memory_offset = info.offset;
		event.memory_size   = info.size;
	}
	std::scoped_lock lock {g_recent_images_mutex};
	g_recent_images[g_recent_images_count % RecentImageRing] = event;
	g_recent_images_count++;
}

void TextureCache::DescribeRecentImages() const {
	std::scoped_lock lock {g_recent_images_mutex};
	const auto count = std::min<uint64_t>(g_recent_images_count, RecentImageRing);
	std::printf("device-fault-images: last %llu of %llu image events (oldest first), gpu tick %llu\n",
	            static_cast<unsigned long long>(count),
	            static_cast<unsigned long long>(g_recent_images_count),
	            static_cast<unsigned long long>(m_scheduler.GetMasterSemaphore().KnownGpuTick()));
	for (uint64_t n = g_recent_images_count - count; n < g_recent_images_count; n++) {
		const auto& e = g_recent_images[n % RecentImageRing];
		std::printf("device-fault-image: %s tick=%llu guest=0x%016llx size=0x%llx %ux%u layers=%u levels=%u "
		            "vkformat=%u usage=%c%c%c%c image=0x%llx memory=0x%llx+0x%llx (block 0x%llx)\n",
		            e.freed ? "free" : "new ", static_cast<unsigned long long>(e.tick),
		            static_cast<unsigned long long>(e.address), static_cast<unsigned long long>(e.size),
		            e.width, e.height, e.layers, e.levels, e.format, (e.usage & 1u) ? 'T' : '-',
		            (e.usage & 2u) ? 'R' : '-', (e.usage & 4u) ? 'D' : '-', (e.usage & 8u) ? 'S' : '-',
		            static_cast<unsigned long long>(e.image), static_cast<unsigned long long>(e.memory),
		            static_cast<unsigned long long>(e.memory_offset),
		            static_cast<unsigned long long>(e.memory_size));
	}
	std::fflush(stdout);
}

ImageId TextureCache::InsertImage(const ImageInfo& info) {
	const auto id = m_slot_images.insert(m_graphics, m_scheduler, info);
	LogImageChurn("new", m_slot_images[id]);
	NoteRecentImage(m_graphics, m_slot_images[id], false, m_scheduler.CurrentTick());
	if (!info.data.Empty()) {
		RegisterImage(id);
	}
	return id;
}

// Callers hold m_lock, so there is one writer at a time. The regions are stamped first and the
// new generation published after them: a reader that sees a generation also sees every stamp up
// to it (RangeGeneration's memo relies on this).
void TextureCache::AdvanceImageSetGeneration(GuestRange range) noexcept {
	const auto stamp = m_image_set_generation.load(std::memory_order_relaxed) + 1;
	const auto first = range.address >> RegionGenerationBits;
	const auto last  = (range.End() - 1) >> RegionGenerationBits;
	for (auto region = first; region <= last; region++) {
		m_region_generations[region % RegionGenerationBuckets].store(stamp,
		                                                            std::memory_order_relaxed);
	}
	m_image_set_generation.store(stamp, std::memory_order_release);
}

// KYTY_DEBUG_AB=regiongen uses the whole image set's generation for every range in every other
// window. The two agree on what they call unchanged: the whole set's generation is at least every
// range's, and equals a range's only when that range saw the newest change.
uint64_t TextureCache::RangeGeneration(GuestRange range) const noexcept {
	static const bool ab    = AbSelected("regiongen");
	const auto        whole = m_image_set_generation.load(std::memory_order_acquire);
	if ((ab && AbFeatureOff()) || range.size == 0) {
		return whole;
	}
	// Draws ask about the same ranges draw after draw, and while the whole set's generation is
	// unchanged no range's is: the answer is kept with the generation it was computed under. A
	// render target spans many regions, and their buckets are scattered over the table.
	// KYTY_DEBUG_AB=rangememo computes every answer in every other window.
	struct Memo {
		const TextureCache* cache      = nullptr;
		uint64_t            address    = 0;
		uint64_t            size       = 0;
		uint64_t            whole      = 0;
		uint64_t            generation = 0;
	};
	thread_local std::array<Memo, 1024> memos {};
	static const bool memo_ab = AbSelected("rangememo");
	auto& memo = memos[((range.address >> 12u) ^ (range.address >> 24u) ^ range.size) % memos.size()];
	if (memo.cache == this && memo.whole == whole && memo.address == range.address &&
	    memo.size == range.size && !(memo_ab && AbFeatureOff())) {
		return memo.generation;
	}
	uint64_t   generation = m_image_set_base;
	const auto first      = range.address >> RegionGenerationBits;
	const auto last       = (range.End() - 1) >> RegionGenerationBits;
	for (auto region = first; region <= last; region++) {
		generation = std::max(generation, m_region_generations[region % RegionGenerationBuckets].load(
		                                      std::memory_order_relaxed));
	}
	memo = {this, range.address, range.size, whole, generation};
	return generation;
}

void TextureCache::RegisterImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (image.registered || image.info.data.Empty()) {
		EXIT("TextureCache: invalid image registration\n");
	}
	ImagePageTable::PageRange pages {};
	if (!ImagePageTable::TryGetPageRange(image.info.data.address, image.info.data.size, pages)) {
		EXIT("TextureCache: image registration is outside the guest address space\n");
	}
	ForEachPage(image.info.data.address, image.info.data.size, [this, id](uint64_t page) {
		m_image_page_table[page].push_back(id);
	});
	AdvanceImageSetGeneration(image.info.data);
	image.registered = true;
	image.lru_id     = m_lru_cache.Insert(id, m_gc_tick);
	image.lru_tick   = m_gc_tick;
	m_total_used_memory += image.AccountedSize();
}

void TextureCache::UnregisterImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (!image.registered) {
		return;
	}
	if (image.bindless_pinned) {
		image.bindless_pinned = false;
		if (on_bindless_unregister) {
			on_bindless_unregister(id);
		}
	}
	UntrackImage(id);
	ImagePageTable::PageRange pages {};
	if (!ImagePageTable::TryGetPageRange(image.info.data.address, image.info.data.size, pages)) {
		EXIT("TextureCache: registered image is outside the guest address space\n");
	}
	ForEachPage(image.info.data.address, image.info.data.size, [this, id](uint64_t page) {
		auto* owners = m_image_page_table.Find(page);
		if (owners == nullptr || !owners->Erase(id)) {
			EXIT("TextureCache: image missing from page owner index\n");
		}
	});
	AdvanceImageSetGeneration(image.info.data);
	m_lru_cache.Free(image.lru_id);
	const auto accounted = image.AccountedSize();
	if (accounted > m_total_used_memory) {
		EXIT("TextureCache: image accounting underflow\n");
	}
	m_total_used_memory -= accounted;
	image.registered = false;
	BumpImageCleanEpoch();
}

void TextureCache::DeleteImage(ImageId id, std::vector<ImageId>* retired) {
	auto* image = m_slot_images.try_get(id);
	if (image == nullptr || !image->registered) {
		return;
	}
	NoteRecentImage(m_graphics, *image, true, m_scheduler.CurrentTick());
	if (!image->depth_id) {
		std::vector<ImageId> associations;
		m_slot_images.ForEach([&](ImageId candidate, const Image& associated) {
			if (associated.depth_id == id) {
				associations.push_back(candidate);
			}
		});
		for (const auto association: associations) {
			FreeImage(association, retired);
		}
	}
	if (image->IsGpuModified()) {
		EXIT("TextureCache: deleting a GPU-modified image without resolving its contents\n");
	}
	m_download_images.erase(id);
	if (image->info.HasMetadata()) {
		const auto metadata = m_surface_metas.find(image->info.metadata.range.address);
		if (metadata != m_surface_metas.end() &&
		    image->info.metadata.kind == ImageMetadataKind::Htile &&
		    metadata->second.type == MetaDataInfo::Type::HTile) {
			// A later binding may have reused this address for another metadata type.
			m_surface_metas.erase(metadata);
			m_surface_meta_generation.fetch_add(1, std::memory_order_release);
		}
	}
	UnregisterImage(id);
	if (retired != nullptr) {
		retired->push_back(id);
	} else if (m_scheduler.Active()) {
		// Deferred twice, like the tiler's scratch buffers: a descriptor write or copy for this
		// image resolved before the deletion can still be recorded after a ring wrap submitted
		// and completed the current tick; its views must outlive that recording too
		// ("deferred-call: stale image view descriptor").
		m_scheduler.DeferOperation([this, id] {
			m_scheduler.DeferOperation([this, id] { m_slot_images.erase(id); });
		});
	} else {
		m_slot_images.erase(id);
	}
}

void TextureCache::FreeImage(ImageId id, std::vector<ImageId>* retired) {
	auto* image = m_slot_images.try_get(id);
	if (image != nullptr) {
		LogImageChurn("free", *image);
	}
	while (image != nullptr && image->registered &&
	       HasPendingDownload(image->info.data.address, image->info.data.size)) {
		// Overlap replacement also retires images outside the collector. Preserve its
		// registration and write watcher until the last overlapping publication completes.
		// Wait does not run general callbacks, so native resources remain alive throughout.
		const auto completion_tick = m_scheduler.CurrentTick();
		{
			struct ReacquireLock {
				explicit ReacquireLock(TrackingSpinLock& mutex): mutex(mutex) { mutex.unlock(); }
				~ReacquireLock() { mutex.lock(); }
				TrackingSpinLock& mutex;
			};
			// All callers own m_lock. Release it while waiting, including for a priority
			// callback that needs the cache; restore it before touching the owner again.
			const ReacquireLock     unlocked(m_lock);
			DrainStats::ReasonScope reason(DrainStats::Reason::FreeImage);
			m_scheduler.Wait(completion_tick);
			m_scheduler.WaitPriorityOperations(completion_tick);
		}
		image = m_slot_images.try_get(id);
	}
	if (image == nullptr || !image->registered) {
		return;
	}
	if (image->IsGpuModified()) {
		image->ClearGpuModified();
	}
	DeleteImage(id, retired);
}

void TextureCache::TouchImage(Image& image) {
	// Draws touch the same images many times per GC tick: the LRU holds this tick already.
	if (image.registered && image.lru_tick != m_gc_tick) {
		image.lru_tick = m_gc_tick;
		m_lru_cache.Touch(image.lru_id, m_gc_tick);
	}
}

void TextureCache::MarkAsMaybeDirty(ImageId id, Image& image) {
	image.MarkMaybeCpuDirty();
	if (image.NeedsMaybeCpuHash()) {
		image.SetMaybeCpuHash(image.HashGuestEdges());
	}
	UntrackImage(id);
}

void TextureCache::TrackImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (!image.registered) {
		return;
	}
	const auto image_begin = image.info.data.address;
	const auto image_end   = image.info.data.End();
	if (image_begin == image.track_addr && image_end == image.track_addr_end) {
		return;
	}
	if (!image.IsTracked()) {
		image.track_addr     = image_begin;
		image.track_addr_end = image_end;
		BumpImageCleanEpoch();
		m_page_manager.UpdatePageWatchers<true>(image_begin, image.info.data.size);
		return;
	}
	if (image_begin < image.track_addr) {
		TrackImageHead(id);
	}
	if (image.track_addr_end < image_end) {
		TrackImageTail(id);
	}
}

void TextureCache::TrackImageHead(ImageId id) {
	auto& image = m_slot_images[id];
	if (!image.registered) {
		return;
	}
	const auto image_begin = image.info.data.address;
	if (image_begin == image.track_addr) {
		return;
	}
	if (!image.IsTracked() || image_begin > image.track_addr) {
		EXIT("TextureCache: invalid image head tracking range\n");
	}
	const auto size  = image.track_addr - image_begin;
	image.track_addr = image_begin;
	BumpImageCleanEpoch();
	m_page_manager.UpdatePageWatchers<true>(image_begin, size);
}

void TextureCache::TrackImageTail(ImageId id) {
	auto& image = m_slot_images[id];
	if (!image.registered) {
		return;
	}
	const auto image_end = image.info.data.End();
	if (image_end == image.track_addr_end) {
		return;
	}
	if (!image.IsTracked() || image.track_addr_end > image_end) {
		EXIT("TextureCache: invalid image tail tracking range\n");
	}
	const auto address   = image.track_addr_end;
	const auto size      = image_end - address;
	image.track_addr_end = image_end;
	BumpImageCleanEpoch();
	m_page_manager.UpdatePageWatchers<true>(address, size);
}

void TextureCache::UntrackImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (!image.IsTracked()) {
		return;
	}
	const auto address   = image.track_addr;
	const auto size      = image.track_addr_end - image.track_addr;
	image.track_addr     = 0;
	image.track_addr_end = 0;
	BumpImageCleanEpoch();
	if (size != 0) {
		m_page_manager.UpdatePageWatchers<false>(address, size);
	}
}

void TextureCache::UntrackImageHead(ImageId id) {
	auto&      image = m_slot_images[id];
	const auto begin = image.info.data.address;
	if (!image.IsTracked() || begin < image.track_addr) {
		return;
	}
	const auto address = Common::AlignDown(begin + TRACKER_PAGE_SIZE, TRACKER_PAGE_SIZE);
	const auto size    = address - begin;
	image.track_addr   = address;
	BumpImageCleanEpoch();
	if (image.track_addr == image.track_addr_end) {
		MarkAsMaybeDirty(id, image);
	}
	if (size != 0) {
		m_page_manager.UpdatePageWatchers<false>(begin, size);
	}
}

void TextureCache::UntrackImageTail(ImageId id) {
	auto&      image = m_slot_images[id];
	const auto end   = image.info.data.End();
	if (!image.IsTracked() || image.track_addr_end < end) {
		return;
	}
	const auto address   = Common::AlignDown(end, TRACKER_PAGE_SIZE);
	const auto size      = end - address;
	image.track_addr_end = address;
	BumpImageCleanEpoch();
	if (image.track_addr == image.track_addr_end) {
		MarkAsMaybeDirty(id, image);
	}
	if (size != 0) {
		m_page_manager.UpdatePageWatchers<false>(address, size);
	}
}

void TextureCache::TrackImageDownload(ImageId id, Image& image) {
	if (m_readback_linear_images && !image.info.IsTiled() && !image.info.data.Empty()) {
		if (!image.IsGpuModified()) {
			EXIT("TextureCache: cannot enroll a non-GPU-owned image for download\n");
		}
		m_download_images.insert(id);
	}
}

TextureCache::ImageIds TextureCache::FindImagesInRegion(uint64_t address, uint64_t size,
                                                        bool page_overlap) const {
	ImagePageTable::PageRange pages {};
	if (!ImagePageTable::TryGetPageRange(address, size, pages)) {
		return {};
	}

	uint32_t query_epoch = ++m_image_query_epoch;
	if (query_epoch == 0) {
		m_slot_images.ForEach([](ImageId, const Image& image) { image.query_epoch = 0; });
		query_epoch = ++m_image_query_epoch;
	}

	ImageIds result;
	ForEachPage(address, size, [&](uint64_t page) {
		const auto* owners = m_image_page_table.Find(page);
		if (owners == nullptr) {
			return;
		}
		owners->ForEach([&](ImageId id) {
			auto* image = m_slot_images.try_get(id);
			if (image == nullptr) {
				return;
			}
			if (image->query_epoch == query_epoch) {
				return;
			}
			image->query_epoch = query_epoch;
			if (image->Overlaps(address, size, page_overlap)) {
				result.push_back(id);
			}
		});
	});
	return result;
}

ImageId TextureCache::GetNullImage(const ImageDesc& desc) {
	const auto format = desc.info.pixel_format;
	if (const auto found = m_null_images.find(format); found != m_null_images.end()) {
		return found->second;
	}
	ImageInfo info {};
	info.pixel_format    = desc.info.pixel_format;
	info.guest_format    = desc.info.guest_format;
	info.type            = Prospero::ImageType::kColor2D;
	info.extent          = {1, 1, 1};
	info.resources       = {1, 1};
	info.pitch           = 1;
	info.bytes_per_block = std::max(desc.info.bytes_per_block, 1u);
	info.samples         = 1;
	info.tile_mode       = Prospero::TileMode::kLinear;
	info.mip_layout[0]   = {0, info.bytes_per_block, 1, 1};
	const auto id        = InsertImage(info);
	m_null_images.emplace(format, id);
	return id;
}

void TextureCache::ValidateImageDesc(const ImageDesc& desc) const {
	ImageOps::Validate(desc.info);
	if (desc.view_info.format == vk::Format::eUndefined || desc.view_info.level_count == 0 ||
	    desc.view_info.layer_count == 0 ||
	    desc.view_info.base_level >= desc.info.resources.levels ||
	    desc.view_info.level_count > desc.info.resources.levels - desc.view_info.base_level ||
	    (!desc.info.IsVolume() &&
	     (desc.view_info.base_layer >= desc.info.resources.layers ||
	      desc.view_info.layer_count > desc.info.resources.layers - desc.view_info.base_layer))) {
		EXIT("TextureCache: invalid image view description\n");
	}
	if (desc.type == BindingType::DepthTarget && !IsSupportedDepthTargetFormat(desc.info)) {
		EXIT("TextureCache: unsupported depth image description\n");
	}
	if (desc.type == BindingType::VideoOut && !IsSupportedVideoOutFormat(desc.info)) {
		EXIT("TextureCache: unsupported video-out image description\n");
	}
	if (desc.type == BindingType::VideoOut &&
	    desc.info.metadata.compression == VideoOutCompression::Unsupported) {
		EXIT("TextureCache: unsupported compressed video-out description\n");
	}
}

void TextureCache::PrepareImageCopy(Image& image) {
	if (image.IsCpuDirty()) {
		image.RefreshComplete();
	}
}

void TextureCache::RefreshCopySource(ImageId id) {
	auto& image = m_slot_images[id];
	RefreshImage(id);
	if (image.IsDefinitelyCpuDirty()) {
		EXIT("TextureCache: image copy source remained CPU-dirty after refresh\n");
	}
}

bool TextureCache::CopyD16(Image& destination, Image& source) {
	TileManager::ScratchScope scratch_scope(m_tiler);
	const bool source_depth      = source.info.IsDepth();
	const bool destination_depth = destination.info.IsDepth();
	if (source_depth == destination_depth) {
		return false;
	}
	auto&      depth          = source_depth ? source : destination;
	auto&      color          = source_depth ? destination : source;
	const auto transfer_bytes = DepthAspectTransferBytes(depth.backing.format);
	if (depth.info.bytes_per_block != sizeof(uint16_t) ||
	    color.info.bytes_per_block != sizeof(uint16_t) || transfer_bytes != sizeof(uint32_t)) {
		return false;
	}
	EXIT_IF(source.backing.samples != 1 || destination.backing.samples != 1 ||
	        source.info.resources.levels != 1 || destination.info.resources.levels != 1 ||
	        source.info.extent != destination.info.extent ||
	        source.info.resources.layers != destination.info.resources.layers);

	const auto     layers = depth.info.resources.layers;
	const uint64_t depth_slice =
	    static_cast<uint64_t>(depth.info.pitch) * depth.info.extent.height * transfer_bytes;
	const uint64_t color_slice =
	    static_cast<uint64_t>(color.info.pitch) * color.info.extent.height * sizeof(uint16_t);
	EXIT_IF(layers == 0 || depth_slice > UINT64_MAX / layers || color_slice > UINT64_MAX / layers);
	const auto                       depth_size = depth_slice * layers;
	const auto                       color_size = color_slice * layers;
	std::vector<vk::BufferImageCopy> depth_copies(layers);
	std::vector<vk::BufferImageCopy> color_copies(layers);
	for (uint32_t layer = 0; layer < layers; layer++) {
		depth_copies[layer].bufferOffset      = depth_slice * layer;
		depth_copies[layer].bufferRowLength   = depth.info.pitch;
		depth_copies[layer].bufferImageHeight = depth.info.extent.height;
		depth_copies[layer].imageSubresource  = {vk::ImageAspectFlagBits::eDepth, 0, layer, 1};
		depth_copies[layer].imageExtent       = depth.info.extent;
		color_copies[layer].bufferOffset      = color_slice * layer;
		color_copies[layer].bufferRowLength   = color.info.pitch;
		color_copies[layer].bufferImageHeight = color.info.extent.height;
		color_copies[layer].imageSubresource  = {vk::ImageAspectFlagBits::eColor, 0, layer, 1};
		color_copies[layer].imageExtent       = color.info.extent;
	}

	auto                         depth_buffer = m_tiler.GetScratchBuffer(depth_size);
	auto                         color_buffer = m_tiler.GetScratchBuffer(color_size);
	const TileManager::D16Layout promote_layout {
	    .width               = depth.info.extent.width,
	    .height              = depth.info.extent.height,
	    .layers              = layers,
	    .source_row_stride   = static_cast<uint64_t>(color.info.pitch) * sizeof(uint16_t),
	    .target_row_stride   = static_cast<uint64_t>(depth.info.pitch) * transfer_bytes,
	    .source_slice_stride = color_slice,
	    .target_slice_stride = depth_slice,
	};
	const bool d32 = DepthAspectTransferFormat(depth.backing.format) == vk::Format::eD32Sfloat;
	if (source_depth) {
		source.Download(depth_copies, depth_buffer.buffer, depth_buffer.offset, depth_buffer.size,
		                depth_buffer.capacity);
		m_tiler.ConvertD16(depth_buffer, color_buffer, TileManager::D16Direction::Demote, d32,
		                   {.width               = promote_layout.width,
		                    .height              = promote_layout.height,
		                    .layers              = promote_layout.layers,
		                    .source_row_stride   = promote_layout.target_row_stride,
		                    .target_row_stride   = promote_layout.source_row_stride,
		                    .source_slice_stride = promote_layout.target_slice_stride,
		                    .target_slice_stride = promote_layout.source_slice_stride});
		destination.Upload(color_copies, color_buffer.buffer, color_buffer.offset,
		                   color_buffer.size, color_buffer.capacity);
	} else {
		source.Download(color_copies, color_buffer.buffer, color_buffer.offset, color_buffer.size,
		                color_buffer.capacity);
		m_tiler.ConvertD16(color_buffer, depth_buffer, TileManager::D16Direction::Promote, d32,
		                   promote_layout);
		destination.Upload(depth_copies, depth_buffer.buffer, depth_buffer.offset,
		                   depth_buffer.size, depth_buffer.capacity);
	}
	return true;
}

void TextureCache::CopyImage(ImageId destination_id, ImageId source_id) {
	RefreshCopySource(source_id);
	auto& destination = m_slot_images[destination_id];
	auto& source      = m_slot_images[source_id];
	GpuZones::KeyScope zone_key(destination.info.data.address);
	TrackImage(destination_id);
	if (source.backing.samples != destination.backing.samples) {
		EXIT("TextureCache: cannot issue an unequal-sample image copy\n");
	}
	PrepareImageCopy(destination);
	if (source.IsBufferModified()) {
		if (source.info.data == destination.info.data) {
			destination.MarkBufferModified();
		}
		return;
	}
	const bool source_depth = source.info.IsDepth();
	const bool dest_depth   = destination.info.IsDepth();
	const bool direct_copy =
	    source.info.GetColorTransform() == destination.info.GetColorTransform() &&
	    (source.backing.image_type == destination.backing.image_type ||
	     (source.backing.image_type != vk::ImageType::e1D &&
	      destination.backing.image_type != vk::ImageType::e1D)) &&
	    (source.backing.format == destination.backing.format ||
	     (!source_depth && !dest_depth &&
	      vk::blockSize(source.backing.format) == vk::blockSize(destination.backing.format)));
	if (direct_copy) {
		destination.CopyImage(source);
	} else if (!CopyD16(destination, source)) {
		if (source.backing.samples != 1 || destination.backing.samples != 1) {
			EXIT("TextureCache: cross-format multisample image copy is unsupported\n");
		}
		auto& copy_buffer = m_buffer_cache.GetUtilityBuffer(MemoryUsage::DeviceLocal);
		destination.CopyImageWithBuffer(source, copy_buffer, m_tiler);
	}
	if (source.IsGpuModified()) {
		MarkGpuModified(destination);
	}
	destination.ClearBufferModified();
	if (PixelHistoryEnabled()) [[unlikely]] {
		PixelHistoryProbe(m_graphics, m_scheduler, destination, 0, 0, "copy", 0, 0,
		                  fmt::format("from image 0x{:x}", source.info.data.address), true);
	}
}

void TextureCache::CopyImageMip(ImageId destination_id, ImageId source_id, uint32_t mip,
                                uint32_t layer) {
	RefreshCopySource(source_id);
	// A partial image copy must start from the current buffer-owned destination,
	// otherwise untouched mips/layers retain stale bytes from the earlier upload.
	if (m_slot_images[destination_id].IsBufferModified()) RefreshImage(destination_id);
	auto& destination = m_slot_images[destination_id];
	auto& source      = m_slot_images[source_id];
	GpuZones::KeyScope zone_key(destination.info.data.address);
	TrackImage(destination_id);
	if (source.IsBufferModified() || source.backing.samples != destination.backing.samples) {
		EXIT("TextureCache: invalid mip-copy ownership or sample count\n");
	}
	destination.CopyMip(source, mip, layer);
	if (PixelHistoryEnabled()) [[unlikely]] {
		PixelHistoryProbe(m_graphics, m_scheduler, destination, 0, 0, "copy-mip", 0, 0,
		                  fmt::format("from image 0x{:x} mip {} layer {}", source.info.data.address, mip, layer), true);
	}
	if (source.IsGpuModified()) {
		MarkGpuModified(destination);
	}
}

ImageId TextureCache::ResolveDepthOverlap(const ImageInfo& requested, BindingType binding,
                                          ImageId cached_id) {
	ChurnReason reason("depth-overlap");
	auto&       cached = m_slot_images[cached_id];
	if ((!cached.info.IsDepth() && !requested.IsDepth()) ||
	    cached.info.tile_mode != requested.tile_mode) {
		return {};
	}
	const bool stencil_match = requested.HasStencil() == cached.info.HasStencil();
	const bool bpp_match     = requested.bytes_per_block == cached.info.bytes_per_block;
	// PPSA04264
	const bool raw_d16_texture =
	    binding == BindingType::Texture && cached.info.IsDepth() &&
	    cached.info.guest_format == Prospero::BufferFormat::k16UNorm &&
	    requested.guest_format == Prospero::BufferFormat::k16UInt &&
	    requested.pixel_format == vk::Format::eR16Uint && cached.backing.samples == 1 &&
	    requested.samples == 1 && requested.data == cached.info.data &&
	    requested.extent == cached.info.extent && requested.resources == cached.info.resources &&
	    requested.type == cached.info.type && requested.pitch == cached.info.pitch &&
	    !requested.HasStencil() && !cached.info.HasStencil() && !requested.HasMetadata() &&
	    !cached.info.HasMetadata();
	// PPSA04264, PPSA04288
	// A partial view retains the entire matching array layout. HTile belongs to
	// the depth source, independently of the data slices copied into color storage.
	const bool retain_cached_layout =
	    requested.samples == 1 && cached.info.samples == 1 && cached.backing.samples == 1 &&
	    requested.bytes_per_block == cached.info.bytes_per_block &&
	    requested.data.address == cached.info.data.address &&
	    requested.data.size < cached.info.data.size && requested.extent == cached.info.extent &&
	    requested.resources.levels == 1 && cached.info.resources.levels == 1 &&
	    requested.resources.layers != 0 && cached.info.resources.layers != 0 &&
	    requested.resources.layers < cached.info.resources.layers &&
	    requested.type == cached.info.type && requested.pitch == cached.info.pitch &&
	    requested.mip_layout[0].offset == 0 &&
	    cached.info.mip_layout[0].offset == 0 &&
	    requested.mip_layout[0].size == requested.data.size &&
	    cached.info.mip_layout[0].size == cached.info.data.size &&
	    requested.data.size % requested.resources.layers == 0 &&
	    cached.info.data.size % cached.info.resources.layers == 0 &&
	    requested.data.size / requested.resources.layers ==
	        cached.info.data.size / cached.info.resources.layers &&
	    !requested.HasStencil() && !cached.info.HasStencil() && !requested.HasMetadata() &&
	    (cached.info.metadata.kind == ImageMetadataKind::None ||
	     cached.info.metadata.kind == ImageMetadataKind::Htile);
	bool recreate = cached.info.resources < requested.resources ||
	                requested.IsVolume() != cached.info.IsVolume();
	switch (binding) {
		case BindingType::Texture:
			recreate |= requested.IsDepth() && !cached.info.IsDepth();
			recreate |= raw_d16_texture;
			break;
		case BindingType::Storage: recreate |= cached.info.IsDepth(); break;
		case BindingType::RenderTarget: recreate |= cached.info.IsDepth(); break;
		case BindingType::DepthTarget:
			recreate |= !cached.info.IsDepth();
			recreate |= cached.info.IsDepth() && !(stencil_match && bpp_match);
			break;
		case BindingType::VideoOut: recreate |= cached.info.IsDepth(); break;
	}
	if (!recreate) {
		return cached_id;
	}
	RefreshImage(cached_id);
	auto info = requested;
	if (retain_cached_layout) {
		info.data       = cached.info.data;
		info.resources  = cached.info.resources;
		info.mip_layout = cached.info.mip_layout;
	}
	info.htile_clear_mask     = 0;
	const auto replacement_id = InsertImage(info);
	auto&      replacement    = m_slot_images[replacement_id];
	replacement.usage         = cached.usage;
	if (cached.binding.is_bound || cached.binding.is_target) {
		cached.binding.needs_rebind = true;
		BumpImageCleanEpoch();
	}
	if (cached.backing.samples == replacement.backing.samples) {
		const bool copy_supported =
		    cached.backing.samples == 1 || cached.backing.format == replacement.backing.format ||
		    (!cached.info.IsDepth() && !replacement.info.IsDepth() &&
		     ImageViewOps::FormatsCompatible(cached.backing.format, replacement.backing.format));
		if (copy_supported) {
			CopyImage(replacement_id, cached_id);
		} else {
			LOGF_COLOR(Log::Color::BrightYellow,
			           "TextureCache: unsupported cross-format multisample depth copy\n");
		}
	} else if (cached.backing.samples == 1 && replacement.backing.samples > 1 &&
	           replacement.info.IsDepth()) {
		RefreshCopySource(cached_id);
		if (cached.IsBufferModified() || cached.IsDefinitelyCpuDirty()) {
			EXIT("TextureCache: multisample depth conversion source is not native-current\n");
		}
		PrepareImageCopy(replacement);
		m_blit_helper.ReinterpretColorAsMsDepth(cached, replacement);
		CommitGpuWrite(replacement);
	} else {
		LOGF_COLOR(Log::Color::BrightYellow,
		           "TextureCache: unsupported unequal-sample depth overlap copy (%u -> %u)\n",
		           cached.backing.samples, replacement.backing.samples);
	}
	// The copy carries the depth plane only. A depth image replaced by a color one keeps its
	// stencil plane aside; a color image replaced by a depth one gets it back (Ghost of Yotei
	// alternates its depth buffer between depth target and storage image every frame, and reads
	// the stencil for its material masks in between).
	if (cached.info.IsDepth() && cached.info.HasStencil() && !replacement.info.IsDepth()) {
		KeepStencil(cached_id);
	} else if (!cached.info.IsDepth() && replacement.info.IsDepth() && replacement.info.HasStencil()) {
		// KYTY_STENCIL_FROM_R8=1: while the depth buffer was a color image, shaders may have
		// changed the stencil through the 8-bit image at its stencil address; that image, when
		// written since, refreshes the keeper first, so the depth image does not get the stale
		// kept stencil back.
		RefreshKeeperFromStencilTexture(replacement);
		if (RestoreStencil(replacement, true)) {
			replacement.stencil_restored = true;
		} else {
			if (StencilSourceLog()) [[unlikely]] {
				std::printf("\x1b[91mstencil-source: depth 0x%llx (stencil 0x%llx) replaces a color image without "
				            "a kept stencil: %s\x1b[0m\n",
				            static_cast<unsigned long long>(replacement.info.data.address),
				            static_cast<unsigned long long>(replacement.info.stencil.address),
				            StencilKeepFix() ? "stencil plane cleared to 0" : "its stencil plane is not restored");
				std::fflush(stdout);
			}
			if (StencilKeepFix()) {
				// KYTY_STENCIL_KEEP_FIX: a defined stencil (0, as the frame's stencil clear) instead of
				// whatever the new image's memory holds.
				m_scheduler.EndRendering();
				auto command = m_scheduler.Current().Handle();
				replacement.Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, {},
				                    command);
				const vk::ClearDepthStencilValue zero {0.0f, 0u};
				const vk::ImageSubresourceRange  plane {vk::ImageAspectFlagBits::eStencil, 0,
				                                        replacement.backing.mip_levels, 0, replacement.backing.layers};
				command.clearDepthStencilImage(replacement.backing.image, vk::ImageLayout::eTransferDstOptimal,
				                               &zero, 1, &plane);
				if (PixelHistoryEnabled()) [[unlikely]] {
					PixelHistoryProbe(m_graphics, m_scheduler, replacement, 0, 0, "init-stencil", 0, 0,
					                  "no kept stencil, cleared to 0", true);
				}
			}
		}
	}
	FreeImage(cached_id);
	return replacement_id;
}

bool TextureCache::IsBlockAlias(const ImageInfo& requested, const ImageInfo& cached) {
	return requested.guest_format != cached.guest_format &&
	       requested.IsBlock() != cached.IsBlock() &&
	       requested.tile_mode == cached.tile_mode &&
	       requested.bytes_per_block == cached.bytes_per_block &&
	       requested.samples == 1 && cached.samples == 1 &&
	       (requested.data != cached.data || requested.resources != cached.resources ||
	        requested.BlockExtent() != cached.BlockExtent()) &&
	       !SameBacking(cached, requested, false);
}

void TextureCache::ResolveBlockAlias(ImageId cached_id, ImageId merged_id) {
	auto& cached = m_slot_images[cached_id];
	auto* merged = merged_id ? &m_slot_images[merged_id] : nullptr;
	// A raw block image can cover several packed BC mips. Publish its guest
	// footprint before retiring the incompatible native image, preserving bytes
	// outside that footprint through the existing buffer ownership path.
	if (cached.SafeToDownload()) {
		if (merged && merged->SafeToDownload() && !PublishBlockAlias(*merged)) return;
		if (!PublishBlockAlias(cached)) return;
		if (merged) merged->MarkBufferModified();
	}
	cached.binding.needs_rebind |= cached.binding.is_bound || cached.binding.is_target;
	FreeImage(cached_id);
}

TextureCache::OverlapResult TextureCache::ResolveOverlap(const ImageInfo& requested,
                                                         BindingType binding, ImageId cached_id,
                                                         ImageId merged_id) {
	auto owner = m_slot_images.try_get(cached_id);
	if (owner == nullptr || !owner->registered) {
		return {merged_id};
	}
	if (IsBlockAlias(requested, owner->info) && !owner->IsCpuDirty()) {
		ResolveBlockAlias(cached_id, merged_id);
		return {merged_id};
	}
	auto&      cached       = *owner;
	const auto current_tick = m_scheduler.CurrentTick();
	const bool safe_to_delete =
	    current_tick - std::min(current_tick, cached.tick_accessed_last) > NumFramesBeforeRemoval;

	const uint32_t requested_block = requested.bytes_per_block * requested.samples;
	const uint32_t cached_block    = cached.info.bytes_per_block * cached.info.samples;
	if (requested.data.address == cached.info.data.address &&
	    requested.BlockExtent() == cached.info.BlockExtent() && requested_block == cached_block) {
		if (const auto depth_id = ResolveDepthOverlap(requested, binding, cached_id)) {
			return {depth_id};
		}
		// Equal pitch does not imply equal mip placement: a changed extent can move
		// a level into or out of the mip tail. These are separate guest layouts.
		if (requested.tile_mode != cached.info.tile_mode ||
		    (requested.resources == cached.info.resources &&
		     requested.mip_layout != cached.info.mip_layout)) {
			if (safe_to_delete) {
				ChurnReason reason("overlap-layout");
				FreeImage(cached_id);
			}
			return {merged_id};
		}
		if (requested.GetColorTransform() != cached.info.GetColorTransform()) {
			return {ExpandImage(requested, cached_id)};
		}
		if (requested.IsBlock() && !cached.info.IsBlock()) {
			return {ExpandImage(requested, cached_id)};
		}
		// Volume depth is not an array-layer count. A larger depth can retain the
		// same block-slice layout while requiring a larger native image.
		if ((requested.IsVolume() || cached.info.IsVolume()) &&
		    (requested.data.size == cached.info.data.size ||
		     (requested.type == cached.info.type && requested.resources == cached.info.resources &&
		      requested.extent.width == cached.info.extent.width &&
		      requested.extent.height == cached.info.extent.height &&
		      requested.extent.depth > cached.info.extent.depth))) {
			return {ExpandImage(requested, cached_id)};
		}
		// PPSA08394
		// A view cannot change the native image type or grow its extent.
		if (requested.data.size == cached.info.data.size &&
		    requested.resources == cached.info.resources &&
		    ImageViewOps::FormatsCompatible(cached.info.pixel_format, requested.pixel_format) &&
		    (requested.type != cached.info.type
		         ? requested.extent == cached.info.extent
		         : requested.extent.width > cached.info.extent.width &&
		               requested.extent.height >= cached.info.extent.height &&
		               requested.extent.depth >= cached.info.extent.depth)) {
			return {ExpandImage(requested, cached_id)};
		}
		// PS5 mip tails can expose more levels without increasing the guest allocation.
		if (requested.pixel_format == cached.info.pixel_format &&
		    requested.type == cached.info.type && requested.resources > cached.info.resources &&
		    (requested.data.size > cached.info.data.size ||
		     (requested.data.size == cached.info.data.size &&
		      requested.extent == cached.info.extent &&
		      cached.info.resources.levels > 1 &&
		      requested.resources.layers == cached.info.resources.layers))) {
			return {ExpandImage(requested, cached_id)};
		}
		if (requested.type != cached.info.type) {
			return {merged_id};
		}
		if (requested.pixel_format != cached.info.pixel_format ||
		    requested.data.size <= cached.info.data.size) {
			const auto result_id = merged_id ? merged_id : cached_id;
			const auto result    = m_slot_images.try_get(result_id);
			return {result != nullptr && ImageViewOps::FormatsCompatible(result->info.pixel_format,
			                                                             requested.pixel_format)
			            ? result_id
			            : ImageId {}};
		}
		// Same address, extent, format and type, but the request spans more guest memory (seen in
		// Ghost of Yotei: 1x1 R8 views of 0x1000 and 0x2000 bytes). The larger image replaces the
		// cached one, which it fully contains.
		static std::atomic<uint32_t> grow_logs {0};
		if (grow_logs.fetch_add(1, std::memory_order_relaxed) < 8) {
			std::printf("TextureCache: equal-address image grows, address=0x%016" PRIx64
			            " levels=%u/%u layers=%u/%u size=0x%" PRIx64 "/0x%" PRIx64 " tile=%u\n",
			            requested.data.address, requested.resources.levels,
			            cached.info.resources.levels, requested.resources.layers,
			            cached.info.resources.layers, requested.data.size, cached.info.data.size,
			            static_cast<uint32_t>(requested.tile_mode));
		}
		return {ExpandImage(requested, cached_id)};
	}

	const int32_t requested_mip = requested.MipOf(cached.info);
	if (requested_mip >= 0) {
		const int32_t layer = requested.SliceOf(cached.info, requested_mip);
		return {cached_id, requested_mip, layer};
	}

	const int32_t mip = cached.info.MipOf(requested);
	if (mip >= 0) {
		const int32_t layer = cached.info.SliceOf(requested, mip);
		if (!merged_id) {
			return {ExpandImage(requested, cached_id)};
		}
		cached.binding.needs_rebind |= cached.binding.is_bound || cached.binding.is_target;
		m_slot_images[merged_id].binding.is_target |= cached.binding.is_target;
		CopyImageMip(merged_id, cached_id, static_cast<uint32_t>(mip),
		             static_cast<uint32_t>(layer));
		ChurnReason reason("overlap-merge");
		FreeImage(cached_id);
		return {merged_id};
	}
	if (requested.data.address >= cached.info.data.address && safe_to_delete) {
		ChurnReason reason("overlap-replace");
		FreeImage(cached_id);
	}
	return {merged_id};
}

ImageId TextureCache::ExpandImage(const ImageInfo& info, ImageId source_id) {
	ChurnReason reason("expand");
	RefreshCopySource(source_id);
	const auto expanded_id = InsertImage(info);
	auto&      source      = m_slot_images[source_id];
	if (source.binding.is_bound || source.binding.is_target) {
		source.binding.needs_rebind = true;
		BumpImageCleanEpoch();
	}
	InitializeImage(expanded_id);
	const int32_t mip = source.info.MipOf(info);
	const int32_t layer = source.info.SliceOf(info, mip);
	if (layer >= 0) {
		CopyImageMip(expanded_id, source_id, static_cast<uint32_t>(mip),
		             static_cast<uint32_t>(layer));
	} else {
		CopyImage(expanded_id, source_id);
	}
	FreeImage(source_id);
	return expanded_id;
}

struct TextureCache::TextureTransfer {
	TextureUploadLayout              layout;
	std::vector<vk::BufferImageCopy> regions;
	std::vector<GpuTileInfo>         tiles;
	ColorTransform                   color_transform = ColorTransform::None;
	bool                             valid           = false;

	[[nodiscard]] uint64_t LinearSize() const {
		uint64_t size = 0;
		for (const auto& tile: tiles) {
			size = std::max(size, tile.linear_offset + tile.linear_size);
		}
		return size;
	}
};

struct TextureCache::ImageDownload {
	TextureTransfer texture;
	bool                depth_target = false;
	bool                valid        = false;
};

TextureCache::TextureTransfer
TextureCache::BuildTextureTransfer(const Image& image, BindingType binding,
                                    TransferDirection direction) const {
	const auto& info             = image.info;
	const bool  upload           = direction == TransferDirection::Upload;
	const bool  render_target    = binding == BindingType::RenderTarget;
	const bool  video_out        = binding == BindingType::VideoOut;
	uint32_t    layers           = info.TransferLayers();
	bool        volume           = info.IsVolume();
	const char* owner            = "TextureCache readback";

	TextureTransfer transfer;
	transfer.color_transform = info.GetColorTransform();
	if (upload) {
		if ((render_target || video_out) &&
		    (info.resources.layers == 0 || info.data.size % info.resources.layers != 0 ||
		     info.samples != 1 || image.backing.samples != 1)) {
			EXIT("TextureCache: invalid color-attachment upload\n");
		}
		owner = "TextureCache";
		if (render_target) {
			owner = "RenderTarget";
		} else if (binding == BindingType::Storage) {
			owner = "StorageTextureCache";
		} else if (video_out) {
			if (info.metadata.compression != VideoOutCompression::Uncompressed) {
				EXIT("TextureCache: invalid color-attachment upload\n");
			}
			layers = info.resources.layers;
			volume = false;
			owner  = "VideoOut";
		}
	}

	transfer.layout = TextureCalcUploadLayout(info.guest_format, info.extent.width,
	                                         info.extent.height, info.resources.levels, layers,
	                                         info.tile_mode, info.data.size, volume, owner);
	transfer.regions = TextureBuildImageCopies(transfer.layout);
	if (info.IsDepth()) {
		for (auto& region: transfer.regions) {
			region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
		}
	}
	if (transfer.layout.surface.description.tile_mode != Prospero::TileMode::kLinear) {
		if (!TextureBuildGpuTileInfos(info.data.size, transfer.regions, transfer.layout,
		                              info.resources.levels, transfer.tiles)) {
			return transfer;
		}
	}
	transfer.valid = true;
	return transfer;
}

TextureCache::ImageDownload TextureCache::BuildDownload(const Image& image) const {
	const auto&  info    = image.info;
	const auto   binding = UploadBinding(image);
	ImageDownload transfer {.depth_target = binding == BindingType::DepthTarget};
	if (info.samples != 1 || image.backing.samples != 1) {
		return transfer;
	}
	if (transfer.depth_target) {
		transfer.valid = IsSupportedDepthPlaneReadback(info) && info.resources.layers != 0 &&
		             info.data.size % info.resources.layers == 0 &&
		             Prospero::NumBytesPerElement(info.guest_format) == info.bytes_per_block;
		return transfer;
	}
	if (info.metadata.compression != VideoOutCompression::Uncompressed) {
		return transfer;
	}
	transfer.texture = BuildTextureTransfer(image, binding, TransferDirection::Download);
	transfer.valid   = transfer.texture.valid;
	return transfer;
}

bool TextureCache::PublishBlockAlias(Image& image) {
	auto transfer = BuildDownload(image);
	if (!transfer.valid) return false;
	const auto [buffer, offset] =
	    m_buffer_cache.ObtainBufferForImagePublication(image.info.data.address, image.info.data.size);
	DownloadImage(image, *buffer, offset, image.info.data.size, std::move(transfer));
	image.ClearGpuModified();
	image.MarkBufferModified();
	return true;
}

void TextureCache::PrepareBlockBufferWrite(uint64_t address, uint64_t size) {
	if (!GuestRange {address, size}.Valid()) {
		return;
	}
	std::scoped_lock lock {m_lock};
	PrepareBlockBufferWriteLocked(address, size);
}

void TextureCache::PrepareBlockBufferWriteLocked(uint64_t address, uint64_t size) {
	for (const auto id: FindImagesInRegion(address, size, false)) {
		auto& image = m_slot_images[id];
		if (!image.registered || image.depth_id || !image.info.IsBlock() ||
		    !image.Overlaps(address, size) || !image.SafeToDownload()) {
			continue;
		}
		// A writable descriptor can store only a few blocks. Preserve every old
		// GPU block before the caller records its write, including other mips
		// and layers outside the requested buffer's original partial owner.
		if (!PublishBlockAlias(image)) {
			EXIT("TextureCache: cannot preserve GPU-owned BC image before buffer write\n");
		}
	}
}

void TextureCache::UploadImage(Image& image, Buffer& source, uint64_t source_offset) {
	TileManager::ScratchScope scratch_scope(m_tiler);
	GpuZones::KeyScope zone_key(image.info.data.address);
	auto& destination = image.depth_id ? m_slot_images[image.depth_id] : image;
	const auto binding = image.depth_id ? BindingType::DepthTarget : UploadBinding(image);
	const auto  upload  = [&](std::vector<vk::BufferImageCopy>& copies, TileManager::Result linear) {
		for (auto& copy: copies) {
			copy.bufferOffset += linear.offset;
		}
		destination.Upload(copies, linear.buffer, linear.offset, linear.size, linear.capacity);
	};

	if (binding != BindingType::DepthTarget) {
		const auto& info = image.info;
		// Ghost of Yotei's terrain images (513x513 R32F/R8) are created where the early device
		// losses happen: note any with more than one level or layer (a larger upload footprint).
		if (info.extent.width == 513u && info.extent.height == 513u &&
		    (info.resources.levels > 1u || info.resources.layers > 1u)) {
			static std::atomic<uint32_t> noted {0};
			if (noted.fetch_add(1, std::memory_order_relaxed) < 16u) {
				std::printf("copy-terrain-upload: guest=0x%016" PRIx64 " size=0x%" PRIx64
				            " format=%u tile=%u levels=%u layers=%u\n",
				            info.data.address, info.data.size,
				            static_cast<uint32_t>(info.guest_format),
				            static_cast<uint32_t>(info.tile_mode), info.resources.levels,
				            info.resources.layers);
			}
		}
		auto transfer = BuildTextureTransfer(image, binding, TransferDirection::Upload);
		if (!transfer.valid) {
			EXIT("TextureCache: invalid texture upload: binding=%u addr=0x%016" PRIx64
			     " size=0x%016" PRIx64 " format=%u tile=%u family=%u extent=%ux%ux%u "
			     "pitch=%u levels=%u layers=%u samples=%u\n",
			     static_cast<uint32_t>(binding), info.data.address, info.data.size,
			     static_cast<uint32_t>(info.guest_format), static_cast<uint32_t>(info.tile_mode),
			     static_cast<uint32_t>(transfer.layout.surface.texture.block.family), info.extent.width,
			     info.extent.height, info.extent.depth, info.pitch, info.resources.levels,
			     info.resources.layers, info.samples);
		}
		TileManager::Result linear {source.Handle(), source_offset, info.data.size, source.Size()};
		if (!transfer.tiles.empty() &&
		    m_tiler.TryUploadImage(destination, linear, transfer.LinearSize(), transfer.tiles,
		                           transfer.regions, transfer.color_transform)) {
			return;
		}
		if (!transfer.tiles.empty()) {
			linear = m_tiler.Detile(source.Handle(), source_offset, info.data.size,
			                        transfer.LinearSize(), transfer.tiles, transfer.color_transform);
		} else if (transfer.color_transform != ColorTransform::None) {
			linear = m_tiler.TransformColor(linear, transfer.color_transform, true);
		}
		upload(transfer.regions, linear);
		return;
	}

	// The stencil plane has its own row pitch and shares the native image
	// with the depth plane.
	auto info = destination.info;
	if (image.depth_id) {
		info.data            = image.info.data;
		info.resources       = {image.stencil_subresources.level_count,
		                        image.stencil_subresources.layer_count};
		info.guest_format    = Prospero::BufferFormat::k8UInt;
		info.bytes_per_block = 1;
		if (info.IsTiled()) info.pitch = TileGetDepthPitch(info.extent.width, 1, 0);
	}
	if (info.samples != 1 || destination.backing.samples != 1 ||
	    info.resources.layers == 0 || info.data.size % info.resources.layers != 0 ||
	    Prospero::NumBytesPerElement(info.guest_format) != info.bytes_per_block) {
		EXIT("TextureCache: invalid depth upload\n");
	}
	const auto          layers          = info.resources.layers;
	const auto          full_slice_size = info.data.size / layers;
	auto copies = BuildDepthCopies(info, full_slice_size, image.depth_id
	                                                        ? vk::ImageAspectFlagBits::eStencil
	                                                        : vk::ImageAspectFlagBits::eDepth);
	if (image.depth_id) {
		for (auto& copy: copies) {
			copy.imageSubresource.baseArrayLayer += image.stencil_subresources.base_layer;
		}
	}
	TileManager::Result linear {source.Handle(), source_offset, source.Size() - source_offset};
	if (info.IsTiled()) {
		const auto tiles = BuildDepthTiles(info);
		linear =
		    m_tiler.Detile(source.Handle(), source_offset, info.data.size, info.data.size, tiles);
	}
	const auto transfer_bytes = image.depth_id ? 1u : DepthAspectTransferBytes(info.pixel_format);
	if (transfer_bytes != info.bytes_per_block) {
		const uint64_t texels_per_slice = static_cast<uint64_t>(info.pitch) * info.extent.height;
		EXIT_NOT_IMPLEMENTED(info.bytes_per_block != sizeof(uint16_t) ||
		                     transfer_bytes != sizeof(uint32_t) || texels_per_slice > UINT32_MAX ||
		                     texels_per_slice > UINT64_MAX / transfer_bytes);
		const uint64_t transfer_slice = texels_per_slice * transfer_bytes;
		EXIT_NOT_IMPLEMENTED(transfer_slice > UINT64_MAX / layers);
		auto promoted = m_tiler.GetScratchBuffer(transfer_slice * layers);
		m_tiler.ConvertD16(
		    linear, promoted, TileManager::D16Direction::Promote,
		    info.pixel_format == vk::Format::eD32SfloatS8Uint,
		    {.width               = info.extent.width,
		     .height              = info.extent.height,
		     .layers              = layers,
		     .source_row_stride   = static_cast<uint64_t>(info.pitch) * sizeof(uint16_t),
		     .target_row_stride   = static_cast<uint64_t>(info.pitch) * sizeof(uint32_t),
		     .source_slice_stride = full_slice_size,
		     .target_slice_stride = transfer_slice});
		linear = promoted;
		for (uint32_t layer = 0; layer < layers; layer++) {
			copies[layer].bufferOffset = transfer_slice * layer;
		}
	}
	upload(copies, linear);
	if (PixelHistoryEnabled()) [[unlikely]] {
		PixelHistoryProbe(m_graphics, m_scheduler, destination, 0, 0,
		                  image.depth_id ? "upload-stencil" : "upload", 0, 0,
		                  fmt::format("from guest 0x{:x}", image.info.data.address), true);
	}
}

void TextureCache::InitializeImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (image.info.data.Empty()) {
		return;
	}
	TrackImage(id);
	if (image.info.metadata.compression != VideoOutCompression::Uncompressed) {
		if (image.IsCpuDirty()) {
			image.RefreshComplete();
		}
		return;
	}
	if (image.info.samples > 1) {
		return;
	}
	const bool upload = image.IsBufferModified() || image.IsCpuDirty();
	if (upload) {
		RecordImageUpload(image.info.data.address, image.info.data.size, image.info.extent.width,
		                  image.info.extent.height, static_cast<uint32_t>(image.info.guest_format),
		                  static_cast<uint32_t>(image.info.tile_mode), image.IsBufferModified());
		const auto [source, source_offset] =
		    m_buffer_cache.ObtainBufferForImage(image.info.data.address, image.info.data.size);
		if (source == nullptr) {
			EXIT("TextureCache: failed to obtain image upload source\n");
		}
		UploadImage(image, *source, source_offset);
		image.ClearBufferModified();
	}
	if (image.IsCpuDirty()) {
		image.RefreshComplete();
	}
}

TextureCache::TargetState TextureCache::CurrentTargetState() const noexcept {
	return {m_image_set_generation.load(std::memory_order_acquire), m_scheduler.CurrentTick(),
	        m_gc_tick};
}

// Whether MaterializeColorClearNow would change nothing, as it did last time for this image: the
// same surface and view, no clear flag registered since (its erase would find none), and for a
// GPU-written check, no GPU-dirty change and no GPU write into checked slices since (the same
// slices stay checked, with the same keys); for a check of metadata the GPU had not written, no
// GPU-dirty change since and the same first key in each slice.
bool TextureCache::ColorClearUnchanged(const Image& image, const ImageDesc& desc,
                                       uint32_t metadata_base_layer) const {
	const auto& check = image.color_clear_check;
	return check.valid && check.metadata == desc.info.metadata &&
	       image.info.metadata == desc.info.metadata && check.view == desc.view_info &&
	       check.resources == desc.info.resources && check.extent == desc.info.extent &&
	       check.image_type == static_cast<uint32_t>(desc.info.type) &&
	       check.metadata_base_layer == metadata_base_layer &&
	       check.binding_type == static_cast<uint8_t>(desc.type) &&
	       check.surface_meta_generation ==
	           m_surface_meta_generation.load(std::memory_order_relaxed) &&
	       (!check.gpu_checked ||
	        (check.gpu_dirty_generation == m_buffer_cache.GpuDirtyGeneration() &&
	         check.dcc_checked_generation ==
	             m_dcc_checked_generation.load(std::memory_order_acquire))) &&
	       (!check.cpu_checked ||
	        (check.gpu_dirty_generation == m_buffer_cache.GpuDirtyGeneration() &&
	         ColorClearKeysUnchanged(check.keys)));
}

bool TextureCache::IsColorClearCurrent(ImageId id, const ImageDesc& desc,
                                       uint32_t metadata_base_layer) const {
	return (desc.info.metadata.kind != ImageMetadataKind::Dcc &&
	        desc.info.metadata.kind != ImageMetadataKind::Cmask) ||
	       ColorClearUnchanged(m_slot_images[id], desc, metadata_base_layer);
}

// Whether each slice's first metadata key still reads as it did (see ColorClearCheck::cpu_checked).
bool TextureCache::ColorClearKeysUnchanged(const Image::ColorClearCheck::Keys& keys) {
	for (uint32_t i = 0; i < keys.count; i++) {
		uint8_t code = 0;
		if (!LibKernel::Memory::TryReadBacking(keys.addresses[i], &code, sizeof(code)) ||
		    code != keys.codes[i]) {
			return false;
		}
	}
	return true;
}

// Most lookups bind a color surface whose metadata was already checked, with nothing to apply:
// skip the check while nothing it depends on has changed (ColorClearUnchanged). Only Thread_Gpu
// changes images and their checks. KYTY_VERIFY_COLOR_CLEAR=1 checks anyway and reports skips that
// would have missed a change; KYTY_DEBUG_AB=colorclear checks every time in alternate windows.
void TextureCache::MaterializeColorClear(ImageId id, const ImageDesc& desc,
                                       uint32_t metadata_base_layer) {
	if (desc.info.metadata.kind != ImageMetadataKind::Dcc &&
	    desc.info.metadata.kind != ImageMetadataKind::Cmask) {
		return;
	}
	static const bool verify = KYTY_GETENV("KYTY_VERIFY_COLOR_CLEAR") != nullptr;
	static const bool ab     = AbSelected("colorclear");
	const bool        skip   = !(ab && AbFeatureOff()) &&
	                  ColorClearUnchanged(m_slot_images[id], desc, metadata_base_layer);
	if (skip && !verify) {
		return;
	}
	bool                         changed = false;
	Image::ColorClearCheck::Keys keys;
	const auto outcome = MaterializeColorClearNow(id, desc, metadata_base_layer, changed, keys);
	if (skip) {
		static std::atomic<uint64_t> checked {0};
		static std::atomic<uint64_t> missed {0};
		const auto count = checked.fetch_add(1, std::memory_order_relaxed) + 1;
		if (changed || outcome == ColorClearOutcome::Other) {
			const auto index = missed.fetch_add(1, std::memory_order_relaxed);
			if (index < 16) {
				std::printf("color-clear verify: skip of 0x%016" PRIx64 " (metadata 0x%016" PRIx64
				            ") would have missed a change (outcome %d, changed %d)\n",
				            m_slot_images[id].info.data.address, desc.info.metadata.range.address,
				            static_cast<int>(outcome), changed ? 1 : 0);
			}
		}
		if (count % 100000 == 0) {
			std::printf("color-clear verify: skips=%" PRIu64 " missed=%" PRIu64 "\n", count,
			            missed.load(std::memory_order_relaxed));
			std::fflush(stdout);
		}
	}
	auto& check = m_slot_images[id].color_clear_check;
	if (outcome == ColorClearOutcome::Other) {
		check.valid = false;
		return;
	}
	check.metadata                = desc.info.metadata;
	check.view                    = desc.view_info;
	check.resources               = desc.info.resources;
	check.extent                  = desc.info.extent;
	check.image_type              = static_cast<uint32_t>(desc.info.type);
	check.metadata_base_layer     = metadata_base_layer;
	check.binding_type            = static_cast<uint8_t>(desc.type);
	check.valid                   = true;
	check.gpu_checked             = outcome == ColorClearOutcome::GpuChecked;
	check.cpu_checked             = outcome == ColorClearOutcome::CpuChecked;
	check.keys                    = keys;
	check.surface_meta_generation = m_surface_meta_generation.load(std::memory_order_relaxed);
	check.gpu_dirty_generation    = m_buffer_cache.GpuDirtyGeneration();
	check.dcc_checked_generation  = m_dcc_checked_generation.load(std::memory_order_acquire);
}

TextureCache::ColorClearOutcome
TextureCache::MaterializeColorClearNow(ImageId id, const ImageDesc& desc,
                                       uint32_t metadata_base_layer, bool& changed,
                                       Image::ColorClearCheck::Keys& keys) {
	keys.count = 0;
	const auto range = desc.info.metadata.range;
	{
		std::scoped_lock lock {m_lock};
		auto& image = m_slot_images[id];
		changed |= !(image.info.metadata == desc.info.metadata);
		image.info.metadata = desc.info.metadata;
		// Native color metadata must not retain a reused HTile/CMask/FMask clear flag.
		if (m_surface_metas.erase(range.address) != 0) {
			changed = true;
			m_surface_meta_generation.fetch_add(1, std::memory_order_release);
		}
		if (DrainStats::Enabled() && range.Valid()) {
			m_dcc_metadata_seen.Add(range.address, range.size);
		}
		if (range.size == 0 || desc.info.resources.levels != 1) {
			return ColorClearOutcome::NoSlices;
		}
	}
	const auto layers = desc.info.TransferLayers();
	// These one-mip surfaces use complete 4 KiB color metadata blocks.
	constexpr uint64_t MetadataBlockSize = 0x1000;
	if (!range.Valid() || range.address % MetadataBlockSize != 0 || layers == 0 ||
	    range.size % layers != 0 || (range.size / layers) % MetadataBlockSize != 0) {
		EXIT("TextureCache: color metadata slices must contain aligned 4 KiB blocks\n");
	}
	const auto& view           = desc.view_info;
	const bool  volume_texture = desc.info.IsVolume() && view.type == vk::ImageViewType::e3D;
	const auto  first          = volume_texture ? 0u : metadata_base_layer;
	const auto  image_first    = volume_texture ? 0u : view.base_layer;
	const auto  count          = volume_texture ? desc.info.extent.depth : view.layer_count;
	if (first >= layers || count > layers - first) {
		EXIT("TextureCache: color view exceeds its native metadata slices\n");
	}
	const auto slice_size  = range.size / layers;
	const bool gpu_written = m_buffer_cache.IsRegionGpuModified(range.address, range.size);
	if (gpu_written) {
		bool no_op = false;
		if (MaterializeDccClearOnGpu(id, desc, range.address + slice_size * first, slice_size,
		                             image_first, count, &no_op)) {
			if (StampTraceEnabled()) [[unlikely]] {
				StampTraceEvent(no_op ? 'g' : 'G', desc.info.data.address);
			}
			return no_op ? ColorClearOutcome::GpuChecked : ColorClearOutcome::Other;
		}
	}
	// Finish native metadata writes before reading backing bytes. This can submit the scheduler,
	// so discovery runs before final draw uploads and never holds the texture lock across it.
	if (gpu_written) {
		DrainStats::ReasonScope reason(DrainStats::Reason::DccClear);
		m_buffer_cache.ReadMemory(range.address, range.size, false);
	}
	uint64_t cleared_slices = 0;
	struct CheckRecord {
		bool      record;
		uint64_t& cleared;
		~CheckRecord() {
			if (record) {
				DrainStats::ReasonScope reason(DrainStats::Reason::DccClear);
				DrainStats::Record(DrainStats::Kind::DccCheck, cleared);
			}
		}
	} check_record {gpu_written, cleared_slices};
	// Metadata the GPU has not written, whose first keys are no clear, gets the same result while
	// those keys stay (see ColorClearCheck::cpu_checked).
	bool settled = !gpu_written && count <= keys.addresses.size();
	for (uint32_t slice = 0; slice < count; slice++) {
		const auto address = range.address + slice_size * (first + slice);
		uint8_t code = 0;
		// Metadata whose backing is not mapped (a descriptor naming freed or unmapped memory) has
		// no clear to materialize: the slice stays as it is instead of ending the session.
		if (!LibKernel::Memory::TryReadBacking(address, &code, sizeof(code))) {
			settled = false;
			static std::atomic<uint32_t> unmapped_logged {0};
			if (unmapped_logged.fetch_add(1, std::memory_order_relaxed) < 8) {
				std::printf("TextureCache: color metadata 0x%" PRIx64 " is not mapped; slice not cleared\n",
				            address);
			}
			continue;
		}
		if (settled) {
			keys.addresses[keys.count] = address;
			keys.codes[keys.count]     = code;
			keys.count++;
		}
		vk::ClearValue clear {};
		if (!DecodeColorClear(desc, code, clear.color)) {
			continue;
		}
		// The slice's other keys decide, and they can change without this one.
		settled = false;
		std::vector<uint8_t> bytes(slice_size);
		if (!LibKernel::Memory::TryReadBacking(address, bytes.data(), bytes.size())) {
			continue;
		}
		if (!std::all_of(bytes.begin(), bytes.end(), [code](uint8_t byte) { return byte == code; })) {
			continue;
		}
		{
			std::scoped_lock lock {m_lock};
			ClearImage(m_scheduler.Current(), id, view.format,
			           {vk::ImageAspectFlagBits::eColor, view.base_level, view.level_count,
			            image_first + slice, 1}, clear);
		}
		if (StampTraceEnabled()) [[unlikely]] {
			StampTraceEvent('C', desc.info.data.address);
		}
		cleared_slices++;
		// Publish the conversion's expanded keys without treating them as guest writes
		// to overlapping image data. Invalidate the buffer before updating its backing.
		if (desc.type != BindingType::VideoOut) {
			std::fill(bytes.begin(), bytes.end(), uint8_t {0xff});
			m_buffer_cache.InvalidateMemory(address, slice_size);
			LibKernel::Memory::WriteBacking(address, bytes.data(), bytes.size());
		}
	}
	if (settled && cleared_slices == 0) {
		return ColorClearOutcome::CpuChecked;
	}
	keys.count = 0;
	return ColorClearOutcome::Other;
}

bool TextureCache::MaterializeDccClearOnGpu(ImageId id, const ImageDesc& desc,
                                            uint64_t slices_address, uint64_t slice_size,
                                            uint32_t image_first, uint32_t count, bool* no_op) {
	if (!m_dcc_gpu_clear || count == 0) {
		return false;
	}
	GpuZones::KeyScope zone_key(m_slot_images[id].info.data.address);
	std::array<vk::ClearColorValue, DccClearResolver::CodeCount> colors {};
	uint32_t                                                     code_mask = 0;
	for (uint32_t index = 0; index < DccClearResolver::CodeCount; index++) {
		if (DecodeColorClear(desc, DccClearResolver::Code(index), colors[index])) {
			code_mask |= 1u << index;
		}
	}
	if (code_mask == 0) {
		// No key clears this view, so the metadata bytes cannot change the image.
		if (no_op != nullptr) {
			*no_op = true;
		}
		return true;
	}
	// A GPU check consumed every clear it found, and no GPU write has touched the slices since,
	// so any binding type (textures, storage, video out) would find no clear to apply either.
	if (DccSlicesChecked(slices_address, slice_size, count, code_mask)) {
		if (no_op != nullptr) {
			*no_op = true;
		}
		return true;
	}
	// Render targets and sampled textures: both consume the keys (as the CPU path does), so each
	// GPU write is checked once. A texture qualifies when its image can already be a color
	// attachment, as a render target the game fast-clears and then samples without drawing to it
	// first: on the CPU path each such lookup drained the GPU (once a frame near Astro Bot's
	// crashed ship, waiting about 25 ms behind the frame's heaviest draw).
	// Storage images consume the keys on the CPU path too: ASTRO's PLAYROOM's hub binds a
	// fast-cleared target as a storage image of an indirect dispatch about four times a frame,
	// and each drained the GPU (11 % of Thread_Gpu). KYTY_DCC_GPU_STORAGE=0 keeps them on the CPU
	// path. KYTY_DEBUG_DCC_FALLBACK=1 prints why a check still takes the CPU path.
	static const bool storage = [] {
		const char* text = KYTY_GETENV("KYTY_DCC_GPU_STORAGE");
		return text == nullptr || std::strcmp(text, "0") != 0;
	}();
	const auto fallback = [&](const char* why) {
		static const bool          enabled = KYTY_GETENV("KYTY_DEBUG_DCC_FALLBACK") != nullptr;
		static std::atomic<uint32_t> printed {0};
		if (enabled && printed.fetch_add(1, std::memory_order_relaxed) < 64) {
			std::printf("dcc-fallback: %s image=0x%016" PRIx64 " type=%u fmt=%u %ux%u layers=%u\n",
			            why, desc.info.data.address, static_cast<uint32_t>(desc.type),
			            static_cast<uint32_t>(desc.view_info.format), desc.info.extent.width,
			            desc.info.extent.height, count);
			std::fflush(stdout);
		}
		return false;
	};
	// The flipped image too: Ghost of Yotei presents a fast-cleared, GPU-written target, and the
	// CPU path read its metadata back at every flip, draining the GPU for 14-20 ms a frame. As
	// on the CPU path the keys of a flipped image are not consumed, so its check repeats on the
	// GPU each flip. KYTY_DCC_GPU_VIDEOOUT=0 keeps flips on the CPU path.
	static const bool video_out = [] {
		const char* text = KYTY_GETENV("KYTY_DCC_GPU_VIDEOOUT");
		return text == nullptr || std::strcmp(text, "0") != 0;
	}();
	const bool  consume = desc.type != BindingType::VideoOut;
	const auto& view    = desc.view_info;
	if (desc.type != BindingType::RenderTarget && desc.type != BindingType::Texture &&
	    !(storage && desc.type == BindingType::Storage) &&
	    !(video_out && desc.type == BindingType::VideoOut)) {
		return fallback("binding type");
	}
	if (desc.info.IsVolume() || count > DccClearResolver::MaxSlices || view.base_level != 0 ||
	    !(m_graphics.GetFormatProperties(view.format).optimalTilingFeatures &
	      vk::FormatFeatureFlagBits::eColorAttachment)) {
		return fallback("view or format");
	}
	{
		std::scoped_lock lock {m_lock};
		const auto&      image = m_slot_images[id];
		if (image.depth_id || image.backing.image == nullptr ||
		    !(image.backing.usage & vk::ImageUsageFlagBits::eColorAttachment) ||
		    image_first >= image.backing.layers || count > image.backing.layers - image_first) {
			return fallback("image usage");
		}
	}
	m_dcc_gpu_checks++;
	{
		DrainStats::ReasonScope reason(DrainStats::Reason::DccClear);
		DrainStats::Record(DrainStats::Kind::DccGpuCheck, count);
	}
	// The check consumes cleared slices on the GPU, so the metadata stays GPU-owned.
	// Preserve a byte-overlapping BC owner and acquire this metadata destination
	// under one explicit texture operation, without recursive preparation.
	std::pair<Buffer*, uint64_t> metadata_source;
	{
		std::scoped_lock lock {m_lock};
		PrepareBlockBufferWriteLocked(slices_address, slice_size * count);
		metadata_source =
		    m_buffer_cache.ObtainBufferForImagePublication(slices_address, slice_size * count);
	}
	const auto [metadata, metadata_offset] = metadata_source;
	auto& command = m_scheduler.Current();
	command.EndRendering();
	const auto native = command.Handle();
	m_dcc_resolver.Record(native, *metadata, metadata_offset, slice_size, count, code_mask, consume);
	{
		std::scoped_lock lock {m_lock};
		auto&            image = m_slot_images[id];
		TrackImage(id);
		if (image.IsBufferModified() || image.IsCpuDirty()) {
			// A predicated clear may not happen, so the image must already hold guest contents.
			InitializeImage(id);
			if (image.info.samples == 1 && (image.IsBufferModified() || image.IsCpuDirty())) {
				EXIT("TextureCache: DCC clear target retained guest ownership\n");
			}
		}
		image.Transit(vk::ImageLayout::eColorAttachmentOptimal,
		              vk::AccessFlagBits2::eColorAttachmentRead |
		                  vk::AccessFlagBits2::eColorAttachmentWrite,
		              {}, native);
		ImageViewInfo attachment_view {};
		attachment_view.format      = view.format;
		attachment_view.type        = count == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray;
		attachment_view.base_layer  = image_first;
		attachment_view.layer_count = count;
		attachment_view.usage       = vk::ImageUsageFlagBits::eColorAttachment;
		vk::RenderingAttachmentInfo attachment {};
		attachment.imageView   = image.FindView(attachment_view);
		attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachment.loadOp      = vk::AttachmentLoadOp::eLoad;
		attachment.storeOp     = vk::AttachmentStoreOp::eStore;
		const vk::Extent2D extent {image.info.extent.width, image.info.extent.height};
		vk::RenderingInfo  rendering {};
		rendering.renderArea.extent    = extent;
		rendering.layerCount           = count;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments    = &attachment;
		native.beginRendering(&rendering);
		for (uint32_t slice = 0; slice < count; slice++) {
			for (uint32_t index = 0; index < DccClearResolver::CodeCount; index++) {
				if ((code_mask & (1u << index)) == 0) {
					continue;
				}
				vk::ConditionalRenderingBeginInfoEXT condition {};
				condition.buffer = m_dcc_resolver.PredicateBuffer();
				condition.offset = DccClearResolver::PredicateOffset(slice, index);
				native.beginConditionalRenderingEXT(&condition);
				const vk::ClearAttachment clear {vk::ImageAspectFlagBits::eColor, 0,
				                                 vk::ClearValue {colors[index]}};
				const vk::ClearRect       rect {vk::Rect2D {{0, 0}, extent}, slice, 1};
				native.clearAttachments(1, &clear, 1, &rect);
				native.endConditionalRenderingEXT();
			}
		}
		native.endRendering();
		// Transit does not separate attachment writes in the same layout; order the clears
		// before the draw that follows.
		vk::MemoryBarrier2 ordered {};
		ordered.srcStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		ordered.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
		ordered.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
		ordered.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
		vk::DependencyInfo dependency {};
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers    = &ordered;
		native.pipelineBarrier2(dependency);
		CommitGpuWrite(image);
	}
	if (consume) {
		MarkDccSlicesChecked(slices_address, slice_size, count, code_mask);
	}
	return true;
}

bool TextureCache::DccSlicesChecked(uint64_t address, uint64_t slice_size, uint32_t count,
                                    uint32_t code_mask) {
	std::scoped_lock lock {m_dcc_checked_mutex};
	for (uint32_t slice = 0; slice < count; slice++) {
		const auto found = m_dcc_checked.find(address + slice_size * slice);
		if (found == m_dcc_checked.end() || found->second.size != slice_size ||
		    (found->second.code_mask & code_mask) != code_mask) {
			return false;
		}
	}
	return true;
}

void TextureCache::MarkDccSlicesChecked(uint64_t address, uint64_t slice_size, uint32_t count,
                                        uint32_t code_mask) {
	std::scoped_lock lock {m_dcc_checked_mutex};
	for (uint32_t slice = 0; slice < count; slice++) {
		m_dcc_checked[address + slice_size * slice] = {slice_size, code_mask};
	}
	m_dcc_checked_generation.fetch_add(1, std::memory_order_release);
}

void TextureCache::OnBufferGpuWrite(uint64_t address, uint64_t size) {
	std::scoped_lock lock {m_dcc_checked_mutex};
	if (m_dcc_checked.empty() || size == 0) {
		return;
	}
	auto entry = m_dcc_checked.lower_bound(address);
	if (entry != m_dcc_checked.begin()) {
		const auto previous = std::prev(entry);
		if (previous->first + previous->second.size > address) {
			entry = previous;
		}
	}
	while (entry != m_dcc_checked.end() && entry->first < address + size) {
		entry = m_dcc_checked.erase(entry);
		m_dcc_checked_generation.fetch_add(1, std::memory_order_release);
	}
}

void TextureCache::RefreshImage(ImageId id) {
	auto& image = m_slot_images[id];
	if (image.depth_id &&
	    (m_slot_images[image.depth_id].info.metadata.stencil_compressed ||
	     m_slot_images[image.depth_id].info.samples != 1)) {
		return;
	}
	TrackImage(id);
	if (image.IsMaybeCpuDirty()) {
		const auto hash = image.HashGuestEdges();
		if (image.NeedsMaybeCpuHash()) {
			image.SetMaybeCpuHash(hash);
			return;
		}
		(void)image.ResolveMaybeCpuHash(hash);
	}
	bool cpu_dirty = image.IsBufferModified() || image.IsDefinitelyCpuDirty();
	if (image.info.metadata.compression != VideoOutCompression::Uncompressed) {
		if (cpu_dirty) {
			EXIT("TextureCache: compressed guest image refresh is unsupported\n");
		}
		return;
	}
	if (!cpu_dirty) {
		return;
	}
	if (GpuZones::Enabled()) [[unlikely]] {
		LogZoneRefresh(image);
	}
	InitializeImage(id);
}

// The stencil plane of `depth_id`, which a color image is about to replace, into the keeper of
// its stencil address (the association record says whether the GPU wrote it: an uploaded stencil
// is still in guest memory). Nothing is kept for a stencil the GPU never wrote.
void TextureCache::KeepStencil(ImageId depth_id) {
	auto&      depth = m_slot_images[depth_id];
	const auto erase = [&] { EraseStencilKeeper(depth.info.stencil.address); };
	if (depth.backing.image == nullptr || depth.backing.samples != 1 || !depth.info.HasStencil()) {
		erase();
		return;
	}
	ImageId association {};
	m_slot_images.ForEach([&](ImageId id, const Image& image) {
		if (image.depth_id == depth_id && image.info.data == depth.info.stencil) {
			association = id;
		}
	});
	// KYTY_STENCIL_KEEP_FIX=1: without an association record that says the GPU wrote the stencil,
	// the whole stencil plane (level 0, all layers) is kept anyway: dropping it left the depth image
	// that replaces the color one with an undefined stencil plane (Ghost of Yotei: 0xff in the
	// interior, which 7c766c91 paints white).
	const char* drop_reason = !association                                       ? "no association record"
	                          : !m_slot_images[association].IsGpuModified()       ? "record not GPU-written"
	                          : m_slot_images[association].stencil_subresources.layer_count == 0 ? "no stencil layers"
	                                                                             : nullptr;
	if (drop_reason != nullptr) {
		if (StencilSourceLog()) [[unlikely]] {
			static std::map<std::string, uint64_t> logged;
			const auto key = fmt::format("{:x} {}", depth.info.data.address, drop_reason);
			if (const auto count = ++logged[key]; count <= 3 || count % 1000 == 0) {
				std::printf("\x1b[91mstencil-source: keep of depth 0x%llx (stencil 0x%llx): %s, %s (#%llu)\x1b[0m\n",
				            static_cast<unsigned long long>(depth.info.data.address),
				            static_cast<unsigned long long>(depth.info.stencil.address), drop_reason,
				            StencilKeepFix() ? "keeping the whole plane" : "nothing kept",
				            static_cast<unsigned long long>(count));
				std::fflush(stdout);
			}
		}
		if (!StencilKeepFix()) {
			erase();
			return;
		}
	}
	const ImageSubresourceRange kept = drop_reason == nullptr
	                                       ? m_slot_images[association].stencil_subresources
	                                       : ImageSubresourceRange {0, 1, 0, depth.info.resources.layers};
	auto info      = depth.info;
	info.resources = {kept.level_count, kept.layer_count};
	if (info.IsTiled()) {
		info.pitch = TileGetDepthPitch(info.extent.width, 1, 0);
	}
	const uint64_t slice = static_cast<uint64_t>(info.pitch) * info.extent.height;
	const uint64_t size  = slice * info.resources.layers;
	if (info.pitch < info.extent.width || size == 0 ||
	    kept.base_layer + info.resources.layers > depth.info.resources.layers) {
		erase();
		return;
	}
	auto copies = BuildDepthCopies(info, slice, vk::ImageAspectFlagBits::eStencil);
	for (auto& copy: copies) {
		copy.imageSubresource.baseArrayLayer += kept.base_layer;
	}
	auto& keeper = m_stencil_keepers[depth.info.stencil.address];
	if (keeper.buffer == nullptr || keeper.size < size) {
		if (keeper.buffer != nullptr) {
			// Recorded commands may still read the old buffer.
			RetireStencilBuffer(std::move(keeper.buffer));
		}
		keeper.buffer = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::DeviceLocal, 0,
		                                         AllFlags, size);
	}
	keeper.extent = info.extent;
	keeper.layers = info.resources.layers;
	keeper.pitch  = info.pitch;
	keeper.size   = size;
	depth.Download(copies, keeper.buffer->Handle(), 0, size, keeper.buffer->Size());
	if (PixelHistoryEnabled()) [[unlikely]] {
		// The value kept: the depth image's stencil plane as it is copied out.
		PixelHistoryProbe(m_graphics, m_scheduler, depth, 0, kept.base_layer,
		                  "keep-stencil", 0, 0, "copied to keeper", true);
	}
}

bool TextureCache::StencilKeepFix() {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_STENCIL_KEEP_FIX");
		return text != nullptr && std::strcmp(text, "0") != 0;
	}();
	return enabled;
}

bool TextureCache::StencilSourceLog() {
	static const bool enabled = KYTY_GETENV("KYTY_STENCIL_FROM_R8") != nullptr ||
	                            KYTY_GETENV("KYTY_STENCIL_KEEP_FIX") != nullptr ||
	                            KYTY_GETENV("KYTY_DEBUG_STENCIL_SOURCE") != nullptr;
	return enabled;
}

void TextureCache::RefreshKeeperFromStencilTexture(const Image& depth) {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_STENCIL_FROM_R8");
		return text != nullptr && std::strcmp(text, "0") != 0;
	}();
	if (!enabled) {
		return;
	}
	const auto address = depth.info.stencil.address;
	const auto found   = m_stencil_keepers.find(address);
	for (const auto id: FindImagesInRegion(address, 1, false)) {
		auto* texture = m_slot_images.try_get(id);
		if (texture == nullptr || texture->depth_id || texture->info.IsDepth() ||
		    texture->info.data.address != address || texture->backing.image == nullptr ||
		    texture->backing.samples != 1 || vk::blockSize(texture->backing.format) != 1 ||
		    texture->info.extent.width != depth.info.extent.width ||
		    texture->info.extent.height != depth.info.extent.height || !texture->IsGpuModified() ||
		    texture->IsBufferModified() || texture->IsCpuDirty()) {
			continue;
		}
		auto info      = texture->info;
		info.resources = {1, std::min(texture->backing.layers, depth.info.resources.layers)};
		info.pitch     = found != m_stencil_keepers.end() ? found->second.pitch : info.extent.width;
		if (info.pitch < info.extent.width) {
			info.pitch = info.extent.width;
		}
		const uint64_t slice = static_cast<uint64_t>(info.pitch) * info.extent.height;
		const uint64_t size  = slice * info.resources.layers;
		auto&          keeper = m_stencil_keepers[address];
		if (keeper.buffer == nullptr || keeper.size < size) {
			if (keeper.buffer != nullptr) {
				RetireStencilBuffer(std::move(keeper.buffer));
			}
			keeper.buffer = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::DeviceLocal, 0,
			                                         AllFlags, size);
		}
		keeper.extent = info.extent;
		keeper.layers = info.resources.layers;
		keeper.pitch  = info.pitch;
		keeper.size   = size;
		texture->Download(BuildDepthCopies(info, slice, vk::ImageAspectFlagBits::eColor),
		                  keeper.buffer->Handle(), 0, size, keeper.buffer->Size());
		static std::map<uint64_t, uint64_t> logged;
		if (const auto count = ++logged[address]; count <= 3 || count % 1000 == 0) {
			std::printf("\x1b[91mstencil-source: depth 0x%llx takes its stencil from the 8-bit image 0x%llx "
			            "(%ux%u) (#%llu)\x1b[0m\n",
			            static_cast<unsigned long long>(depth.info.data.address),
			            static_cast<unsigned long long>(address), info.extent.width, info.extent.height,
			            static_cast<unsigned long long>(count));
			std::fflush(stdout);
		}
		return;
	}
}

// A keeper's buffer is the source of RestoreStencil's upload and the target of KeepStencil's
// download, both recorded as deferred copies that the record thread replays later. Destroying it
// with the keeper made those copies read a destroyed VkBuffer ("deferred-call: stale buffer
// source ... vkCmdCopyBufferToImage", guest=0x0, the size of a stencil plane). It is released
// only after the tick then being recorded completes, deferred twice like the tiler's scratch
// buffers so a ring wrap between the copy and the erase cannot shorten its life.
void TextureCache::RetireStencilBuffer(std::unique_ptr<Buffer> buffer) {
	if (buffer == nullptr) {
		return;
	}
	if (!m_scheduler.Active()) {
		buffer.reset();
		return;
	}
	m_scheduler.DeferOperation([this, old = std::move(buffer)]() mutable {
		m_scheduler.DeferOperation([last = std::move(old)]() mutable { last.reset(); });
	});
}

void TextureCache::EraseStencilKeeper(uint64_t address) {
	const auto found = m_stencil_keepers.find(address);
	if (found == m_stencil_keepers.end()) {
		return;
	}
	RetireStencilBuffer(std::move(found->second.buffer));
	m_stencil_keepers.erase(found);
}

// The kept stencil into `image`: its stencil plane (`stencil_aspect`, a depth image replacing a
// color one) or its color plane (the 8-bit texture a shader reads the stencil through while the
// depth image is a color image). The keeper stays until a depth image's association consumes it.
bool TextureCache::RestoreStencil(Image& image, bool stencil_aspect) {
	const auto address = stencil_aspect ? image.info.stencil.address : image.info.data.address;
	const auto found   = m_stencil_keepers.find(address);
	if (found == m_stencil_keepers.end() || image.backing.image == nullptr ||
	    image.backing.samples != 1) {
		return false;
	}
	const auto& keeper = found->second;
	if (keeper.extent.width != image.info.extent.width ||
	    keeper.extent.height != image.info.extent.height ||
	    keeper.layers > image.info.resources.layers || image.info.IsVolume()) {
		return false;
	}
	if (stencil_aspect ? !image.info.IsDepth() || !image.info.HasStencil()
	                   : image.info.IsDepth() || image.info.bytes_per_block != 1 ||
	                         image.info.resources.levels != 1 ||
	                         vk::blockSize(image.backing.format) != 1) {
		return false;
	}
	auto info      = image.info;
	info.pitch     = keeper.pitch;
	info.resources = {1, keeper.layers};
	const auto copies =
	    BuildDepthCopies(info, static_cast<uint64_t>(keeper.pitch) * keeper.extent.height,
	                     stencil_aspect ? vk::ImageAspectFlagBits::eStencil
	                                    : vk::ImageAspectFlagBits::eColor);
	image.Upload(copies, keeper.buffer->Handle(), 0, keeper.size, keeper.buffer->Size());
	if (PixelHistoryEnabled()) [[unlikely]] {
		PixelHistoryProbe(m_graphics, m_scheduler, image, 0, 0, "restore-stencil", 0, 0,
		                  stencil_aspect ? "into depth stencil plane" : "into 8-bit texture", true);
	}
	return true;
}

ImageId TextureCache::AssociateStencil(ImageId depth_id, GuestRange stencil) {
	if (!stencil.Valid()) {
		EXIT("TextureCache: invalid stencil association range\n");
	}
	auto& depth = m_slot_images[depth_id];
	if (!depth.info.IsDepth() || !depth.info.HasStencil()) {
		EXIT("TextureCache: stencil association requires a depth/stencil image\n");
	}

	ImageId association {};
	for (const auto id: FindImagesInRegion(stencil.address, stencil.size, false)) {
		const auto owner = m_slot_images.try_get(id);
		if (owner != nullptr && owner->info.data == stencil &&
		    owner->info.extent == depth.info.extent) {
			association = id;
		}
	}
	if (!association) {
		ImageInfo info {};
		info.data   = stencil;
		info.extent = depth.info.extent;
		association = InsertImage(info);
	}
	auto& record = m_slot_images[association];
	TouchImage(record);
	record.depth_id             = depth_id;
	record.stencil_subresources = depth.stencil_subresources;
	if (depth.stencil_restored) {
		// The stencil plane came from the keeper (RestoreStencil), not from guest memory: the
		// record must not upload the stale guest copy over it.
		depth.stencil_restored = false;
		if (record.IsCpuDirty()) {
			record.RefreshComplete();
		}
		record.ClearBufferModified();
		MarkGpuModified(record);
		EraseStencilKeeper(stencil.address);
	}
	BumpImageCleanEpoch();
	return association;
}

ImageId TextureCache::FindImage(ImageDesc& desc, bool exact_format, uint64_t* unique_generation) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::FindImage);
	auto& command = m_scheduler.Current();
	if (command.IsInvalid()) {
		EXIT("TextureCache: image lookup requires a valid command buffer\n");
	}
	if (unique_generation != nullptr) {
		*unique_generation = 0;
	}
	m_last_find_overlap = false;
	ValidateImageDesc(desc);
	if (desc.info.data.Empty()) {
		std::scoped_lock lock {m_lock};
		return GetNullImage(desc);
	}
	const auto metadata_base_layer = desc.view_info.base_layer;

	ImageId result {};
	{
		std::unique_lock lock {m_lock};
		int32_t view_mip        = -1;
		int32_t view_layer      = -1;
		int32_t backing_matches = 0;
		ImageId only_candidate {};
		const auto       lookup     = [&] {
			result          = {};
			view_mip        = -1;
			view_layer      = -1;
			backing_matches = 0;
			const auto candidates =
			    FindImagesInRegion(desc.info.data.address, desc.info.data.size, false);
			bool block_alias = false;
			only_candidate = candidates.size() == 1 ? candidates[0] : ImageId {};
			for (const auto id: candidates) {
				const auto& image = m_slot_images[id];
				if (SameBacking(image.info, desc.info, exact_format)) {
					result = id;
					backing_matches++;
				} else if (image.SafeToDownload() && IsBlockAlias(desc.info, image.info)) {
					block_alias = true;
				}
			}
			if (!result) {
				for (const auto candidate: candidates) {
					view_mip                = -1;
					view_layer              = -1;
					const auto& merged_info = result ? m_slot_images[result].info : desc.info;
					const auto  overlap = ResolveOverlap(merged_info, desc.type, candidate, result);
					if (overlap.image) {
						result     = overlap.image;
						view_mip   = overlap.mip;
						view_layer = overlap.layer;
					}
				}
			}
			if (result && block_alias) {
				for (const auto candidate: candidates) {
					const auto* owner = m_slot_images.try_get(candidate);
					if (candidate != result && owner != nullptr && owner->registered &&
					    !owner->IsCpuDirty() && IsBlockAlias(m_slot_images[result].info, owner->info)) {
						ResolveBlockAlias(candidate, result);
					}
				}
			}
			if (result) {
				auto& resolved = m_slot_images[result];
				if (exact_format && resolved.info.pixel_format != desc.info.pixel_format) {
					result = {};
				} else if (resolved.info.resources < desc.info.resources) {
					ChurnReason reason("find-grow");
					FreeImage(result);
					result = {};
				}
			}
		};
		// Draws look up the same textures again and again; see ImageLookup.
		static const bool memo_enabled = [] {
			const char* text = KYTY_GETENV("KYTY_DEBUG_IMAGE_MEMO");
			return text == nullptr || std::strcmp(text, "0") != 0;
		}();
		auto* const  lookups          = ImageLookupSet(desc.info, exact_format);
		const auto   range_generation = RangeGeneration(desc.info.data);
		ImageLookup* remembered       = nullptr;
		for (size_t way = 0; memo_enabled && way < ImageLookupWays; way++) {
			if (lookups[way].generation == range_generation &&
			    lookups[way].Matches(desc.info, exact_format)) {
				remembered = &lookups[way];
				break;
			}
		}
		// KYTY_VERIFY_IMAGE_MEMO=1: a remembered lookup is looked up again and must agree.
		static const bool verify_memo = KYTY_GETENV("KYTY_VERIFY_IMAGE_MEMO") != nullptr;
		if (remembered != nullptr && verify_memo) [[unlikely]] {
			const auto remembered_id = remembered->id;
			lookup();
			const bool same = result == remembered_id && backing_matches == 1 && view_mip < 0 &&
			                  view_layer < 0;
			static std::atomic<uint64_t> checked {0};
			static std::atomic<uint64_t> missed {0};
			const auto count = checked.fetch_add(1, std::memory_order_relaxed) + 1;
			if (!same && missed.fetch_add(1, std::memory_order_relaxed) < 32) {
				std::printf("image-memo verify: 0x%016" PRIx64 " size 0x%" PRIx64
				            " remembered a different lookup (matches %d, mip %d, layer %d)\n",
				            desc.info.data.address, desc.info.data.size, backing_matches, view_mip,
				            view_layer);
			}
			if (count % 100000 == 0) {
				std::printf("image-memo verify: hits=%" PRIu64 " missed=%" PRIu64 "\n", count,
				            missed.load(std::memory_order_relaxed));
				std::fflush(stdout);
			}
			// Go on with the lookup's own result, as without the memo.
			remembered = nullptr;
			if (!same) {
				result = {};
			}
		}
		// KYTY_DEBUG_FIND_STATS=1: how lookups that ask for a generation (targets) end, every 5 s.
		static const bool find_stats = KYTY_GETENV("KYTY_DEBUG_FIND_STATS") != nullptr;
		const auto        note_find  = [&](uint32_t outcome) {
			if (!find_stats || unique_generation == nullptr) [[likely]] {
				return;
			}
			static constexpr std::array<const char*, 6> Names {"memo", "unique", "multi", "view",
			                                                   "overlap", "inserted"};
			static std::array<uint64_t, Names.size()> counts {};
			static auto start = std::chrono::steady_clock::now();
			counts[outcome]++;
			if (std::chrono::steady_clock::now() - start >= std::chrono::seconds(5)) {
				start          = std::chrono::steady_clock::now();
				uint64_t total = 0;
				for (const auto n: counts) {
					total += n;
				}
				std::string line = fmt::format("find-stats: lookups={}", total);
				for (size_t i = 0; i < Names.size(); i++) {
					line += fmt::format(" {}%={:.1f}", Names[i],
					                    100.0 * static_cast<double>(counts[i]) / static_cast<double>(total));
				}
				std::printf("%s\n", line.c_str());
				std::fflush(stdout);
				counts.fill(0);
			}
		};
		if (remembered != nullptr) {
			note_find(0);
			remembered->last_use = ++m_image_lookup_clock;
			result               = remembered->id;
			if (unique_generation != nullptr) {
				*unique_generation = range_generation;
			}
		} else {
			lookup();
			note_find(!result               ? 5
			          : backing_matches > 1 ? 2
			          : backing_matches == 1 ? (view_mip < 0 && view_layer < 0 ? 1 : 3)
			                                 : 4);
			if (!result && m_last_pressure_gc_tick != m_gc_tick &&
			    m_graphics.CanReportMemoryUsage() &&
			    m_graphics.GetDeviceMemoryUsage() >= m_pressure_gc_memory) {
				// At most once per submission age: image misses must not age live resources or
				// repeatedly drain a working set that cannot be reclaimed losslessly.
				m_last_pressure_gc_tick = m_gc_tick;
				lock.unlock();
				(void)CollectGarbage(true);
				lock.lock();
				// Collection invalidates aliases and can retire the source of this lookup.
				lookup();
			}
			if (!result) {
				ChurnReason reason("find-miss");
				result         = InsertImage(desc.info);
				auto& inserted = m_slot_images[result];
				if (m_buffer_cache.HasGpuDirtyBytes(inserted.info.data.address,
				                                    inserted.info.data.size)) {
					inserted.MarkBufferModified();
				}
				if (!m_stencil_keepers.empty() && RestoreStencil(inserted, false)) {
					// A shader reads a kept stencil through this texture: current from the keeper,
					// not from guest memory.
					if (inserted.IsCpuDirty()) {
						inserted.RefreshComplete();
					}
					inserted.ClearBufferModified();
					MarkGpuModified(inserted);
				}
			} else if (backing_matches == 1 && view_mip < 0 && view_layer < 0) {
				// The lookup can have registered or freed images: take the range's generation now.
				const auto found_generation = RangeGeneration(desc.info.data);
				auto*      victim           = lookups;
				for (size_t way = 0; way < ImageLookupWays; way++) {
					if (lookups[way].generation != RangeGeneration(lookups[way].data)) {
						victim = &lookups[way];
						break;
					}
					if (lookups[way].last_use < victim->last_use) {
						victim = &lookups[way];
					}
				}
				*victim = {.generation      = found_generation,
				           .last_use        = ++m_image_lookup_clock,
				           .data            = desc.info.data,
				           .extent          = desc.info.extent,
				           .resources       = desc.info.resources,
				           .samples         = desc.info.samples,
				           .bytes_per_block = desc.info.bytes_per_block,
				           .tile_mode       = desc.info.tile_mode,
				           .pixel_format    = desc.info.pixel_format,
				           .type            = desc.info.type,
				           .exact_format    = exact_format,
				           .id              = result};
				if (unique_generation != nullptr) {
					*unique_generation = found_generation;
				}
			} else if (backing_matches == 0 && unique_generation != nullptr && result == only_candidate &&
			           (desc.type == BindingType::RenderTarget || desc.type == BindingType::DepthTarget) &&
			           OverlapTargetsEnabled() && RangeGeneration(desc.info.data) == range_generation) {
				// The request overlapped the one image over its range, which ResolveOverlap kept (a
				// depth target over a texture's image, a layer or level of it, a compatible view)
				// without registering or freeing an image: those outcomes depend only on the two
				// images, so the same request resolves the same way, to the view now in desc, until
				// an image over the range comes or goes.
				*unique_generation  = range_generation;
				m_last_find_overlap = true;
			}
		}
		auto& image = m_slot_images[result];
		if (view_mip >= 0) {
			desc.view_info.base_level = static_cast<uint32_t>(view_mip);
		}
		if (view_layer >= 0) {
			desc.view_info.base_layer = static_cast<uint32_t>(view_layer);
		}
		image.tick_accessed_last = m_scheduler.CurrentTick();
		TouchImage(image);
	}
	FinishFind(result, desc, metadata_base_layer);
	return result;
}

bool TextureCache::OverlapTargetsEnabled() noexcept {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_OVERLAP_TARGETS");
		return text == nullptr || std::strcmp(text, "0") != 0;
	}();
	static const bool ab = AbSelected("overlap");
	return enabled && !(ab && AbFeatureOff());
}

// KYTY_VERIFY_OVERLAP_TARGETS=1: a target taken again from an overlap lookup (see LastFindOverlap)
// is looked up in full as well, which must find the same image and view.
void TextureCache::VerifyOverlapRefind(ImageId id, const ImageDesc& desc, bool exact_format) {
	static const bool verify = KYTY_GETENV("KYTY_VERIFY_OVERLAP_TARGETS") != nullptr;
	if (!verify) [[likely]] {
		return;
	}
	auto       check = desc;
	const auto found = FindImage(check, exact_format);
	static uint64_t checked = 0;
	static uint64_t differ  = 0;
	checked++;
	if (found != id || !(check.view_info == desc.view_info)) {
		if (differ++ < 16) {
			std::printf("overlap-targets verify: 0x%016" PRIx64 " %ux%u kept image %u, lookup finds %u%s\n",
			            desc.info.data.address, desc.info.extent.width, desc.info.extent.height,
			            static_cast<uint32_t>(id.index), static_cast<uint32_t>(found.index),
			            found == id ? " with another view" : "");
		}
	}
	if (checked % 200000 == 0) {
		std::printf("overlap-targets verify: checked=%" PRIu64 " differ=%" PRIu64 "\n", checked, differ);
		std::fflush(stdout);
	}
}

void TextureCache::MarkImagesAccessed(std::span<const ImageId> ids) {
	const auto                       tick = m_scheduler.CurrentTick();
	std::unique_lock<TrackingSpinLock> lock {m_lock, std::defer_lock};
	for (const auto id: ids) {
		auto& image = m_slot_images[id];
		if (image.tick_accessed_last != tick || (image.registered && image.lru_tick != m_gc_tick)) {
			if (!lock.owns_lock()) {
				lock.lock();
			}
			image.tick_accessed_last = tick;
			TouchImage(image);
		}
	}
}

bool TextureCache::RefindImage(ImageId id, uint64_t generation, const ImageDesc& desc,
                               uint32_t metadata_base_layer) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::RefindImage);
	// Only Thread_Gpu (the caller) registers, frees and touches images, so when this tick and GC
	// tick already saw the image, the bookkeeping below would change nothing: skip the lock.
	if (generation == 0 || generation != RangeGeneration(desc.info.data)) {
		return false;
	}
	const auto tick  = m_scheduler.CurrentTick();
	auto&      image = m_slot_images[id];
	if (image.tick_accessed_last != tick || (image.registered && image.lru_tick != m_gc_tick)) {
		std::scoped_lock lock {m_lock};
		if (generation != RangeGeneration(desc.info.data)) {
			return false;
		}
		image.tick_accessed_last = tick;
		TouchImage(image);
	}
	FinishFind(id, desc, metadata_base_layer);
	return true;
}

bool TextureCache::IsTextureCurrent(ImageId id, uint64_t generation) const noexcept {
	// The image may have been freed since; a slot reused by another image has a newer generation.
	const auto* owner = m_slot_images.try_get(id);
	if (owner == nullptr || generation == 0 || generation != RangeGeneration(owner->info.data)) {
		return false;
	}
	return IsTextureClean(id);
}

bool TextureCache::IsTextureClean(ImageId id) const noexcept {
	const auto& image = m_slot_images[id];
	// KYTY_IMAGE_RESIZE_SYNC: an image with a sibling of another size goes through Find* on every
	// use, where SyncResizedImages copies the sibling's newer content in.
	if (image.resize_siblings) {
		return false;
	}
	// FindTexture's RefreshImage and stencil refresh would do nothing: the image is tracked over
	// its whole range and neither CPU nor buffer writes made it dirty.
	return image.registered && !image.depth_id && !image.binding.needs_rebind &&
	       !image.info.data.Empty() && !image.info.HasStencil() && !image.IsCpuDirty() &&
	       !image.IsBufferModified() && image.track_addr == image.info.data.address &&
	       image.track_addr_end == image.info.data.End();
}

bool TextureCache::IsRenderTargetCurrent(ImageId id, uint64_t generation) const noexcept {
	if (!IsTextureCurrent(id, generation)) {
		return false;
	}
	// FindRenderTarget's MarkGpuModified, CommitGpuWrite and usage flag would change nothing, and
	// TrackImageDownload enrolls nothing (linear readback images re-enroll every acquisition).
	const auto& image = m_slot_images[id];
	return image.IsGpuModified() && image.usage.render_target && image.backing.image != nullptr &&
	       !(m_readback_linear_images && !image.info.IsTiled());
}

bool TextureCache::IsStorageCurrent(ImageId id, uint64_t generation) const noexcept {
	if (!IsTextureCurrent(id, generation)) {
		return false;
	}
	// FindTexture's MarkGpuModified and CommitGpuWrite would change nothing, and TrackImageDownload
	// enrolls nothing (linear readback images re-enroll every acquisition).
	const auto& image = m_slot_images[id];
	return image.IsGpuModified() && image.backing.image != nullptr &&
	       !(m_readback_linear_images && !image.info.IsTiled());
}

bool TextureCache::IsDepthTargetCurrent(ImageId id, uint64_t generation, uint64_t meta_generation,
                                        const ImageDesc& desc) const noexcept {
	// A stencil request associates the stencil image, which IsTextureCurrent does not cover.
	if (desc.info.HasStencil() || !IsTextureCurrent(id, generation) ||
	    meta_generation != m_surface_meta_generation.load(std::memory_order_acquire)) {
		return false;
	}
	// FindDepthTarget's MarkGpuModified, CommitGpuWrite, usage flag and stencil and metadata
	// assignments would change nothing, and the metadata entry it made is still there.
	const auto& image = m_slot_images[id];
	return image.IsGpuModified() && image.usage.depth_target && image.backing.image != nullptr &&
	       image.info.stencil == desc.info.stencil && image.info.metadata == desc.info.metadata;
}

// The part of FindImage after the lookup that runs without the lock.
void TextureCache::FinishFind(ImageId id, const ImageDesc& desc, uint32_t metadata_base_layer) {
	DrawPhaseTimer::ProbeScope probe(g_draw_phases, DrawPhaseTimer::FindFinish);
	MaterializeColorClear(id, desc, metadata_base_layer);
	if (desc.type == BindingType::VideoOut &&
	    desc.info.metadata.compression != VideoOutCompression::Uncompressed) {
		std::scoped_lock lock {m_lock};
		const auto& image = m_slot_images[id];
		const bool guest_dirty = image.IsBufferModified() || image.IsCpuDirty();
		const bool native_current =
		    (image.usage.render_target || image.IsGpuModified()) && !guest_dirty;
		if (!native_current) {
			EXIT("TextureCache: compressed video-out read requires clean native GPU "
			     "contents\n");
		}
	}
}

void TextureCache::UpdateImage(ImageId id) {
	std::scoped_lock lock {m_lock};
	auto&            image = m_slot_images[id];
	TouchImage(image);
	RefreshImage(id);
}

ImageId TextureCache::FindImageFromRange(uint64_t address, uint64_t size, bool ensure_valid) {
	if (!GuestRange {address, size}.Valid()) {
		return {};
	}
	std::scoped_lock lock {m_lock};
	ImageIds         matches;
	for (const auto id: FindImagesInRegion(address, size, false)) {
		auto owner = m_slot_images.try_get(id);
		if (owner == nullptr || owner->info.data.address != address) {
			continue;
		}
		if (ensure_valid && owner->depth_id) {
			owner = m_slot_images.try_get(owner->depth_id);
		}
		if (owner == nullptr || (ensure_valid && !owner->SafeToDownload())) {
			continue;
		}
		matches.push_back(id);
	}
	ImageId selected {};
	if (matches.size() == 1) {
		selected = matches.front();
	} else {
		for (const auto id: matches) {
			const auto& image = m_slot_images[id];
			if (image.info.data.size == size) {
				selected = id;
				break;
			}
		}
	}
	if (selected && ensure_valid) {
		const auto owner = m_slot_images.try_get(selected);
		if (owner != nullptr && owner->depth_id) {
			selected = owner->depth_id;
		}
	}
	return selected;
}

vk::ImageView TextureCache::FindTexture(ImageId id, const ImageDesc& desc) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::FindTexture);
	RefreshVia       via(desc.type == BindingType::Storage ? "storage" : "texture");
	std::scoped_lock lock {m_lock};
	auto&            image = m_slot_images[id];
	TouchImage(image);
	if (!image.info.data.Empty()) {
		if (!image.registered || image.depth_id || image.binding.needs_rebind) {
			EXIT("TextureCache: texture requires rediscovery before final acquisition\n");
		}
	}
	if (desc.type == BindingType::Storage) {
		MarkGpuModified(image);
	}
	if (!image.info.data.Empty()) {
		RefreshImage(id);
		if (image.info.HasStencil() &&
		    desc.info.data.address >= image.info.stencil.address &&
		    desc.info.data.End() <= image.info.stencil.End()) {
			for (const auto stencil_id:
			     FindImagesInRegion(image.info.stencil.address, image.info.stencil.size, false)) {
				const auto* stencil = m_slot_images.try_get(stencil_id);
				if (stencil != nullptr && stencil->depth_id == id &&
				    stencil->info.data == image.info.stencil) {
					RefreshImage(stencil_id);
					break;
				}
			}
		}
	}
	if (!image.info.data.Empty()) {
		SyncResizedImages(id, desc.type == BindingType::Storage);
	}
	switch (desc.type) {
		case BindingType::Texture: break;
		case BindingType::Storage:
			if (!image.info.data.Empty()) {
				CommitGpuWrite(image);
			}
			TrackImageDownload(id, image);
			break;
		default: EXIT("TextureCache: invalid texture binding\n");
	}
	return image.FindView(desc.view_info);
}

vk::ImageView TextureCache::FindRenderTarget(ImageId id, const ImageDesc& desc) {
	if (desc.type != BindingType::RenderTarget) {
		EXIT("TextureCache: invalid color-target binding\n");
	}
	RefreshVia       via("render-target");
	std::scoped_lock lock {m_lock};
	auto&            image = m_slot_images[id];
	if (!image.registered || image.depth_id || image.binding.needs_rebind) {
		EXIT("TextureCache: color target requires rediscovery before final acquisition\n");
	}
	TouchImage(image);
	MarkGpuModified(image);
	image.usage.render_target = true;
	RefreshImage(id);
	SyncResizedImages(id, true);
	CommitGpuWrite(image);
	TrackImageDownload(id, image);
	return image.FindView(desc.view_info);
}

vk::ImageView TextureCache::FindDepthTarget(ImageId id, const ImageDesc& desc) {
	if (desc.type != BindingType::DepthTarget) {
		EXIT("TextureCache: invalid depth-target binding\n");
	}
	RefreshVia       via("depth-target");
	std::scoped_lock lock {m_lock};
	auto&            image = m_slot_images[id];
	if (!image.registered || image.depth_id || image.binding.needs_rebind) {
		EXIT("TextureCache: depth target requires rediscovery before final acquisition\n");
	}
	TouchImage(image);
	MarkGpuModified(image);
	image.usage.depth_target = true;
	if (desc.info.HasStencil()) {
		const auto layers = image.info.resources.layers;
		EXIT_IF(layers == 0 || image.info.data.size % layers != 0 ||
		        desc.info.data.address < image.info.data.address);
		const auto slice_size = image.info.data.size / layers;
		const auto offset     = desc.info.data.address - image.info.data.address;
		EXIT_IF(slice_size == 0 || offset % slice_size != 0 || offset / slice_size >= layers ||
		        desc.info.resources.layers > layers - offset / slice_size);
		image.stencil_subresources = {0, desc.info.resources.levels,
		                             static_cast<uint32_t>(offset / slice_size),
		                             desc.info.resources.layers};
	}
	image.info.stencil = desc.info.stencil;
	BumpImageCleanEpoch();
	image.info.metadata = desc.info.metadata;
	if (desc.info.HasMetadata()) {
		const auto layers = desc.info.resources.layers;
		const auto range  = desc.info.metadata.range;
		EXIT_IF(desc.info.metadata.kind != ImageMetadataKind::Htile || !range.Valid() ||
		        !desc.info.data.Valid() || layers == 0 || layers > image.info.resources.layers ||
		        range.size % layers != 0 ||
		        desc.info.data.size % layers != 0);
		const auto metadata_layer_size = range.size / layers;
		const auto depth_layer_size    = desc.info.data.size / layers;
		const auto low_mask = [](uint32_t count) {
			return count >= 32 ? UINT32_MAX : count == 0 ? 0u : (1u << count) - 1u;
		};
		const auto [meta, inserted] = m_surface_metas.emplace(
		    range.address, MetaDataInfo {.type = MetaDataInfo::Type::HTile});
		auto& state = meta->second;
		const vk::Extent2D depth_extent {desc.info.extent.width, desc.info.extent.height};
		const bool same_layout = !inserted && state.type == MetaDataInfo::Type::HTile &&
		                         state.owner_address == desc.info.data.address &&
		                         state.metadata_layer_size == metadata_layer_size &&
		                         state.depth_layer_size == depth_layer_size &&
		                         state.depth_extent == depth_extent &&
		                         state.depth_samples == desc.info.samples &&
		                         state.depth_format == desc.info.guest_format;
		const bool geometry_changed = !same_layout || layers > state.layer_count;
		if (!same_layout) {
			state.type             = MetaDataInfo::Type::HTile;
			state.layer_count      = layers;
			state.clear_mask       = image.info.htile_clear_mask & low_mask(layers);
			state.high_clear_masks.assign((static_cast<size_t>(layers) - 1) / 32,
			                              image.info.htile_clear_mask == UINT32_MAX ? UINT32_MAX : 0u);
			if (layers % 32 != 0 && !state.high_clear_masks.empty()) {
				state.high_clear_masks.back() &= (1u << (layers % 32)) - 1u;
			}
		} else if (layers > state.layer_count) {
			// A smaller view must not discard the known allocation. Growth preserves consumed
			// slices and initializes only newly registered slices from the image's initial state.
			state.high_clear_masks.resize((static_cast<size_t>(layers) - 1) / 32, 0u);
			for (uint32_t slice = state.layer_count; slice < layers; ++slice) {
				const auto seed = image.info.htile_clear_mask;
				if (seed == UINT32_MAX || (slice < 32 && (seed & (1u << slice)) != 0)) {
					auto& mask = slice < 32 ? state.clear_mask : state.high_clear_masks[slice / 32 - 1];
					mask |= 1u << (slice % 32);
				}
			}
			state.layer_count = layers;
		}
		state.owner_address       = desc.info.data.address;
		state.metadata_layer_size = metadata_layer_size;
		state.depth_layer_size    = depth_layer_size;
		state.depth_extent        = depth_extent;
		state.depth_samples       = desc.info.samples;
		state.depth_format        = desc.info.guest_format;
		if (geometry_changed) {
			m_surface_meta_generation.fetch_add(1, std::memory_order_release);
		}
		if (desc.info.metadata.kind == ImageMetadataKind::Htile) {
			MarkHtileStale(desc, id);
		}
	}
	RefreshImage(id);
	SyncResizedImages(id, true);
	CommitGpuWrite(image);
	if (desc.info.HasStencil()) {
		RefreshImage(AssociateStencil(id, desc.info.stencil));
	}
	if (PixelHistoryEnabled()) [[unlikely]] {
		// The depth/stencil as the draw finds it, before it renders: a change here came from the
		// cache (refresh, association, replacement), not from the draw.
		PixelHistoryProbe(m_graphics, m_scheduler, image, desc.view_info.base_level, desc.view_info.base_layer,
		                  "depth-acquire", 0, 0, {}, false);
	}
	return image.FindView(desc.view_info);
}

void TextureCache::MarkGpuWritten(ImageId id) {
	std::scoped_lock lock {m_lock};
	auto&            image = m_slot_images[id];
	if (!image.registered || image.depth_id) {
		EXIT("TextureCache: cannot mark an unavailable image GPU-written\n");
	}
	TrackImage(id);
	if (image.resize_siblings) {
		image.resize_content = ++m_resize_sequence;
		BumpImageCleanEpoch();
	}
	CommitGpuWrite(image);
	if (image.info.HasStencil()) {
		const auto stencil_id = AssociateStencil(id, image.info.stencil);
		TrackImage(stencil_id);
		CommitGpuWrite(m_slot_images[stencil_id]);
	}
}

void TextureCache::CommitGpuWrite(Image& image) {
	if (!image.depth_id && image.backing.image == nullptr) {
		EXIT("TextureCache: GPU writes require a native image or stencil association\n");
	}
	image.ClearBufferModified();
	if (image.IsCpuDirty()) {
		image.RefreshComplete();
	}
	MarkGpuModified(image);
}

void TextureCache::SyncResizedImages(ImageId id, bool write) {
	static const bool enabled = [] {
		const char* text = KYTY_GETENV("KYTY_IMAGE_RESIZE_SYNC");
		return text != nullptr && std::strcmp(text, "0") != 0;
	}();
	if (!enabled) [[likely]] {
		return;
	}
	auto& image = m_slot_images[id];
	if (image.backing.image == nullptr || image.info.IsVolume()) {
		return;
	}
	// Red lines, deduplicated per key: the first three, then every 1000th.
	static std::map<std::string, uint64_t> logged;
	const auto log = [&](const std::string& key, const std::string& line) {
		const auto count = ++logged[key];
		if (count <= 3 || count % 1000 == 0) {
			std::printf("\x1b[91mimage-alias: %s (#%llu)\x1b[0m\n", line.c_str(),
			            static_cast<unsigned long long>(count));
			std::fflush(stdout);
		}
	};
	const auto& info   = image.info;
	ImageId     newest {};
	for (const auto other_id: FindImagesInRegion(info.data.address, 1, false)) {
		if (other_id == id) continue;
		auto* other = m_slot_images.try_get(other_id);
		if (other == nullptr || other->info.data.address != info.data.address ||
		    other->backing.image == nullptr || other->info.extent == info.extent) {
			continue;
		}
		const auto& o      = other->info;
		// The pitch follows the width on the PS5 (TileGetRenderTargetPitch), so two sizes never share
		// rows in guest memory: the copy keeps the picture, as the game sees one target, not bytes.
		const char* reason = o.tile_mode != info.tile_mode                 ? "tiling differs"
		                     : o.pixel_format != info.pixel_format         ? "format differs"
		                     : o.type != info.type || o.IsVolume()         ? "type differs"
		                     : other->backing.samples != image.backing.samples ? "samples differ"
		                                                                       : nullptr;
		if (reason != nullptr) {
			log(fmt::format("skip {:x} {}x{} {}x{} {}", info.data.address, info.extent.width,
			                info.extent.height, o.extent.width, o.extent.height, reason),
			    fmt::format("addr=0x{:x} {}x{} pitch={} tile={} fmt={} vs {}x{} pitch={} tile={} fmt={}: not synced, {}",
			                info.data.address, info.extent.width, info.extent.height, info.pitch,
			                static_cast<uint32_t>(info.tile_mode), static_cast<uint32_t>(info.pixel_format),
			                o.extent.width, o.extent.height, o.pitch, static_cast<uint32_t>(o.tile_mode),
			                static_cast<uint32_t>(o.pixel_format), reason));
			continue;
		}
		if (!image.resize_siblings || !other->resize_siblings) {
			// Bindings that kept a view of either (image groups, view reuse) look again.
			image.resize_siblings  = true;
			other->resize_siblings = true;
			BumpImageCleanEpoch();
		}
		if (other->resize_content > image.resize_content && other->IsGpuModified() &&
		    !other->IsBufferModified() && !other->IsCpuDirty() &&
		    (!newest || other->resize_content > m_slot_images[newest].resize_content)) {
			newest = other_id;
		}
	}
	if (newest) {
		auto& source = m_slot_images[newest];
		log(fmt::format("sync {:x} {}x{} {}x{}", info.data.address, info.extent.width, info.extent.height,
		                source.info.extent.width, source.info.extent.height),
		    fmt::format("addr=0x{:x} {}x{} <- copy of {}x{} (pitch={} tile={} fmt={})", info.data.address,
		                info.extent.width, info.extent.height, source.info.extent.width,
		                source.info.extent.height, info.pitch, static_cast<uint32_t>(info.tile_mode),
		                static_cast<uint32_t>(info.pixel_format)));
		image.CopyAliasRegion(source);
		image.resize_content = source.resize_content;
		MarkGpuModified(image);
	}
	if (write && image.resize_siblings) {
		image.resize_content = ++m_resize_sequence;
		// The siblings' bindings must reach Find* again to copy this write.
		BumpImageCleanEpoch();
	}
}

bool TextureCache::ClearImageFromBuffer(CommandBuffer& command, uint64_t address, uint64_t size,
                                        uint32_t packed_clear, uint32_t block_bytes) {
	if (command.IsInvalid() || !GuestRange {address, size}.Valid()) {
		EXIT("TextureCache: invalid image clear\n");
	}
	std::scoped_lock      lock {m_lock};
	ImageId               selected {};
	ImageId               stencil_id {};
	vk::ImageAspectFlags  aspect {};
	ImageSubresourceRange subresources;
	std::vector<ImageId>  also_clear;
	for (const auto id: FindImagesInRegion(address, size, false)) {
		auto owner = m_slot_images.try_get(id);
		if (owner == nullptr) {
			continue;
		}
		vk::ImageAspectFlags candidate {};
		ImageId              candidate_id = id;
		ImageSubresourceRange candidate_subresources {
		    0, owner->info.resources.levels, 0, owner->info.TransferLayers()};
		if (owner->depth_id && owner->info.data.address == address &&
		    owner->info.data.size == size) {
			candidate              = vk::ImageAspectFlagBits::eStencil;
			candidate_id           = owner->depth_id;
			candidate_subresources = owner->stencil_subresources;
			owner        = m_slot_images.try_get(candidate_id);
			if (owner == nullptr || owner->backing.image == nullptr || !owner->info.HasStencil()) {
				continue;
			}
		} else if (!owner->depth_id && owner->info.data.address == address &&
		           owner->info.data.size == size) {
			candidate = owner->info.IsDepth() ? vk::ImageAspectFlagBits::eDepth
			                                  : vk::ImageAspectFlagBits::eColor;
		}
		if (!candidate) {
			continue;
		}
		// A DCC comp-to-single block color belongs to the compressed surface of that texel size
		// only; other images over the memory (aliases of another format) are left alone.
		if (block_bytes != 0 && (candidate != vk::ImageAspectFlagBits::eColor ||
		                         owner->info.bytes_per_block * owner->info.samples != block_bytes)) {
			continue;
		}
		if (selected && block_bytes != 0) {
			return false;
		}
		if (selected && candidate == vk::ImageAspectFlagBits::eColor &&
		    aspect == vk::ImageAspectFlagBits::eColor && selected != candidate_id) {
			// Several color images over exactly this memory (Ghost of Yotei: its 8-bit shadow mask
			// and an R16G16 float surface that reuses the memory): the fill writes the same bytes
			// under all of them, so each gets the clear. Rejecting the fill left the mask stale.
			also_clear.push_back(candidate_id);
			continue;
		}
		if (selected && (selected != candidate_id || subresources != candidate_subresources)) {
			return false;
		}
		selected     = candidate_id;
		stencil_id   = candidate == vk::ImageAspectFlagBits::eStencil ? id : ImageId {};
		aspect       = candidate;
		subresources = candidate_subresources;
	}
	if (!selected) {
		return false;
	}
	auto&          image = m_slot_images[selected];
	if (block_bytes != 0 && (aspect != vk::ImageAspectFlagBits::eColor ||
	                         image.info.bytes_per_block * image.info.samples != block_bytes)) {
		return false;
	}
	vk::ClearValue clear {};
	if (aspect == vk::ImageAspectFlagBits::eColor) {
		if (!DecodeColorDwordFill(image.info.pixel_format, packed_clear, clear.color)) {
			return false;
		}
		for (const auto other: also_clear) {
			vk::ClearColorValue unused {};
			if (!DecodeColorDwordFill(m_slot_images[other].info.pixel_format, packed_clear, unused)) {
				return false;
			}
		}
	} else {
		uint8_t stencil_clear = 0;
		if ((aspect == vk::ImageAspectFlagBits::eDepth &&
		     !DecodePackedDepthClear(image.info.pixel_format, packed_clear, clear.depthStencil.depth)) ||
		    (aspect == vk::ImageAspectFlagBits::eStencil &&
		     !DecodePackedStencilClear(packed_clear, stencil_clear))) {
			return false;
		}
		clear.depthStencil.stencil = stencil_clear;
	}
	ClearImage(command, selected, image.backing.format,
	           {aspect, subresources.base_level, subresources.level_count,
	            subresources.base_layer, subresources.layer_count}, clear);
	for (const auto other: also_clear) {
		auto&          alias = m_slot_images[other];
		vk::ClearValue alias_clear {};
		(void)DecodeColorDwordFill(alias.info.pixel_format, packed_clear, alias_clear.color);
		ClearImage(command, other, alias.backing.format,
		           {vk::ImageAspectFlagBits::eColor, 0, alias.info.resources.levels, 0,
		            alias.info.TransferLayers()},
		           alias_clear);
	}
	if (stencil_id) {
		TrackImage(stencil_id);
		CommitGpuWrite(m_slot_images[stencil_id]);
	}
	return true;
}

void TextureCache::ClearImage(CommandBuffer& command, ImageId id, vk::Format format,
                              const vk::ImageSubresourceRange& range, const vk::ClearValue& clear) {
	auto& image = m_slot_images[id];
	const auto aspects = image.info.IsDepth() ? ImageViewOps::DepthAspectMask(image.backing.format)
	                                          : vk::ImageAspectFlagBits::eColor;
	EXIT_IF(range.baseMipLevel >= image.info.resources.levels);
	const auto layers = image.info.IsVolume()
	                        ? std::max(image.info.extent.depth >> range.baseMipLevel, 1u)
	                        : image.backing.layers;
	EXIT_IF(command.IsInvalid() || image.depth_id || !range.aspectMask || range.levelCount == 0 ||
	        range.levelCount > image.info.resources.levels - range.baseMipLevel ||
	        range.layerCount == 0 || range.baseArrayLayer >= layers ||
	        range.layerCount > layers - range.baseArrayLayer ||
	        (range.aspectMask & aspects) != range.aspectMask);
	const bool full_subresources = range.baseMipLevel == 0 &&
	                               range.levelCount == image.info.resources.levels &&
	                               range.baseArrayLayer == 0 && range.layerCount == layers;
	const bool full_image = range.aspectMask == aspects && full_subresources;
	TrackImage(id);
	if (!full_image && (image.IsBufferModified() || image.IsCpuDirty())) {
		InitializeImage(id);
		if (image.info.samples == 1 && (image.IsBufferModified() || image.IsCpuDirty())) {
			EXIT("TextureCache: image clear retained guest ownership\n");
		}
	}
	if (image.info.HasStencil() && (range.aspectMask & vk::ImageAspectFlagBits::eStencil)) {
		const auto stencil_id = AssociateStencil(id, image.info.stencil);
		if (!full_subresources) {
			RefreshImage(stencil_id);
		} else {
			TrackImage(stencil_id);
			CommitGpuWrite(m_slot_images[stencil_id]);
		}
	}
	command.EndRendering();
	// Transfer clears use the backing format; aliased clears must encode through their view.
	if (format != image.backing.format || (image.info.IsVolume() && !full_image)) {
		EXIT_NOT_IMPLEMENTED(range.aspectMask != vk::ImageAspectFlagBits::eColor ||
		                     range.levelCount != 1);
		ImageViewInfo view {};
		view.format = format;
		view.type   = range.layerCount == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray;
		view.base_level  = range.baseMipLevel;
		view.base_layer  = range.baseArrayLayer;
		view.layer_count = range.layerCount;
		view.usage       = vk::ImageUsageFlagBits::eColorAttachment;
		image.Transit(vk::ImageLayout::eColorAttachmentOptimal,
		              vk::AccessFlagBits2::eColorAttachmentWrite, {}, command.Handle());
		vk::RenderingAttachmentInfo attachment {};
		attachment.imageView   = image.FindView(view);
		attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachment.loadOp      = vk::AttachmentLoadOp::eClear;
		attachment.storeOp     = vk::AttachmentStoreOp::eStore;
		attachment.clearValue  = clear;
		vk::RenderingInfo rendering {};
		rendering.renderArea.extent = {
		    std::max(image.info.extent.width >> range.baseMipLevel, 1u),
		    std::max(image.info.extent.height >> range.baseMipLevel, 1u)};
		rendering.layerCount           = range.layerCount;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments    = &attachment;
		LabelCausalTrace::NotePartialNativeClear(true,reinterpret_cast<uint64_t>(&command));
		command.Handle().beginRendering(&rendering);
		command.Handle().endRendering();
		CommitGpuWrite(image);
		return;
	}
	image.Transit(vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, {},
	              command.Handle());
	LabelCausalTrace::NotePartialNativeClear(false,reinterpret_cast<uint64_t>(&command));
	auto native_range = range;
	GpuZones::Mark(command.Handle(), DrainStats::Zone::ImageCopy);
	if (image.info.IsVolume()) {
		native_range.baseArrayLayer = 0;
		native_range.layerCount     = 1;
	}
	if (range.aspectMask == vk::ImageAspectFlagBits::eColor) {
		command.Handle().clearColorImage(image.backing.image, vk::ImageLayout::eTransferDstOptimal,
		                                 &clear.color, 1, &native_range);
	} else {
		command.Handle().clearDepthStencilImage(image.backing.image,
		                                        vk::ImageLayout::eTransferDstOptimal,
		                                        &clear.depthStencil, 1, &native_range);
	}
	CommitGpuWrite(image);
	if (PixelHistoryEnabled()) [[unlikely]] {
		PixelHistoryProbe(m_graphics, m_scheduler, image, range.baseMipLevel, range.baseArrayLayer, "clear", 0, 0,
		                  range.aspectMask == vk::ImageAspectFlagBits::eColor
		                      ? fmt::format("color {:08x}", clear.color.uint32[0])
		                      : fmt::format("depth {} stencil {:02x} aspects {:x}", clear.depthStencil.depth,
		                                    clear.depthStencil.stencil, static_cast<uint32_t>(range.aspectMask)),
		                  true);
	}
}

void TextureCache::InvalidateMemory(uint64_t address, uint64_t size) {
	if (!GuestRange {address, size}.Valid()) {
		EXIT("TextureCache: invalid memory-invalidation range\n");
	}
	if (!GuestGpu::IsGpuThread() && CommandScheduler::InDeferredOperation()) {
		EXIT("unsupported image invalidation from an asynchronous GPU completion, "
		     "addr=0x%016" PRIx64 " size=0x%016" PRIx64 "\n",
		     address, size);
	}
	// Invalidation can release a page's write watcher even when the fault is just outside
	// an image's byte range. Finish every writeback on that page before permitting CPU stores.
	const auto page_begin = std::max<uint64_t>(Common::AlignDown(address, TRACKER_PAGE_SIZE), 1);
	const auto page_end   = Common::AlignUp(address + size, TRACKER_PAGE_SIZE);
	const auto page_size  = page_end - page_begin;
	std::unique_lock lock {m_lock};
	while (HasPendingDownload(page_begin, page_size)) {
		// The GPU thread can need m_lock before reaching this command. Never wait for that
		// thread while holding the texture lock on a CPU fault path.
		lock.unlock();
		m_scheduler.Context().GetGpu().SendCommandSync([this, page_begin, page_size] {
			DrainStats::ReasonScope reason(DrainStats::Reason::TexturePendingDownload);
			if (HasPendingDownload(page_begin, page_size)) {
				const auto tick = m_scheduler.CurrentTick();
				m_scheduler.Wait(tick);
				m_scheduler.WaitPriorityOperations(tick);
			}
		});
		lock.lock();
		// A new download may have been recorded before we reacquired m_lock. Its publication
		// must also complete; checking and invalidating under this lock excludes another one.
	}
	InvalidateCpuAliases(address, size);
}

void TextureCache::DownloadDepth(Image& image, Buffer& destination, uint64_t destination_offset) {
	TileManager::ScratchScope scratch_scope(m_tiler);
	const auto&    info             = image.info;
	const auto     layers           = info.resources.layers;
	const auto     full_slice_size  = info.data.size / layers;
	const auto     transfer_bytes   = DepthAspectTransferBytes(info.pixel_format);
	const uint64_t texels_per_slice = static_cast<uint64_t>(info.pitch) * info.extent.height;
	EXIT_NOT_IMPLEMENTED(transfer_bytes == 0 || texels_per_slice > UINT32_MAX ||
	                     texels_per_slice > UINT64_MAX / transfer_bytes ||
	                     texels_per_slice > UINT64_MAX / info.bytes_per_block);
	const uint64_t transfer_slice = texels_per_slice * transfer_bytes;
	const uint64_t guest_slice    = texels_per_slice * info.bytes_per_block;
	EXIT_NOT_IMPLEMENTED(transfer_slice > UINT64_MAX / layers);
	const uint64_t transfer_size = transfer_slice * layers;
	EXIT_NOT_IMPLEMENTED(guest_slice > full_slice_size);
	auto copies = BuildDepthCopies(info, full_slice_size, vk::ImageAspectFlagBits::eDepth);
	if (transfer_bytes == info.bytes_per_block) {
		if (!info.IsTiled()) {
			for (auto& copy: copies) {
				copy.bufferOffset += destination_offset;
			}
			image.Download(copies, destination.Handle(), destination_offset, info.data.size,
			               destination.Size());
			return;
		}
		const auto tiles = BuildDepthTiles(info);
		m_tiler.TileImage(image, copies, destination.Handle(), destination_offset, info.data.size,
		                  info.data.size, tiles);
		return;
	}
	EXIT_NOT_IMPLEMENTED(info.bytes_per_block != sizeof(uint16_t) ||
	                     transfer_bytes != sizeof(uint32_t));
	for (uint32_t layer = 0; layer < layers; layer++) {
		copies[layer].bufferOffset = transfer_slice * layer;
	}
	auto host_linear = m_tiler.GetScratchBuffer(transfer_size);
	image.Download(copies, host_linear.buffer, 0, host_linear.size, host_linear.capacity);
	const bool tiled        = info.IsTiled();
	auto       guest_linear = tiled ? m_tiler.GetScratchBuffer(info.data.size)
	                                : TileManager::Result {destination.Handle(), destination_offset,
	                                                       destination.Size() - destination_offset};
	m_tiler.ConvertD16(host_linear, guest_linear, TileManager::D16Direction::Demote,
	                   DepthAspectTransferFormat(info.pixel_format) == vk::Format::eD32Sfloat,
	                   {.width               = info.extent.width,
	                    .height              = info.extent.height,
	                    .layers              = layers,
	                    .source_row_stride   = static_cast<uint64_t>(info.pitch) * sizeof(uint32_t),
	                    .target_row_stride   = static_cast<uint64_t>(info.pitch) * sizeof(uint16_t),
	                    .source_slice_stride = transfer_slice,
	                    .target_slice_stride = full_slice_size});
	if (!tiled) {
		return;
	}
	const auto tiles = BuildDepthTiles(info);
	m_tiler.Tile(guest_linear.buffer, guest_linear.offset, info.data.size, destination.Handle(),
	             destination_offset, info.data.size, tiles);
}

void TextureCache::DownloadImage(Image& image, Buffer& destination, uint64_t destination_offset,
                                     uint64_t destination_size, ImageDownload transfer) {
	TileManager::ScratchScope scratch_scope(m_tiler);
	GpuZones::KeyScope zone_key(image.info.data.address | GpuZones::DownloadKey);
	if (!transfer.valid) {
		EXIT("TextureCache: invalid image download transfer\n");
	}
	if (transfer.depth_target) {
		if (destination_size != image.info.data.size) {
			EXIT("TextureCache: partial depth image download is unsupported\n");
		}
		DownloadDepth(image, destination, destination_offset);
		return;
	}

	auto&      texture   = transfer.texture;
	const auto transform = texture.color_transform;
	if (texture.tiles.empty()) {
		if (transform != ColorTransform::None) {
			auto linear = m_tiler.GetScratchBuffer(destination_size);
			image.Download(texture.regions, linear.buffer, 0, linear.size, linear.capacity);
			m_tiler.TransformColor(linear,
			                       {destination.Handle(), destination_offset, destination_size},
			                       transform, false);
			return;
		}
		for (auto& copy: texture.regions) {
			copy.bufferOffset += destination_offset;
		}
		image.Download(texture.regions, destination.Handle(), destination_offset, destination_size,
		               destination.Size());
		return;
	}

	m_tiler.TileImage(image, texture.regions, destination.Handle(), destination_offset,
	                  destination_size, texture.LinearSize(), texture.tiles, transform);
}

bool BufferCache::SynchronizeBufferFromImage(Buffer& buffer, uint64_t vaddr, uint64_t size) {
	const auto selected = m_texture_cache.FindImageFromRange(vaddr, size);
	if (!selected) {
		return false;
	}

	std::scoped_lock lock {m_texture_cache.m_lock};
	auto& image = m_texture_cache.m_slot_images[selected];
	// The GPU thread owns image retirement; CPU invalidation can dirty this image after lookup.
	if (!image.SafeToDownload()) {
		return false;
	}
	if (!buffer.IsInBounds(image.info.data.address, 1)) {
		return false;
	}
	const auto buf_offset = buffer.Offset(image.info.data.address);
	const auto available  = buffer.Size() - buf_offset;
	uint32_t   levels     = 0;
	uint64_t   copy_size  = 0;
	if (buffer.IsInBounds(image.info.data.address, image.info.data.size)) {
		levels    = image.info.resources.levels;
		copy_size = image.info.data.size;
	} else if (image.info.IsVolume() || image.info.resources.layers > 1) {
		// Array and volume slices are strided across the full mip chain.
		return false;
	} else {
		for (; levels < image.info.resources.levels; ++levels) {
			const auto& mip = image.info.mip_layout[levels];
			if (mip.size == 0 || mip.offset > available || mip.size > available - mip.offset) {
				break;
			}
			copy_size = std::max(copy_size, mip.offset + mip.size);
		}
	}
	if (copy_size == 0) {
		return false;
	}
	auto transfer = m_texture_cache.BuildDownload(image);
	if (!transfer.valid) {
		return false;
	}
	if (transfer.depth_target && copy_size != image.info.data.size) {
		return false;
	}
	if (!transfer.depth_target && levels < image.info.resources.levels) {
		auto& texture = transfer.texture;
		std::erase_if(texture.regions, [levels](const vk::BufferImageCopy& region) {
			return region.imageSubresource.mipLevel >= levels;
		});
		if (texture.regions.empty()) {
			return false;
		}
		if (!texture.tiles.empty()) {
			texture.tiles.clear();
			if (!TextureBuildGpuTileInfos(copy_size, texture.regions, texture.layout, levels,
			                              texture.tiles)) {
				return false;
			}
		}
	}
	m_texture_cache.DownloadImage(image, buffer, buf_offset, copy_size, std::move(transfer));
	return true;
}

bool TextureCache::DownloadImageMemory(ImageId id) {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::DownloadImageMemory);
	auto& image = m_slot_images[id];
	if (image.depth_id) {
		return false;
	}
	auto transfer = BuildDownload(image);
	if (!transfer.valid || !image.SafeToDownload()) {
		return false;
	}
	const auto range    = image.info.data;
	auto&      staging  = m_buffer_cache.GetUtilityBuffer(MemoryUsage::Download);
	auto [mapped, offset] =
	    staging.Map(range.size, std::max<uint64_t>(image.info.bytes_per_block, 4));
	std::unique_ptr<Buffer> temporary;
	Buffer*                 download = &staging;
	if (mapped == nullptr) {
		// Tiled and format-converted downloads also write via compute shaders, so the fallback
		// must retain storage-buffer usage rather than only supporting transfer destinations.
		temporary = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Download, 0,
		                                     AllFlags, range.size);
		download  = temporary.get();
		mapped    = download->Mapped().data();
		offset    = 0;
		EXIT_IF(mapped == nullptr);
	} else {
		staging.Commit();
	}
	if (!LibKernel::Memory::TryReadBacking(range.address, mapped, range.size)) {
		return false;
	}
	download->Flush(offset, range.size);

	DownloadImage(image, *download, offset, range.size, std::move(transfer));
	vk::BufferMemoryBarrier barrier {};
	barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eTransferWrite |
	                        vk::AccessFlagBits::eShaderWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = download->Handle();
	barrier.offset              = offset;
	barrier.size                = range.size;
	m_scheduler.EndRendering();
	m_scheduler.Current().Handle().pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                                               vk::PipelineStageFlagBits::eHost, {}, 0, nullptr,
	                                               1, &barrier, 0, nullptr);
	{
		std::lock_guard lock(m_pending_download_mutex);
		m_pending_downloads.push_back(range);
	}
	m_scheduler.DeferPriorityOperation(
	    [this, download, owner = std::move(temporary), range, mapped, offset] {
		    download->Invalidate(offset, range.size);
		    LibKernel::Memory::WriteBacking(range.address, mapped, range.size);
		    // Separate from m_lock: the GPU thread may wait for this callback while it owns the
		    // texture cache lock (for example on staging wrap). Keep overlapping records distinct.
		    std::lock_guard lock(m_pending_download_mutex);
		    const auto      pending = std::ranges::find(m_pending_downloads, range);
		    EXIT_IF(pending == m_pending_downloads.end());
		    m_pending_downloads.erase(pending);
		    (void)owner;
	    });
	return true;
}

bool TextureCache::HasPendingDownload(uint64_t address, uint64_t size) {
	if (!GuestRange {address, size}.Valid()) {
		return false;
	}
	std::lock_guard lock(m_pending_download_mutex);
	return std::ranges::any_of(m_pending_downloads, [address, size](const GuestRange& pending) {
		return pending.address < address + size && address < pending.End();
	});
}

void TextureCache::InvalidateMemoryFromGPU(uint64_t address, uint64_t size,
                                           GpuWriteSource source) {
	if (!GuestRange {address, size}.Valid()) {
		return;
	}
	// Fill/copy paths invalidate before acquiring the writable buffer. The
	// acquisition hook covers shader stores and callers that invalidate after
	// recording; preserve the old image before relinquishing its ownership.
	PrepareBlockBufferWrite(address, size);
	std::scoped_lock lock {m_lock};
	const auto images = FindImagesInRegion(address, size, true);
	// A shader writing exactly one image's range as a buffer (Sky Garden writes several tiled
	// render-target surfaces so, each a few times a frame) changes that image and those inside
	// the range. An image the range only partly overlaps is another surface using the memory at
	// another time: on the console the bytes outside the range keep its contents, and the bytes
	// inside are another surface's layout, which it does not read. Re-uploading it from the
	// buffer instead lost the rest of its contents and cost a full-size detile per write.
	// KYTY_DEBUG_AB=exactwrite marks every overlapped image again in every other window.
	static const bool ab    = AbSelected("exactwrite");
	bool              exact = false;
	if (!(ab && AbFeatureOff())) {
		for (const auto id: images) {
			const auto& data = m_slot_images[id].info.data;
			exact            = exact || (data.address == address && data.size == size);
		}
	}
	// BC blocks can be filled through interior buffer ranges, mip by mip and
	// layer by layer. Earlier image transfers were published before the write,
	// so refreshing any byte-overlapping BC consumer preserves untouched blocks.
	for (const auto id: images) {
		auto& image = m_slot_images[id];
		if (image.info.IsBlock() && image.Overlaps(address, size, false)) {
			static std::mutex                   bc_mutex;
			static std::unordered_set<uint64_t> bc_seen;
			{
				const std::lock_guard bc_lock(bc_mutex);
				if (bc_seen.size() < 1024 && bc_seen.insert(image.info.data.address).second) {
					std::printf("\x1b[91mbc-buffer-write: BC image 0x%016" PRIx64 " (%ux%u, %u layers, %u mips) takes "
					            "a buffer write at +0x%" PRIx64 " size 0x%" PRIx64 "\x1b[0m\n",
					            image.info.data.address, image.info.extent.width, image.info.extent.height,
					            image.info.resources.layers, image.info.resources.levels,
					            address - image.info.data.address, size);
					std::fflush(stdout);
				}
			}
			if (image.IsGpuModified()) {
				image.ClearGpuModified();
			}
			image.MarkBufferModified();
			continue;
		}
		if (image.info.data.address != address) {
			continue;
		}
		if (exact && (image.info.data.address < address ||
		              image.info.data.End() > address + size ||
		              (image.info.IsTiled() && image.info.data.size != size))) {
			continue;
		}
		if (GpuZones::Enabled()) [[unlikely]] {
			LogZoneBufferWrite(image, address, size, source);
		}
		if (image.IsGpuModified()) {
			image.ClearGpuModified();
		}
		image.MarkBufferModified();
	}
}

bool TextureCache::IsRegionGpuModified(uint64_t address, uint64_t size) {
	if (!GuestRange {address, size}.Valid()) {
		return false;
	}
	// Strict SRT reads ask this for the same small CPU-written tables every draw. A tracker page
	// without a GPU-modified image stays so until an image is registered or becomes GPU-modified;
	// every other change only cleans pages further (and stencil associations never go away).
	struct CleanPage {
		uint64_t page       = UINT64_MAX;
		uint64_t images     = 0;
		uint64_t gpu_writes = 0;
	};
	thread_local std::array<CleanPage, 64> clean_pages {};
	// KYTY_DEBUG_AB=cleanpages scans the images every time in every other window.
	static const bool ab       = AbSelected("cleanpages");
	const auto        page     = address / TRACKER_PAGE_SIZE;
	const bool        one_page = (address + size - 1) / TRACKER_PAGE_SIZE == page &&
	                      !(ab && AbFeatureOff());
	auto& slot = clean_pages[page % clean_pages.size()];
	if (one_page && slot.page == page &&
	    slot.images == m_image_set_generation.load(std::memory_order_acquire) &&
	    slot.gpu_writes == m_gpu_modified_generation.load(std::memory_order_acquire)) {
		return false;
	}
	std::scoped_lock lock {m_lock};
	// Taken before the scan, so a change racing with it leaves the record stale, never clean.
	const auto images       = m_image_set_generation.load(std::memory_order_acquire);
	const auto gpu_writes   = m_gpu_modified_generation.load(std::memory_order_acquire);
	// PPSA17168: S_LOAD_DWORD reads shader data at an address overlapping an old render target
	// whose memory the CPU has reused. The cached image still retains its earlier GPU-modified
	// flag, so a definitely CPU-dirty image doesn't count. The clean-page record ignores that
	// exception: CPU-dirty clears without a generation bump, so only pages without any
	// GPU-modified image are recorded.
	const auto gpu_modified = [this](uint64_t begin, uint64_t bytes, bool honor_cpu_dirty) {
		for (const auto id: FindImagesInRegion(begin, bytes, false)) {
			const auto& image = m_slot_images[id];
			if (!image.depth_id && image.IsGpuModified() &&
			    !(honor_cpu_dirty && image.IsDefinitelyCpuDirty())) {
				return true;
			}
		}
		return false;
	};
	if (one_page && !gpu_modified(page * TRACKER_PAGE_SIZE, TRACKER_PAGE_SIZE, false)) {
		slot = {page, images, gpu_writes};
		return false;
	}
	return gpu_modified(address, size, true);
}

void TextureCache::InvalidateCpuAliases(uint64_t address, uint64_t size) {
	const auto page_begin = Common::AlignDown(address, TRACKER_PAGE_SIZE);
	const auto page_end   = Common::AlignUp(address + size, TRACKER_PAGE_SIZE);
	for (const auto id: FindImagesInRegion(address, size, true)) {
		auto owner = m_slot_images.try_get(id);
		if (owner == nullptr) {
			continue;
		}
		if (owner->Overlaps(address, size)) {
			owner->InvalidateCpuWrite(address, size);
			UntrackImage(id);
			continue;
		}
		const auto image_begin = owner->info.data.address;
		const auto image_end   = owner->info.data.End();
		if (page_end < image_end) {
			UntrackImageHead(id);
		} else if (image_begin < page_begin) {
			UntrackImageTail(id);
		} else {
			MarkAsMaybeDirty(id, *owner);
		}
	}
}

void TextureCache::MarkHtileStale(const ImageDesc& desc, ImageId image) {
	const auto& range = desc.info.metadata.range;
	if (range.size == 0) {
		return;
	}
	const StaleHtile surface {.address      = range.address,
	                          .size         = range.size,
	                          .data_address = desc.info.data.address,
	                          .width        = desc.info.extent.width,
	                          .height       = desc.info.extent.height,
	                          .layers       = desc.info.resources.layers,
	                          .image        = image};
	for (auto& stale: m_htile_stale) {
		if (stale.address == range.address) {
			stale = surface;
			return;
		}
	}
	// Surfaces nobody reads stay listed; keep the list (checked for every compute read) short.
	constexpr size_t MaxStale = 32;
	if (m_htile_stale.size() >= MaxStale) {
		m_htile_stale.erase(m_htile_stale.begin());
	}
	m_htile_stale.push_back(surface);
}

void TextureCache::SyncHtile(uint64_t address, uint64_t size) {
	if (m_htile_stale.empty() || size == 0 || size > UINT64_MAX - address) {
		return;
	}
	for (size_t index = 0; index < m_htile_stale.size();) {
		const auto stale = m_htile_stale[index];
		if (stale.address < address + size && address < stale.address + stale.size) {
			m_htile_stale.erase(m_htile_stale.begin() + static_cast<std::ptrdiff_t>(index));
			SynthesizeHtile(stale);
		} else {
			index++;
		}
	}
}

void TextureCache::SynthesizeHtile(const StaleHtile& surface) {
	static std::atomic<uint32_t> skipped {0};
	ImageInfo info {};
	auto skip = [&](const char* why) {
		if (skipped.fetch_add(1, std::memory_order_relaxed) < 16) {
			std::printf("htile: not synthesized for 0x%016" PRIx64 " (target %ux%u layers=%u, image "
			            "%ux%u layers=%u pitch=%u): %s\n",
			            surface.address, surface.width, surface.height, surface.layers,
			            info.extent.width, info.extent.height, info.resources.layers, info.pitch,
			            why);
			std::fflush(stdout);
		}
	};
	if (!m_htile_writer.Available()) {
		return skip("no pipeline");
	}
	{
		std::scoped_lock lock {m_lock};
		const auto* image = m_slot_images.try_get(surface.image);
		if (image == nullptr || !image->registered || image->backing.image == nullptr ||
		    image->depth_id || image->info.metadata.kind != ImageMetadataKind::Htile ||
		    image->info.metadata.range.address != surface.address) {
			return skip("depth image gone or rebound");
		}
		info = image->info;
	}
	const auto layers         = surface.layers;
	const auto transfer_bytes = DepthAspectTransferBytes(info.pixel_format);
	const auto format         = DepthAspectTransferFormat(info.pixel_format);
	if (info.metadata.stencil_compressed) {
		return skip("stencil in HTile");
	}
	// The target is the image's top-left corner, from its first layer on.
	if (info.data.address != surface.data_address || surface.width == 0 || surface.height == 0 ||
	    layers == 0 || surface.width > info.extent.width || surface.height > info.extent.height ||
	    layers > info.resources.layers) {
		return skip("target not at the image's origin");
	}
	if (info.samples != 1 || transfer_bytes == 0) {
		return skip("multisampled or unsupported depth");
	}
	const auto layer_bytes = surface.size / layers;
	if (surface.size % layers != 0 ||
	    layer_bytes < HtileWriter::LayerBytes(surface.width, surface.height)) {
		return skip("HTile range smaller than the target");
	}
	const uint64_t texels_per_slice = static_cast<uint64_t>(surface.width) * surface.height;
	const uint64_t transfer_slice =
	    Common::AlignUp(texels_per_slice * transfer_bytes, uint64_t {4});
	if (transfer_slice * layers > UINT32_MAX) {
		return skip("target too large");
	}
	std::vector<vk::BufferImageCopy> copies(layers);
	for (uint32_t layer = 0; layer < layers; ++layer) {
		auto& copy             = copies[layer];
		copy.bufferOffset      = transfer_slice * layer;
		copy.bufferRowLength   = surface.width;
		copy.bufferImageHeight = surface.height;
		copy.imageSubresource  = {vk::ImageAspectFlagBits::eDepth, 0, layer, 1};
		copy.imageExtent       = vk::Extent3D {surface.width, surface.height, 1};
	}
	TileManager::ScratchScope scratch_scope(m_tiler);
	const auto linear = m_tiler.GetScratchBuffer(transfer_slice * layers);
	// The rebuilt HTile is GPU-owned guest memory, like the DB's own writes.
	const auto [htile, htile_offset] =
	    m_buffer_cache.ObtainBuffer(surface.address, surface.size, true, false);
	{
		std::scoped_lock lock {m_lock};
		auto* image = m_slot_images.try_get(surface.image);
		if (image == nullptr || !image->registered || image->backing.image == nullptr) {
			return skip("depth image gone");
		}
		RefreshImage(surface.image);
		image->Download(copies, linear.buffer, 0, linear.size, linear.capacity);
	}
	HtileWriter::Surface params {};
	params.width        = surface.width;
	params.height       = surface.height;
	params.layers       = layers;
	params.row_texels   = surface.width;
	params.slice_texels = static_cast<uint32_t>(transfer_slice / transfer_bytes);
	params.layer_bytes  = layer_bytes;
	params.format       = format == vk::Format::eD16Unorm          ? HtileWriter::DepthFormat::Unorm16
	                      : format == vk::Format::eX8D24UnormPack32 ? HtileWriter::DepthFormat::Unorm24
	                                                                : HtileWriter::DepthFormat::Float32;
	auto& command = m_scheduler.Current();
	command.EndRendering();
	m_htile_writer.Record(command.Handle(), linear.buffer, linear.size, *htile, htile_offset, params);
	// A depth target reused without a fresh acquisition (IsDepthTargetCurrent) would not mark the
	// surface stale again when it is drawn to next.
	m_surface_meta_generation.fetch_add(1, std::memory_order_release);
}

bool TextureCache::IsMeta(uint64_t address) {
	std::scoped_lock lock {m_lock};
	const auto       found = m_surface_metas.find(address);
	return found != m_surface_metas.end();
}

bool TextureCache::IsKnownDccMetadata(uint64_t address, uint64_t size) {
	std::scoped_lock lock {m_lock};
	return IsKnownDccMetadataLocked(address, size);
}

bool TextureCache::IsKnownDccMetadataLocked(uint64_t address, uint64_t size) const {
	return m_dcc_metadata_seen.Intersects(address, size);
}

// Draws ask about the same depth surface over and over. The answer holds until its clear state
// changes or a surface metadata entry comes or goes, so the last one is kept without m_lock.
// KYTY_DEBUG_AB=metamemo looks every answer up in every other window.
bool TextureCache::IsMetaCleared(uint64_t address, uint32_t slice) {
	static const bool ab       = AbSelected("metamemo");
	auto&             memo     = m_meta_clear_memo;
	// Read before the lookup: a change racing with it leaves the memo stale, never wrong.
	const auto        surfaces = m_surface_meta_generation.load(std::memory_order_acquire);
	const auto        clears   = m_meta_clear_generation.load(std::memory_order_acquire);
	if (!(memo.valid && memo.address == address && memo.surface_generation == surfaces &&
	      memo.clear_generation == clears) ||
	    (ab && AbFeatureOff())) {
		std::scoped_lock lock {m_lock};
		const auto       found = m_surface_metas.find(address);
		memo = {.address            = address,
		        .surface_generation = surfaces,
		        .clear_generation   = clears,
		        .clear_mask         = found != m_surface_metas.end() ? found->second.clear_mask : 0,
		        .layer_count        = found != m_surface_metas.end() ? found->second.layer_count : 0,
		        .high_clear_masks   = found != m_surface_metas.end()
		                                  ? found->second.high_clear_masks : std::vector<uint32_t> {},
		        .found              = found != m_surface_metas.end(),
		        .valid              = true};
	}
	if (!memo.found || slice >= memo.layer_count) {
		return false;
	}
	const auto mask = slice < 32 ? memo.clear_mask : memo.high_clear_masks[slice / 32 - 1];
	return (mask & (1u << (slice % 32))) != 0;
}

bool TextureCache::MetaClearLogEnabled() {
	static const bool enabled = KYTY_GETENV("KYTY_DEBUG_META_CLEAR") != nullptr;
	return enabled;
}

void TextureCache::LogMetaClear(const char* what, uint64_t address, int32_t layer, uint32_t mask,
                                uint64_t owner) {
	static std::mutex                   mutex;
	static std::unordered_set<uint64_t> seen;
	static uint64_t                     seen_frame = UINT64_MAX;
	const auto&                         source     = t_meta_clear_source;
	const std::lock_guard               lock(mutex);
	if (seen_frame != source.frame) {
		seen_frame = source.frame;
		seen.clear();
	}
	const auto key = address ^ (uint64_t(uint32_t(layer)) << 48u) ^ reinterpret_cast<uint64_t>(what) ^
	                 (reinterpret_cast<uint64_t>(source.source) << 7u);
	if (seen.size() < 512 && seen.insert(key).second) {
		std::printf("\x1b[91mmeta-clear: frame %llu %s address=0x%016llx image=0x%016llx layer=%d mask=0x%08x "
		            "source=%s shader=%016llx\x1b[0m\n",
		            static_cast<unsigned long long>(source.frame), what,
		            static_cast<unsigned long long>(address), static_cast<unsigned long long>(owner),
		            layer, mask,
		            source.source != nullptr ? source.source : "unknown",
		            static_cast<unsigned long long>(source.shader));
		std::fflush(stdout);
	}
}

bool TextureCache::ClearMeta(uint64_t address) {
	std::scoped_lock lock {m_lock};
	const auto       found = m_surface_metas.find(address);
	if (found == m_surface_metas.end()) {
		return false;
	}
	auto& state = found->second;
	state.clear_mask = state.layer_count >= 32 ? UINT32_MAX : (1u << state.layer_count) - 1u;
	std::ranges::fill(state.high_clear_masks, UINT32_MAX);
	const auto tail_bits = state.layer_count % 32;
	if (tail_bits != 0 && !state.high_clear_masks.empty()) {
		state.high_clear_masks.back() &= (1u << tail_bits) - 1u;
	}
	m_meta_clear_generation.fetch_add(1, std::memory_order_release);
	if (MetaClearLogEnabled()) [[unlikely]] {
		LogMetaClear("recorded clear (all layers)", address, -1, state.clear_mask, state.owner_address);
	}
	return true;
}

bool TextureCache::ExpandMeta(uint64_t address) {
	std::scoped_lock lock {m_lock};
	const auto       found = m_surface_metas.find(address);
	if (found == m_surface_metas.end()) {
		return false;
	}
	auto& state = found->second;
	if (state.clear_mask != 0 || std::ranges::any_of(state.high_clear_masks,
	                                               [](uint32_t mask) { return mask != 0; })) {
		state.clear_mask = 0;
		std::ranges::fill(state.high_clear_masks, 0u);
		m_meta_clear_generation.fetch_add(1, std::memory_order_release);
	}
	return true;
}

uint64_t TextureCache::MetaOwner(uint64_t address) {
	std::scoped_lock lock {m_lock};
	const auto       found = m_surface_metas.find(address);
	return found != m_surface_metas.end() ? found->second.owner_address : 0;
}

bool TextureCache::TouchMeta(uint64_t address, uint32_t slice, bool is_clear) {
	std::scoped_lock lock {m_lock};
	const auto       found = m_surface_metas.find(address);
	if (found == m_surface_metas.end() || slice >= found->second.layer_count) {
		return false;
	}
	auto& state = found->second;
	auto& mask  = slice < 32 ? state.clear_mask : state.high_clear_masks[slice / 32 - 1];
	const auto previous = mask;
	const auto bit      = 1u << (slice % 32);
	if (is_clear) {
		mask |= bit;
	} else {
		mask &= ~bit;
	}
	if (mask != previous) {
		m_meta_clear_generation.fetch_add(1, std::memory_order_release);
		if (is_clear && MetaClearLogEnabled()) [[unlikely]] {
			LogMetaClear("recorded clear (one layer)", address, static_cast<int32_t>(slice),
			             mask, state.owner_address);
		}
	}
	return true;
}

bool TextureCache::IsRegionRegistered(uint64_t address, uint64_t size) {
	std::scoped_lock lock {m_lock};
	return !FindImagesInRegion(address, size, false).empty();
}

void TextureCache::UnmapMemory(uint64_t address, uint64_t size) {
	ChurnReason reason("unmap");
	if (!GuestRange {address, size}.Valid()) {
		EXIT("TextureCache: invalid unmap range\n");
	}
	OnBufferGpuWrite(address, size);
	std::scoped_lock lock {m_lock};
	for (auto metadata = m_surface_metas.begin(); metadata != m_surface_metas.end();) {
		const auto base = metadata->first;
		if (base >= address && base < address + size) {
			metadata = m_surface_metas.erase(metadata);
			m_surface_meta_generation.fetch_add(1, std::memory_order_release);
		} else {
			++metadata;
		}
	}
	auto images = FindImagesInRegion(address, size, false);
	for (const auto id: images) {
		auto owner = m_slot_images.try_get(id);
		if (owner == nullptr) {
			continue;
		}
		FreeImage(id);
	}
}

// KYTY_DEBUG_DUMP_IMAGES=<guest address, hex>[,...] with KYTY_DEBUG_DUMP_IMAGES_FILE=<path>: when
// that file exists at a frame's end (checked every 15 frames), mip 0, layer 0 of every cached image
// starting at a listed address is copied out in its host format, tightly packed, to
// _ImageDump/<address>_<frame>_<width>x<height>_<format>.raw, and the file is removed: one snapshot
// per trigger, to see what a GPU-written surface holds.
// Addresses added at run time (the images of a shader under KYTY_DEBUG_BVH_CHECK). GPU thread only.
static std::vector<uint64_t> g_debug_dump_extra;
static std::atomic<uint64_t> g_debug_dump_flip_address {0};
void DebugNoteFlipAddress(uint64_t address) {
	g_debug_dump_flip_address.store(address, std::memory_order_relaxed);
}
void DebugRegisterDumpAddress(uint64_t address) {
	if (std::ranges::find(g_debug_dump_extra, address) == g_debug_dump_extra.end()) {
		g_debug_dump_extra.push_back(address);
	}
}

void TextureCache::DebugDumpImages() {
	static const std::vector<uint64_t> listed = [] {
		std::vector<uint64_t> list;
		for (const char* text = KYTY_GETENV("KYTY_DEBUG_DUMP_IMAGES"); text != nullptr && *text != '\0';) {
			char*      end   = nullptr;
			const auto value = std::strtoull(text, &end, 16);
			if (end == text) {
				break;
			}
			list.push_back(value);
			text = *end == ',' ? end + 1 : nullptr;
		}
		return list;
	}();
	auto addresses = listed;
	addresses.insert(addresses.end(), g_debug_dump_extra.begin(), g_debug_dump_extra.end());
	static const char* trigger = KYTY_GETENV("KYTY_DEBUG_DUMP_IMAGES_FILE");
	static uint64_t    frame   = 0;
	frame++;
	// KYTY_DEBUG_DUMP_VIDEO_OUT=<n> with n > 1: while the trigger file exists, every n-th frame
	// dumps the display buffer being flipped and the listed images (a film of the picture).
	static const uint64_t film = [] {
		const char* text = KYTY_GETENV("KYTY_DEBUG_DUMP_VIDEO_OUT");
		return text != nullptr ? std::strtoull(text, nullptr, 10) : 0ull;
	}();
	const bool filming = film > 1;
	if (trigger == nullptr || frame % (filming ? film : 15) != 0) {
		return;
	}
	if (filming) {
		addresses = listed;
		addresses.push_back(g_debug_dump_flip_address.load(std::memory_order_relaxed));
	}
	if (addresses.empty()) {
		return;
	}
	std::error_code error;
	if (!std::filesystem::exists(trigger, error)) {
		return;
	}
	if (!filming) {
		std::filesystem::remove(trigger, error);
	}
	std::filesystem::create_directories("_ImageDump", error);
	std::vector<ImageId> ids;
	m_slot_images.ForEach([&](ImageId id, const Image& image) {
		if (image.backing.image != nullptr &&
		    std::ranges::find(addresses, image.info.data.address) != addresses.end()) {
			ids.push_back(id);
		}
	});
	for (const auto id: ids) {
		auto&      image  = m_slot_images[id];
		const auto format = image.backing.format;
		const auto bytes  = vk::blockSize(format);
		const auto width  = image.info.extent.width;
		const auto height = image.info.extent.height;
		if (bytes == 0 || vk::blockExtent(format)[0] != 1 || image.info.IsDepth()) {
			std::printf("image-dump: 0x%" PRIx64 " skipped (format %s)\n", image.info.data.address,
			            vk::to_string(format).c_str());
			continue;
		}
		const uint64_t size = static_cast<uint64_t>(width) * height * bytes;
		Buffer         buffer(m_graphics, m_scheduler, MemoryUsage::Download, 0, AllFlags, size);
		vk::BufferImageCopy copy {};
		copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		copy.imageExtent      = vk::Extent3D {width, height, 1};
		image.Download(std::span(&copy, 1), buffer.Handle(), 0, size, buffer.Size());
		m_scheduler.FlushAndWait();
		buffer.Invalidate(0, size);
		const auto path = fmt::format("_ImageDump/{:x}_{}_{}x{}_{}.raw", image.info.data.address, frame,
		                              width, height, vk::to_string(format));
		if (auto* out = std::fopen(path.c_str(), "wb"); out != nullptr) {
			std::fwrite(buffer.Mapped().data(), 1, size, out);
			std::fclose(out);
		}
		std::printf("image-dump: %s gpu_mod=%d\n", path.c_str(), image.IsGpuModified() ? 1 : 0);
	}
	std::fflush(stdout);
}

void TextureCache::TickFrame() {
	DebugDumpImages();
	// Opt-in diagnostic, independent of drain/GPU-zone timestamps. It samples
	// VMA statistics at most once per5s; VMA may refresh driver budget data.
	// There are no submits, Finish calls or GPU timestamp queries. Clean FPS
	// runs keep it off.
	static const bool memory_budget_diagnostic=[] {
		const char* text=KYTY_GETENV("KYTY_DEBUG_MEMORY_BUDGET");
		return text!=nullptr && std::strcmp(text,"1")==0;
	}();
	if (memory_budget_diagnostic) {
		static auto next=std::chrono::steady_clock::time_point{};
		const auto now=std::chrono::steady_clock::now();
		if (now>=next) {
			next=now+std::chrono::seconds(5);
			std::printf("vram-snapshot gcTick=%" PRIu64 " presentedFrame=%" PRIu64 "\n",
			            m_gc_tick,m_graphics.presented_frames.load(std::memory_order_relaxed));
			m_graphics.LogMemoryBudget();
		}
	}
	std::scoped_lock lock {m_lock};
	++m_gc_tick;
}

void TextureCache::RunGarbageCollector() {
	(void)CollectGarbage(false);
}

bool TextureCache::CollectGarbage(bool pressure_only) {
	ChurnReason reason("gc");
	std::unique_lock lock {m_lock};
	// Ages are counted in presented frames (TickFrame); see BufferCache::RunGarbageCollector.
	const uint64_t   tick = m_gc_tick;
	if (m_graphics.CanReportMemoryUsage()) {
		m_total_used_memory = m_graphics.GetDeviceMemoryUsage();
	}
	if (m_total_used_memory < (pressure_only ? m_pressure_gc_memory : m_trigger_gc_memory)) {
		return false;
	}
	bool                 reclaimed = false;
	std::vector<ImageId> pending_retirement;
	const auto collect = [&](bool allow_aggressive) {
		bool           pressured  = m_total_used_memory >= m_pressure_gc_memory;
		bool           aggressive = allow_aggressive && m_total_used_memory >= m_critical_gc_memory;
		// Ages count presented frames (TickFrame): the former submission ages divided by 8.
		const uint64_t minimum_age = aggressive ? 20 : pressured ? 10 : 2;
		if (pressure_only && tick < minimum_age) {
			return;
		}
		const uint64_t     age            = std::min(minimum_age, tick);
		constexpr uint64_t release_margin = 256ull * 1024 * 1024;
		const auto         release_threshold =
		    m_critical_gc_memory - std::min(m_critical_gc_memory, release_margin);
		size_t         deletions = aggressive ? 40 : pressured ? 20 : 10;
		std::vector<ImageId> candidates;
		candidates.reserve(deletions);
		// Deleting depth recursively deletes its stencil association, so finish LRU traversal
		// first.
		m_lru_cache.ForEachItemBelow(tick - age, [&](ImageId id) {
			candidates.push_back(id);
			return candidates.size() == deletions;
		});
		for (const auto id: candidates) {
			if (deletions == 0) {
				break;
			}
			--deletions;
			auto owner = m_slot_images.try_get(id);
			if (owner == nullptr || !owner->registered || owner->depth_id ||
			    std::ranges::find(pending_retirement, id) != pending_retirement.end()) {
				continue;
			}
			if (owner->bindless_pinned) {
				continue;
			}
			if (pressure_only && owner->tick_accessed_last == m_scheduler.CurrentTick()) {
				continue;
			}
			if (owner->info.IsDepth()) {
				bool dirty_stencil = false;
				bool known_stencil = !owner->info.HasStencil();
				m_slot_images.ForEach([&](ImageId, const Image& associated) {
					if (associated.depth_id == id) {
						known_stencil = true;
						dirty_stencil |= associated.IsGpuModified();
					}
				});
				if (dirty_stencil || !known_stencil) {
					continue;
				}
			}
			const bool gpu_modified = owner->IsGpuModified();
			if (gpu_modified) {
				// The depth transfer preserves its plane only; the stencil ownership check above
				// prevents retiring a dirty or unknown companion. Metadata remains conservative.
				if (!pressured || !owner->SafeToDownload() || owner->info.HasMetadata() ||
				    (owner->info.IsTiled() && !aggressive)) {
					continue;
				}
				if (!DownloadImageMemory(id)) {
					continue;
				}
			}
			if (pressure_only || gpu_modified ||
			    HasPendingDownload(owner->info.data.address, owner->info.data.size)) {
				// Unregistering before publication would let CPU stores race WriteBacking.
				pending_retirement.push_back(id);
			} else {
				FreeImage(id);
			}
			reclaimed = true;
			if (m_total_used_memory < release_threshold && aggressive) {
				deletions >>= 2;
				aggressive = false;
			}
			if (m_total_used_memory < m_pressure_gc_memory && pressured) {
				deletions >>= 1;
				pressured = false;
			}
		}
	};
	collect(false);
	if (m_total_used_memory >= m_critical_gc_memory) {
		collect(true);
	}
	if (!pending_retirement.empty()) {
		// Dirty eviction requires one batched completion before relinquishing guest ownership.
		// Keep m_lock so a faulting CPU writer cannot invalidate/unprotect a victim until its
		// priority writeback completes. Those callbacks use only the separate pending mutex.
		// General callbacks can replace buffers after PrepareBda while a draw is being assembled;
		// leave them for the GPU operation boundary and release only this collector's victims.
		DrainStats::ReasonScope reason(DrainStats::Reason::TextureGc);
		const auto              completion_tick = m_scheduler.CurrentTick();
		m_scheduler.Wait(completion_tick);
		m_scheduler.WaitPriorityOperations(completion_tick);
		std::vector<ImageId> retired;
		for (const auto id: pending_retirement) {
			FreeImage(id, &retired);
		}
		// The Vulkan objects outlive the tick now being recorded: this collection can run while
		// a draw is assembled, after it resolved textures whose descriptor writes and copies are
		// recorded later in this tick.
		for (const auto id: retired) {
			m_scheduler.DeferOperation([this, id] { m_slot_images.erase(id); });
		}
	}
	return reclaimed;
}

void TextureCache::ProcessDownloadImages() {
	FunctionProfile::Scope function_profile(FunctionProfile::Site::ProcessDownloadImages);
	// KYTY_DEBUG_IMAGE_DOWNLOADS=1: every 500 batches, the linear images read back to guest
	// memory (address, extent, format, downloads, MiB), to see which readbacks a game needs.
	static const bool log_downloads = [] {
		const char* text = KYTY_GETENV("KYTY_DEBUG_IMAGE_DOWNLOADS");
		return text != nullptr && *text == '1';
	}();
	struct DownloadStat {
		uint32_t width = 0, height = 0, format = 0;
		uint64_t count = 0, bytes = 0;
	};
	static std::map<uint64_t, DownloadStat> stats;
	static uint64_t                         batches = 0;
	std::scoped_lock lock {m_lock};
	for (const auto id: m_download_images) {
		const auto owner = m_slot_images.try_get(id);
		if (owner != nullptr && owner->registered && owner->IsGpuModified()) {
			(void)DownloadImageMemory(id);
			if (log_downloads) [[unlikely]] {
				auto& stat  = stats[owner->info.data.address];
				stat.width  = owner->info.extent.width;
				stat.height = owner->info.extent.height;
				stat.format = static_cast<uint32_t>(owner->info.pixel_format);
				stat.count++;
				stat.bytes += owner->info.data.size;
			}
		}
	}
	m_download_images.clear();
	if (log_downloads && !stats.empty() && ++batches % 500 == 0) [[unlikely]] {
		for (const auto& [address, stat]: stats) {
			std::printf("image-download: %llx %ux%u vkformat=%u n=%llu %.1f MiB\n",
			            static_cast<unsigned long long>(address), stat.width, stat.height,
			            stat.format, static_cast<unsigned long long>(stat.count),
			            static_cast<double>(stat.bytes) / (1024.0 * 1024.0));
		}
		std::fflush(stdout);
		stats.clear();
	}
}

} // namespace Libs::Graphics
