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

auto queue_wait_idle(Queue queue) -> Result<void>;

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

enum class ImageViewType {
  SRV,
  UAV,
  RTV,
};

union ImageView {
  SRV srv;
  UAV uav;
  RTV rtv;
};

enum class ImageViewDimension {
  e1D,
  e2D,
  eCube,
  e3D,
  Last = e3D,
};
constexpr usize IMAGE_VIEW_DIMENSION_COUNT =
    (usize)ImageViewDimension::Last + 1;

enum class ComponentSwizzle : u8 {
  Identity,
  Zero,
  One,
  R,
  G,
  B,
  A,
  Last = A,
};
constexpr usize COMPONENT_SWIZZLE_COUNT = (usize)ComponentSwizzle::Last + 1;

struct ComponentMapping {
  ComponentSwizzle r : 4 = ComponentSwizzle::Identity;
  ComponentSwizzle g : 4 = ComponentSwizzle::Identity;
  ComponentSwizzle b : 4 = ComponentSwizzle::Identity;
  ComponentSwizzle a : 4 = ComponentSwizzle::Identity;

public:
  bool operator==(const ComponentMapping &) const = default;
};

struct SrvCreateInfo {
  Image image = {};
  ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  ComponentMapping components;
  u32 first_mip_level = 0;
  u32 num_mip_levels = 1;
  u32 first_array_layer = 0;
  u32 num_array_layers = 1;
};

auto create_srv(Device device, const SrvCreateInfo &create_info) -> Result<SRV>;

void destroy_srv(Device device, SRV srv);

struct UavCreateInfo {
  Image image = {};
  ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  u32 mip_level = 0;
  u32 first_array_layer = 0;
  u32 num_array_layers = 1;
};

auto create_uav(Device device, const UavCreateInfo &create_info) -> Result<UAV>;

void destroy_uav(Device device, UAV uav);

struct RtvCreateInfo {
  Image image = {};
  ImageViewDimension dimension = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  u32 mip_level = 0;
  u32 first_array_layer = 0;
  u32 num_array_layers = 1;
};

auto create_rtv(Device device, const RtvCreateInfo &create_info) -> Result<RTV>;

void destroy_rtv(Device device, RTV rtv);

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

struct ResourceDescriptorHeapCreateInfo {
  union {
    struct {
      u32 num_srv_descriptors = 0;
      u32 num_cis_descriptors = 0;
      u32 num_uav_descriptors = 0;
    };
    std::array<u32, 3> num_descriptors;
  };
};

auto create_resource_descriptor_heap(
    Device device, const ResourceDescriptorHeapCreateInfo &create_info)
    -> Result<ResourceDescriptorHeap>;

void destroy_resource_descriptor_heap(Device device,
                                      ResourceDescriptorHeap heap);

void write_resource_descriptor_heap(Device device, ResourceDescriptorHeap heap,
                                    TempSpan<const SRV> srvs, u32 index);

void write_resource_descriptor_heap(Device device, ResourceDescriptorHeap heap,
                                    TempSpan<const SRV> srvs,
                                    TempSpan<const Sampler> samplers,
                                    u32 index);

void write_resource_descriptor_heap(Device device, ResourceDescriptorHeap heap,
                                    TempSpan<const UAV> uavs, u32 index);

auto create_sampler_descriptor_heap(Device device)
    -> Result<SamplerDescriptorHeap>;

void destroy_sampler_descriptor_heap(Device device, SamplerDescriptorHeap heap);

void write_sampler_descriptor_heap(Device device, SamplerDescriptorHeap heap,
                                   TempSpan<const Sampler> samplers, u32 index);

enum class PushDescriptorType {
  SRV,
  UAV,
  CBV,
};

struct PushDescriptor {
  PushDescriptorType type = {};
  u32 binding = 0;
};

struct PipelineLayoutCreateInfo {
  bool use_resource_heap = false;
  bool use_sampler_heap = false;
  TempSpan<const PushDescriptor> push_descriptors = {};
  u32 push_constants_size = 0;
};

auto create_pipeline_layout(Device device,
                            const PipelineLayoutCreateInfo &create_info)
    -> Result<PipelineLayout>;

void destroy_pipeline_layout(Device device, PipelineLayout layout);

struct SpecializationConstant {
  u32 id = 0;
  u32 offset = 0;
  u32 size = 0;
};

struct SpecializationInfo {
  Span<const SpecializationConstant> constants;
  Span<const std::byte> data;
};

struct ShaderInfo {
  Span<const std::byte> code;
  const char *entry_point = "main";
  SpecializationInfo specialization;
};

enum class PrimitiveTopology {
  PointList,
  LineList,
  TriangleList,
  Last = TriangleList,
};
constexpr usize PRIMITIVE_TOPOLOGY_COUNT = (usize)PrimitiveTopology::Last + 1;

struct InputAssemblyStateInfo {
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
};

enum class FillMode {
  Fill,
  Wireframe,
  Last = Wireframe,
};
constexpr usize FILL_MODE_COUNT = (usize)FillMode::Last + 1;

enum class CullMode {
  None,
  Front,
  Back,
  Last = Back,
};
constexpr usize CULL_MODE_COUNT = (usize)CullMode::Last + 1;

enum class FrontFace {
  CCW,
  CW,
  Last = CW,
};
constexpr usize FRONT_FACE_COUNT = (usize)FrontFace::Last + 1;

struct RasterizationStateInfo {
  FillMode fill_mode = FillMode::Fill;
  CullMode cull_mode = CullMode::None;
  FrontFace front_face = FrontFace::CCW;
  bool depth_bias_enable = false;
  i32 depth_bias_constant_factor = 0;
  float depth_bias_clamp = 0.0f;
  float depth_bias_slope_factor = 0.0f;
  // Disable depth clip and enable depth clamp.
  bool depth_clamp_enable = false;
};

enum class SampleCount {
  e1 = 1,
  e2 = 2,
  e4 = 4,
  e8 = 8,
};

struct MultisamplingStateInfo {
  SampleCount sample_count = SampleCount::e1;
  u32 sample_mask = -1;
  bool alpha_to_coverage_enable = false;
};

enum class CompareOp {
  Never,
  Less,
  Equal,
  LessOrEqual,
  Greater,
  NotEqual,
  GreaterOrEqual,
  Always,
  Last = Always,
};
constexpr usize COMPARE_OP_COUNT = (usize)CompareOp::Last + 1;

struct DepthStencilStateInfo {
  bool depth_test_enable = false;
  bool depth_write_enable = true;
  CompareOp depth_compare_op = CompareOp::GreaterOrEqual;
  bool depth_bounds_test_enable = false;
  float min_depth_bounds = 0.0f;
  float max_depth_bounds = 1.0f;
};

constexpr usize MAX_NUM_RENDER_TARGETS = 8;

enum class BlendFactor {
  Zero,
  One,
  SrcColor,
  OneMinusSrcColor,
  DstColor,
  OneMinusDstColor,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha,
  ConstantColor,
  OneMinusConstantColor,
  ConstantAlpha,
  OneMinusConstantAlpha,
  SrcAlphaSaturate,
  Src1Color,
  OneMinusSrc1Color,
  Src1Alpha,
  OneMinusSrc1Alpha,
  Last = OneMinusSrc1Alpha,
};
constexpr usize BLEND_FACTOR_COUNT = (usize)BlendFactor::Last + 1;

enum class BlendOp {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
  Last = Max,
};
constexpr usize BLEND_OP_COUNT = (usize)BlendOp::Last + 1;

// clang-format off
REN_BEGIN_FLAGS_ENUM(ColorComponent) {
  REN_FLAG(R),
  REN_FLAG(G),
  REN_FLAG(B),
  REN_FLAG(A),
  Last = A,
} REN_END_FLAGS_ENUM(ColorComponent);
// clang-format on
constexpr usize COLOR_COMPONENT_COUNT =
    std::countr_zero((usize)ColorComponent::Last) + 1;

} // namespace ren::rhi

REN_ENABLE_FLAGS(ren::rhi::ColorComponent);

namespace ren::rhi {

using ColorComponentMask = Flags<ColorComponent>;

struct RenderTargetBlendInfo {
  bool blend_enable = false;
  BlendFactor src_color_blend_factor = {};
  BlendFactor dst_color_blend_factor = {};
  BlendOp color_blend_op = {};
  BlendFactor src_alpha_blend_factor = {};
  BlendFactor dst_alpha_blend_factor = {};
  BlendOp alpha_blend_op = {};
  ColorComponentMask color_write_mask = ColorComponent::R | ColorComponent::G |
                                        ColorComponent::B | ColorComponent::A;
};

enum class LogicOp {
  Clear,
  And,
  AndReverse,
  Copy,
  AndInverted,
  NoOp,
  Xor,
  Or,
  Nor,
  Equivalent,
  Invert,
  OrReverse,
  CopyInverted,
  OrInverted,
  Nand,
  Set,
  Last = Set,
};
constexpr usize LOGIC_OP_COUNT = (usize)LogicOp::Last + 1;

struct BlendStateInfo {
  bool logic_op_enable = false;
  LogicOp logic_op = {};
  RenderTargetBlendInfo targets[MAX_NUM_RENDER_TARGETS] = {};
  glm::vec4 blend_constants = {};
};

struct GraphicsPipelineCreateInfo {
  PipelineLayout layout;
  ShaderInfo ts;
  ShaderInfo ms;
  ShaderInfo vs;
  ShaderInfo fs;
  InputAssemblyStateInfo input_assembly_state;
  RasterizationStateInfo rasterization_state;
  MultisamplingStateInfo multisampling_state;
  DepthStencilStateInfo depth_stencil_state;
  u32 num_render_targets = 0;
  TinyImageFormat rtv_formats[MAX_NUM_RENDER_TARGETS] = {};
  TinyImageFormat dsv_format = TinyImageFormat_UNDEFINED;
  BlendStateInfo blend_state;
};

struct ComputePipelineCreateInfo {
  PipelineLayout layout;
  ShaderInfo cs;
};

auto create_graphics_pipeline(Device device,
                              const GraphicsPipelineCreateInfo &create_info)
    -> Result<Pipeline>;

auto create_compute_pipeline(Device device,
                             const ComputePipelineCreateInfo &create_info)
    -> Result<Pipeline>;

void destroy_pipeline(Device device, Pipeline pipeline);

enum class PipelineBindPoint {
  Graphics,
  Compute,
  Last = Compute,
};
constexpr usize PIPELINE_BIND_POINT_COUNT = (usize)PipelineBindPoint::Last + 1;

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
