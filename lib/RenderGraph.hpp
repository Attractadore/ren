#pragma once
#include "Attachments.hpp"
#include "BumpAllocator.hpp"
#include "Config.hpp"
#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"
#include "Support/DynamicBitset.hpp"
#include "Support/GenArray.hpp"
#include "Support/GenMap.hpp"
#include "Support/HashMap.hpp"
#include "Support/NewType.hpp"
#include "Support/String.hpp"
#include "Support/Variant.hpp"
#include "Texture.hpp"

#include <functional>
#include <vulkan/vulkan.h>

namespace ren {

#if REN_RG_DEBUG
using RgDebugName = String;
#else
using RgDebugName = DummyString;
#endif

#define REN_RG_DEBUG_NAME_TYPE [[no_unique_address]] RgDebugName

class CommandAllocator;
class CommandRecorder;
class Swapchain;
class RenderPass;
class ComputePass;

class RgPersistent;
class RgBuilder;
class RgPassBuilder;
class RenderGraph;
class RgRuntime;

template <typename F>
concept CRgHostCallback = std::invocable<F, Renderer &, const RgRuntime &>;

using RgHostCallback = std::function<void(Renderer &, const RgRuntime &)>;
static_assert(CRgHostCallback<RgHostCallback>);

template <typename F>
concept CRgGraphicsCallback =
    std::invocable<F, Renderer &, const RgRuntime &, RenderPass &>;

using RgGraphicsCallback =
    std::function<void(Renderer &, const RgRuntime &, RenderPass &)>;
static_assert(CRgGraphicsCallback<RgGraphicsCallback>);

template <typename F>
concept CRgComputeCallback =
    std::invocable<F, Renderer &, const RgRuntime &, ComputePass &>;

using RgComputeCallback =
    std::function<void(Renderer &, const RgRuntime &, ComputePass &)>;
static_assert(CRgComputeCallback<RgComputeCallback>);

template <typename F>
concept CRgCallback =
    std::invocable<F, Renderer &, const RgRuntime &, CommandRecorder &>;

using RgCallback =
    std::function<void(Renderer &, const RgRuntime &, CommandRecorder &)>;
static_assert(CRgCallback<RgCallback>);

template <typename F>
concept CRgTextureInitCallback =
    std::invocable<F, Handle<Texture>, Renderer &, CommandRecorder &>;

using RgTextureInitCallback =
    std::function<void(Handle<Texture>, Renderer &, CommandRecorder &)>;
static_assert(CRgTextureInitCallback<RgTextureInitCallback>);

struct RgPass;
using RgPassId = Handle<RgPass>;

REN_NEW_TYPE(RgPhysicalBufferId, u32);

struct RgBuffer;
REN_NEW_TEMPLATE_TYPE(RgBufferId, Handle<RgBuffer>, T);
using RgUntypedBufferId = Handle<RgBuffer>;

template <typename T> class RgBufferToken {
public:
  RgBufferToken() = default;
  template <typename U>
    requires std::same_as<T, std::byte>
  RgBufferToken(RgBufferToken<U> other) {
    m_value = other;
  }

  explicit RgBufferToken(u32 value) { m_value = value; }

  operator u32() const { return m_value; }

  explicit operator bool() const { return *this != RgBufferToken(); }

private:
  u32 m_value = -1;
};
using RgUntypedBufferToken = RgBufferToken<std::byte>;

REN_NEW_TYPE(RgBufferUseId, u32);

REN_NEW_TYPE(RgPhysicalTextureId, u32);

struct RgTexture;
using RgTextureId = Handle<RgTexture>;

constexpr u32 RG_MAX_TEMPORAL_LAYERS = 4;

class RgTextureToken {
public:
  RgTextureToken() = default;

  explicit RgTextureToken(u32 value) { m_value = value; }

  operator u32() const { return m_value; }

  explicit operator bool() const { return *this != RgTextureToken(); }

private:
  u32 m_value = -1;
};

REN_NEW_TYPE(RgTextureUseId, u32);

struct RgSemaphore;
using RgSemaphoreId = Handle<RgSemaphore>;

REN_NEW_TYPE(RgSemaphoreSignalId, u32);

struct RgPassCreateInfo {
  REN_RG_DEBUG_NAME_TYPE name;
};

struct RgHostPass {
  RgHostCallback cb;
};

struct RgColorAttachment {
  RgTextureToken texture;
  ColorAttachmentOperations ops;
};

struct RgDepthStencilAttachment {
  RgTextureToken texture;
  Optional<DepthAttachmentOperations> depth_ops;
  Optional<StencilAttachmentOperations> stencil_ops;
};

struct RgGraphicsPass {
  StaticVector<Optional<RgColorAttachment>, MAX_COLOR_ATTACHMENTS>
      color_attachments;
  Optional<RgDepthStencilAttachment> depth_stencil_attachment;
  RgGraphicsCallback cb;
};

struct RgComputePass {
  RgComputeCallback cb;
};

struct RgGenericPass {
  RgCallback cb;
};

struct RgPass {
  SmallVector<RgBufferUseId> read_buffers;
  SmallVector<RgBufferUseId> write_buffers;
  SmallVector<RgTextureUseId> read_textures;
  SmallVector<RgTextureUseId> write_textures;
  SmallVector<RgSemaphoreSignalId> wait_semaphores;
  SmallVector<RgSemaphoreSignalId> signal_semaphores;
  Variant<Monostate, RgHostPass, RgGraphicsPass, RgComputePass, RgGenericPass>
      ext;
};

struct RgBufferCreateInfo {
  /// Buffer name.
  REN_RG_DEBUG_NAME_TYPE name;
  /// Memory heap from which to allocate buffer.
  BufferHeap heap = BufferHeap::Dynamic;
  /// Buffer size.
  union {
    usize size = 1;
    usize count;
  };
};

struct RgPhysicalBuffer {
  BufferHeap heap = {};
  usize size = 0;
  BufferView view;
  BufferState state;
};

struct RgBuffer {
#if REN_RG_DEBUG
  String name;
#endif
  RgPhysicalBufferId parent;
  RgPassId def;
  RgPassId kill;
#if REN_RG_DEBUG
  RgUntypedBufferId child;
#endif
};

struct RgBufferUse {
  RgUntypedBufferId buffer;
  BufferState usage;
};

struct RgTextureExternalInfo {
  /// Texture usage
  VkImageUsageFlags usage = 0;
};

struct RgTextureTemporalInfo {
  /// Number of temporal layers.
  u32 num_temporal_layers = 0;
  /// Texture usage in init callback.
  TextureState usage;
  /// Init callback for temporal layers.
  RgTextureInitCallback cb;
};

struct RgTextureCreateInfo {
  /// Texture name
  REN_RG_DEBUG_NAME_TYPE name;
  /// Texture type
  VkImageType type = VK_IMAGE_TYPE_2D;
  /// Texture format
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  /// Texture width
  u32 width = 0;
  /// Texture height
  u32 height = 1;
  /// Texture depth
  u32 depth = 1;
  /// Number of mip levels
  u32 num_mip_levels = 1;
  /// Number of array layers
  u32 num_array_layers = 1;
  /// Additional create info.
  Variant<Monostate, RgTextureTemporalInfo, RgTextureExternalInfo> ext;
};

struct RgTextureInitInfo {
  TextureState usage;
  RgTextureInitCallback cb;
};

struct RgPhysicalTexture {
#if REN_RG_DEBUG
  String name;
#endif
  VkImageType type = VK_IMAGE_TYPE_2D;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  VkImageUsageFlags usage = 0;
  glm::uvec3 size = {};
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
  Handle<Texture> handle;
  TextureState state;
  RgTextureId init_id;
  RgTextureId id;
};

struct RgTexture {
#if REN_RG_DEBUG
  String name;
#endif
  RgPhysicalTextureId parent;
  RgPassId def;
  RgPassId kill;
#if REN_RG_DEBUG
  RgTextureId child;
#endif
};

struct RgTextureUse {
  RgTextureId texture;
  Handle<Sampler> sampler;
  TextureState state;
};

struct RgSemaphore {
#if REN_RG_DEBUG
  String name;
#endif
  Handle<Semaphore> handle;
};

struct RgSemaphoreSignal {
  RgSemaphoreId semaphore;
  VkPipelineStageFlagBits2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  u64 value = 0;
};

struct RgBuildData {
  GenArray<RgPass> m_passes;
  Vector<RgPassId> m_schedule;

  Vector<RgPhysicalBuffer> m_physical_buffers;
  GenArray<RgBuffer> m_buffers;
  Vector<RgBufferUse> m_buffer_uses;

  Vector<RgTextureUse> m_texture_uses;

  Vector<RgSemaphoreSignal> m_semaphore_signals;
};

struct RgRtHostPass {
  RgHostCallback cb;
};

struct RgRtGraphicsPass {
  u32 base_color_attachment = 0;
  u32 num_color_attachments = 0;
  Optional<u32> depth_attachment;
  RgGraphicsCallback cb;
};

struct RgRtComputePass {
  RgComputeCallback cb;
};

struct RgRtGenericPass {
  RgCallback cb;
};

struct RgRtPass {
  RgPassId pass;
  u32 base_memory_barrier = 0;
  u32 num_memory_barriers = 0;
  u32 base_texture_barrier = 0;
  u32 num_texture_barriers = 0;
  u32 base_wait_semaphore = 0;
  u32 num_wait_semaphores = 0;
  u32 base_signal_semaphore = 0;
  u32 num_signal_semaphores = 0;
  Variant<RgRtHostPass, RgRtGraphicsPass, RgRtComputePass, RgRtGenericPass> ext;
};

struct RgTextureDescriptors {
  glsl::Texture sampled;
  glsl::SampledTexture combined;
  glsl::RWStorageTexture *storage = nullptr;
};

struct RgRtData {
  Vector<RgRtPass> m_passes;
#if REN_RG_DEBUG
  GenMap<String, RgPassId> m_pass_names;
#endif
  Vector<VkCommandBufferSubmitInfo> m_batch_cmd_buffers;

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  Vector<BufferView> m_buffers;

  Vector<Handle<Texture>> m_textures;
  Vector<RgTextureDescriptors> m_texture_descriptors;
  Vector<glsl::RWStorageTexture> m_storage_texture_descriptors;

  Vector<VkMemoryBarrier2> m_memory_barriers;
  Vector<VkImageMemoryBarrier2> m_texture_barriers;
  Vector<VkSemaphoreSubmitInfo> m_semaphore_submit_info;
};

class RgPersistent {
public:
  RgPersistent(Renderer &renderer);

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info)
      -> RgTextureId;

  [[nodiscard]] auto create_external_semaphore(RgDebugName name)
      -> RgSemaphoreId;

  void reset();

private:
  friend class RgBuilder;
  friend class RenderGraph;
  using TextureArena = detail::ResourceArenaImpl<Texture>;

  void rotate_textures();

private:
  TextureArena m_texture_arena;
  Vector<RgPhysicalTexture> m_physical_textures;
  DynamicBitset m_persistent_textures;
  DynamicBitset m_external_textures;
  GenArray<RgTexture> m_textures;

  HashMap<RgPhysicalTextureId, RgTextureInitInfo> m_texture_init_info;

  TextureArena m_prev_texture_arena;
  usize m_num_prev_physical_textures = 0;

  Vector<RgTextureId> m_frame_textures;

  GenArray<RgSemaphore> m_semaphores;

  RgBuildData m_build_data;
  RgRtData m_rt_data;
};

class RgBuilder {
public:
  RgBuilder(RgPersistent &rgp, Renderer &renderer,
            DescriptorAllocatorScope &descriptor_allocator);

  [[nodiscard]] auto create_pass(RgPassCreateInfo &&create_info)
      -> RgPassBuilder;

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info)
      -> RgUntypedBufferId;

  template <typename T>
  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info)
      -> RgBufferId<T> {
    create_info.size = create_info.count * sizeof(T);
    return RgBufferId<T>(create_buffer(std::move(create_info)));
  }

  template <typename T>
  [[nodiscard]] auto create_buffer(RgDebugName name,
                                   const StatefulBufferSlice<T> &slice)
      -> RgBufferId<T> {
    RgBufferId<T> buffer = create_buffer<T>({
        .name = std::move(name),
        .heap = m_renderer->get_buffer(slice.slice.buffer).heap,
        .count = slice.slice.count,
    });
    set_external_buffer(buffer, BufferView(slice.slice), slice.state);
    return buffer;
  }

  void set_external_buffer(RgUntypedBufferId id, const BufferView &view,
                           const BufferState &usage = {});

  void set_external_texture(RgTextureId id, Handle<Texture> texture,
                            const TextureState &usage = {});

  void set_external_semaphore(RgSemaphoreId id, Handle<Semaphore> semaphore);

  auto build(DeviceBumpAllocator &device_allocator,
             UploadBumpAllocator &upload_allocator) -> RenderGraph;

  auto get_final_buffer_state(RgUntypedBufferId buffer) const -> BufferState;

private:
  friend RgPassBuilder;

  [[nodiscard]] auto add_buffer_use(RgUntypedBufferId buffer,
                                    const BufferState &usage) -> RgBufferUseId;

  [[nodiscard]] auto create_virtual_buffer(RgPassId pass, RgDebugName name,
                                           RgUntypedBufferId parent)
      -> RgUntypedBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, RgUntypedBufferId buffer,
                                 const BufferState &usage)
      -> RgUntypedBufferToken;

  [[nodiscard]] auto write_buffer(RgPassId pass, RgDebugName name,
                                  RgUntypedBufferId buffer,
                                  const BufferState &usage)
      -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken>;

  [[nodiscard]] auto add_texture_use(RgTextureId texture,
                                     const TextureState &usage,
                                     Handle<Sampler> sampler = NullHandle)
      -> RgTextureUseId;

  [[nodiscard]] auto create_virtual_texture(RgPassId pass, RgDebugName name,
                                            RgTextureId parent) -> RgTextureId;

  [[nodiscard]] auto read_texture(RgPassId pass, RgTextureId texture,
                                  const TextureState &usage, Handle<Sampler>,
                                  u32 temporal_layer) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgPassId pass, RgDebugName name,
                                   RgTextureId texture,
                                   const TextureState &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto write_texture(RgPassId pass, RgTextureId dst,
                                   RgTextureId texture,
                                   const TextureState &usage) -> RgTextureToken;

  [[nodiscard]] auto add_semaphore_signal(RgSemaphoreId semaphore,
                                          VkPipelineStageFlags2 stage_mask,
                                          u64 value) -> RgSemaphoreSignalId;

  void wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                      VkPipelineStageFlags2 stage_mask, u64 value);

  void signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                        VkPipelineStageFlags2 stage_mask, u64 value);

  void set_host_callback(RgPassId id, CRgHostCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(!pass.ext);
    pass.ext = RgHostPass{.cb = std::move(cb)};
  }

  void set_graphics_callback(RgPassId id, CRgGraphicsCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(!pass.ext or pass.ext.get<RgGraphicsPass>());
    pass.ext.get_or_emplace<RgGraphicsPass>().cb = std::move(cb);
  }

  void set_compute_callback(RgPassId id, CRgComputeCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(!pass.ext);
    pass.ext = RgComputePass{.cb = std::move(cb)};
  }

  void set_callback(RgPassId id, CRgCallback auto cb) {
    RgPass &pass = m_data->m_passes[id];
    ren_assert(!pass.ext);
    pass.ext = RgGenericPass{.cb = std::move(cb)};
  }

  void alloc_textures();

  void alloc_buffers(DeviceBumpAllocator &device_allocator,
                     UploadBumpAllocator &upload_allocator);

  void dump_pass_schedule() const;

  void init_runtime_passes();

  void init_runtime_buffers();

  void init_runtime_textures();

  void place_barriers_and_semaphores();

private:
  Renderer *m_renderer = nullptr;
  RgPersistent *m_rgp = nullptr;
  RgBuildData *m_data = nullptr;
  RgRtData *m_rt_data = nullptr;
  DescriptorAllocatorScope *m_descriptor_allocator = nullptr;
};

class RgPassBuilder {
public:
  [[nodiscard]] auto read_buffer(RgUntypedBufferId buffer,
                                 const BufferState &usage)
      -> RgUntypedBufferToken;

  template <typename T>
  [[nodiscard]] auto read_buffer(RgBufferId<T> buffer, const BufferState &usage)
      -> RgBufferToken<T> {
    return RgBufferToken<T>(read_buffer(RgUntypedBufferId(buffer), usage));
  }

  [[nodiscard]] auto write_buffer(RgDebugName name, RgUntypedBufferId buffer,
                                  const BufferState &usage)
      -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken>;

  template <typename T>
  [[nodiscard]] auto write_buffer(RgDebugName name, RgBufferId<T> buffer,
                                  const BufferState &usage)
      -> std::tuple<RgBufferId<T>, RgBufferToken<T>> {
    auto [id, token] =
        write_buffer(std::move(name), RgUntypedBufferId(buffer), usage);
    return {RgBufferId<T>(id), RgBufferToken<T>(token)};
  }

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const TextureState &usage,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const TextureState &usage,
                                  Handle<Sampler> sampler,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgDebugName name, RgTextureId texture,
                                   const TextureState &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto
  write_color_attachment(RgDebugName name, RgTextureId texture,
                         const ColorAttachmentOperations &ops, u32 index = 0)
      -> std::tuple<RgTextureId, RgTextureToken>;

  auto read_depth_attachment(RgTextureId texture, u32 temporal_layer = 0)
      -> RgTextureToken;

  auto write_depth_attachment(RgDebugName name, RgTextureId texture,
                              const DepthAttachmentOperations &ops)
      -> std::tuple<RgTextureId, RgTextureToken>;

  void
  wait_semaphore(RgSemaphoreId semaphore,
                 VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE,
                 u64 value = 0);

  void
  signal_semaphore(RgSemaphoreId semaphore,
                   VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE,
                   u64 value = 0);

  void set_host_callback(CRgHostCallback auto cb) {
    m_builder->set_host_callback(m_pass, std::move(cb));
  }

  void set_graphics_callback(CRgGraphicsCallback auto cb) {
    m_builder->set_graphics_callback(m_pass, std::move(cb));
  }

  void set_compute_callback(CRgComputeCallback auto cb) {
    m_builder->set_compute_callback(m_pass, std::move(cb));
  }

  void set_callback(CRgCallback auto cb) {
    m_builder->set_callback(m_pass, std::move(cb));
  }

private:
  friend class RgBuilder;

  RgPassBuilder(RgPassId pass, RgBuilder &builder);

  void add_color_attachment(u32 index, RgTextureToken texture,
                            const ColorAttachmentOperations &ops);

  void add_depth_attachment(RgTextureToken texture,
                            const DepthAttachmentOperations &ops);

private:
  RgPassId m_pass;
  RgBuilder *m_builder = nullptr;
};

class RenderGraph {
public:
  void execute(CommandAllocator &cmd_allocator);

private:
  friend RgBuilder;
  friend RgRuntime;

private:
  Renderer *m_renderer = nullptr;
  RgPersistent *m_rgp = nullptr;
  RgRtData *m_data = nullptr;
  UploadBumpAllocator *m_upload_allocator = nullptr;
  VkDescriptorSet m_texture_set = nullptr;
  const GenArray<RgSemaphore> *m_semaphores = nullptr;
};

class RgRuntime {
public:
  auto get_buffer(RgUntypedBufferToken buffer) const -> const BufferView &;

  template <typename T>
  auto get_buffer(RgBufferToken<T> buffer) const -> BufferSlice<T> {
    return BufferSlice<T>(get_buffer(RgUntypedBufferToken(buffer)));
  }

  template <typename T>
  auto get_buffer_device_ptr(RgUntypedBufferToken buffer) const
      -> DevicePtr<T> {
    DevicePtr<T> ptr = m_rg->m_renderer->get_buffer_device_ptr(
        BufferSlice<T>(get_buffer(buffer)));
    ren_assert(ptr);
    return ptr;
  }

  template <typename T>
  auto get_buffer_device_ptr(RgBufferToken<T> buffer) const -> DevicePtr<T> {
    return get_buffer_device_ptr<T>(RgUntypedBufferToken(buffer));
  }

  template <typename T>
  auto try_get_buffer_device_ptr(RgUntypedBufferToken buffer) const
      -> DevicePtr<T> {
    if (!buffer) {
      return {};
    }
    DevicePtr<T> ptr = m_rg->m_renderer->get_buffer_device_ptr(
        BufferSlice<T>(get_buffer(buffer)));
    ren_assert(ptr);
    return ptr;
  }

  template <typename T>
  auto try_get_buffer_device_ptr(RgBufferToken<T> buffer) const
      -> DevicePtr<T> {
    return try_get_buffer_device_ptr<T>(RgUntypedBufferToken(buffer));
  }

  template <typename T>
  auto map_buffer(RgUntypedBufferToken buffer) const -> T * {
    return m_rg->m_renderer->map_buffer(BufferSlice<T>(get_buffer(buffer)));
  }

  template <typename T> auto map_buffer(RgBufferToken<T> buffer) const -> T * {
    return map_buffer<T>(RgUntypedBufferToken(buffer));
  }

  auto get_texture(RgTextureToken texture) const -> Handle<Texture>;

  auto get_texture_descriptor(RgTextureToken texture) const -> glsl::Texture;

  auto get_sampled_texture_descriptor(RgTextureToken texture) const
      -> glsl::SampledTexture;

  auto try_get_sampled_texture_descriptor(RgTextureToken texture) const
      -> glsl::SampledTexture;

  auto get_storage_texture_descriptor(RgTextureToken texture, u32 mip = 0) const
      -> glsl::RWStorageTexture;

  auto get_texture_set() const -> VkDescriptorSet;

  auto get_allocator() const -> UploadBumpAllocator &;

  template <typename T = std::byte>
  auto allocate(usize count = 1) const -> UploadBumpAllocation<T> {
    return get_allocator().allocate<T>(count);
  }

  auto get_semaphore(RgSemaphoreId semaphore) const -> Handle<Semaphore>;

private:
  friend RenderGraph;

private:
  RenderGraph *m_rg = nullptr;
};

} // namespace ren
