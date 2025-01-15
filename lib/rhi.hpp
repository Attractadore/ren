#pragma once
#include "core/Flags.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "core/String.hpp"
#include "ren/ren.hpp"
#include "ren/tiny_imageformat.h"
#include "rhi-vk.hpp"

#include <chrono>
#include <glm/vec2.hpp>

struct SDL_Window;

namespace ren::rhi {

struct Error {
  enum Code {
    Unknown,
    Unsupported,
    FeatureNotPresent,
    OutOfDate,
    Incomplete,
  };
  Code code = {};
  String description;

  Error() = default;
  Error(Code code, String description = "") {
    this->code = code;
    this->description = std::move(description);
  }

  operator ren::Error() const { return ren::Error::RHI; }
};

inline auto fail(Error::Code code, String description = "") -> Failure<Error> {
  return Failure(Error(code, std::move(description)));
}

inline bool operator==(const Error &error, Error::Code code) {
  return error.code == code;
}

inline bool operator==(Error::Code code, const Error &error) {
  return error == code;
}

template <typename T> using Result = Result<T, Error>;

struct Features {
  bool debug_names : 1 = false;
  bool debug_layer : 1 = false;
};

[[nodiscard]] auto get_supported_features() -> Result<Features>;

struct InitInfo {
  Features features;
};

[[nodiscard]] auto init(const InitInfo &init_info) -> Result<void>;

void exit();

auto get_adapter_count() -> u32;

auto get_adapter(u32 adapter) -> Adapter;

enum class AdapterPreference {
  Auto,
  LowPower,
  HighPerformance,
};

auto get_adapter_by_preference(AdapterPreference preference) -> Adapter;

struct AdapterFeatures {
  bool amd_anti_lag : 1 = false;
};

auto get_adapter_features(Adapter adapter) -> AdapterFeatures;

enum class QueueFamily {
  Graphics,
  Compute,
  Last = Compute,
};
constexpr usize QUEUE_FAMILY_COUNT = (usize)QueueFamily::Last + 1;

auto is_queue_family_supported(Adapter adapter, QueueFamily family) -> bool;

enum class MemoryHeap {
  Default,
  Upload,
  DeviceUpload,
  Readback,
  Last = Readback
};
constexpr usize MEMORY_HEAP_COUNT = (usize)MemoryHeap::Last + 1;

enum class HostPageProperty {
  NotAvailable,
  WriteCombined,
  WriteBack,
};

enum class MemoryPool {
  L0,
  L1,
};

struct MemoryHeapProperties {
  MemoryHeap heap_type = {};
  HostPageProperty host_page_property = {};
  MemoryPool memory_pool = {};
};

auto get_memory_heap_properties(Adapter adapter, MemoryHeap heap)
    -> MemoryHeapProperties;

struct DeviceCreateInfo {
  Adapter adapter;
  AdapterFeatures features;
};

auto create_device(const DeviceCreateInfo &create_info) -> Result<Device>;

void destroy_device(Device device);

auto get_queue(Device device, QueueFamily family) -> Queue;

auto map(Device device, Allocation allocation) -> void *;

// This is empty for now because buffers are just memory. RADV only treats
// acceleration structures and descriptor buffers differently, otherwise usage
// flags are ignored.
// clang-format off
REN_BEGIN_FLAGS_ENUM(BufferUsage) {
} REN_END_FLAGS_ENUM(BufferUsage);
// clang-format on

struct BufferCreateInfo {
  Device device = {};
  usize size = 0;
  BufferUsage usage = {};
  MemoryHeap heap = MemoryHeap::Default;
};

auto create_buffer(const BufferCreateInfo &create_info) -> Result<Buffer>;

void destroy_buffer(Device device, Buffer buffer);

auto get_allocation(Device device, Buffer buffer) -> Allocation;

auto get_device_ptr(Device device, Buffer buffer) -> u64;

inline auto map(Device device, Buffer buffer) -> void * {
  return map(device, get_allocation(device, buffer));
}

// clang-format off
REN_BEGIN_FLAGS_ENUM(ImageUsage) {
  REN_FLAG(TransferSrc),
  REN_FLAG(TransferDst),
  REN_FLAG(Sampled),
  REN_FLAG(Storage),
  REN_FLAG(ColorAttachment),
  REN_FLAG(DepthAttachment),
  Last = DepthAttachment,
} REN_END_FLAGS_ENUM(ImageUsage);
// clang-format on
constexpr u32 IMAGE_USAGE_COUNT = std::countr_zero((usize)ImageUsage::Last) + 1;

} // namespace ren::rhi

REN_ENABLE_FLAGS(ren::rhi::ImageUsage);

namespace ren::rhi {

using ImageUsageFlags = Flags<rhi::ImageUsage>;

struct ImageCreateInfo {
  Device device = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 0;
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
  ImageUsageFlags usage;
};

auto create_image(const ImageCreateInfo &create_info) -> Result<Image>;

void destroy_image(Device device, Image image);

auto get_allocation(Device device, Image image) -> Allocation;

enum class Filter {
  Nearest,
  Linear,
  Last = Linear,
};
constexpr usize FILTER_COUNT = (usize)Filter::Last + 1;

enum class SamplerMipmapMode {
  Nearest,
  Linear,
  Last = Linear,
};
constexpr usize SAMPLER_MIPMAP_MODE_COUNT = (usize)SamplerMipmapMode::Last + 1;

enum class SamplerAddressMode {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
  Last = ClampToEdge,
};
constexpr usize SAMPLER_ADDRESS_MODE_COUNT =
    (usize)SamplerAddressMode::Last + 1;

constexpr float LOD_CLAMP_NONE = 1000.0f;

enum class SamplerReductionMode {
  WeightedAverage,
  Min,
  Max,
  Last = Max,
};
constexpr usize SAMPLER_REDUCTION_MODE_COUNT =
    (usize)SamplerReductionMode::Last + 1;

struct SamplerCreateInfo {
  Device device = {};
  Filter mag_filter = Filter::Nearest;
  Filter min_filter = Filter::Nearest;
  SamplerMipmapMode mipmap_mode = SamplerMipmapMode::Nearest;
  SamplerAddressMode address_mode_u = SamplerAddressMode::Repeat;
  SamplerAddressMode address_mode_v = SamplerAddressMode::Repeat;
  SamplerAddressMode address_mode_w = SamplerAddressMode::Repeat;
  SamplerReductionMode reduction_mode = SamplerReductionMode::WeightedAverage;
  float mip_lod_bias = 0.0f;
  float max_anisotropy = 0.0f;
  float min_lod = 0.0f;
  float max_lod = LOD_CLAMP_NONE;
};

auto create_sampler(const SamplerCreateInfo &create_info) -> Result<Sampler>;

void destroy_sampler(Device device, Sampler sampler);

enum class SemaphoreType {
  Binary,
  Timeline,
  Last = Timeline,
};
constexpr usize SEMAPHORE_TYPE_COUNT = (usize)SemaphoreType::Last + 1;

struct SemaphoreCreateInfo {
  Device device = {};
  SemaphoreType type = SemaphoreType::Timeline;
  u64 initial_value = 0;
};

auto create_semaphore(const SemaphoreCreateInfo &create_info)
    -> Result<Semaphore>;

void destroy_semaphore(Device device, Semaphore semaphore);

enum class WaitResult {
  Success,
  Timeout,
};

struct SemaphoreWaitInfo {
  Semaphore semaphore;
  u64 value = 0;
};

auto wait_for_semaphores(Device device,
                         TempSpan<const SemaphoreWaitInfo> wait_infos,
                         std::chrono::nanoseconds timeout)
    -> Result<WaitResult>;

extern const uint32_t SDL_WINDOW_FLAGS;

auto create_surface(SDL_Window *window) -> Result<Surface>;

void destroy_surface(Surface surface);

auto is_queue_family_present_supported(Adapter adapter, QueueFamily family,
                                       Surface surface) -> bool;

enum class PresentMode {
  Immediate,
  Mailbox,
  Fifo,
  FifoRelaxed,
  Last = FifoRelaxed,
};
constexpr u32 PRESENT_MODE_COUNT = (usize)PresentMode::Last + 1;

auto get_surface_present_modes(Adapter adapter, Surface surface,
                               u32 *num_present_modes,
                               PresentMode *present_modes) -> Result<void>;

auto get_surface_formats(Adapter adapter, Surface surface, u32 *num_formats,
                         TinyImageFormat *formats) -> Result<void>;

auto get_surface_supported_image_usage(Adapter adapter, Surface surface)
    -> Result<Flags<ImageUsage>>;

struct SwapChainCreateInfo {
  rhi::Device device = {};
  rhi::Surface surface;
  rhi::Queue queue;
  u32 width = 0;
  u32 height = 0;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  Flags<ImageUsage> usage = {};
  u32 num_images = 0;
  PresentMode present_mode = PresentMode::Fifo;
};

constexpr u32 MAX_SWAP_CHAIN_IMAGE_COUNT = 8;

auto create_swap_chain(const SwapChainCreateInfo &create_info)
    -> Result<SwapChain>;

void destroy_swap_chain(SwapChain swap_chain);

auto get_swap_chain_size(SwapChain swap_chain) -> glm::uvec2;

auto get_swap_chain_images(SwapChain swap_chain, u32 *num_images, Image *images)
    -> Result<void>;

auto resize_swap_chain(SwapChain swap_chain, glm::uvec2 size, u32 num_images)
    -> Result<void>;

auto set_present_mode(SwapChain swap_chain, PresentMode present_mode)
    -> Result<void>;

auto acquire_image(SwapChain swap_chain, Semaphore semaphore) -> Result<u32>;

auto present(SwapChain swap_chain, Semaphore semaphore) -> Result<void>;

} // namespace ren::rhi
