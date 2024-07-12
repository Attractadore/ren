#pragma once
#include "Attachments.hpp"
#include "BumpAllocator.hpp"
#include "Config.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"
#include "Support/DynamicBitset.hpp"
#include "Support/FlatSet.hpp"
#include "Support/GenArray.hpp"
#include "Support/GenMap.hpp"
#include "Support/HashMap.hpp"
#include "Support/NewType.hpp"
#include "Support/String.hpp"
#include "Support/Variant.hpp"
#include "TextureIdAllocator.hpp"

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

struct RgBufferState {
  /// Pipeline stages in which this buffer is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Memory accesses performed on this buffer
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
};

constexpr auto operator|(const RgBufferState &lhs,
                         const RgBufferState &rhs) -> RgBufferState {
  return {
      .stage_mask = lhs.stage_mask | rhs.stage_mask,
      .access_mask = lhs.access_mask | rhs.access_mask,
  };
};

constexpr RgBufferState RG_HOST_WRITE_BUFFER = {};

constexpr RgBufferState RG_VS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferState RG_FS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferState RG_TRANSFER_SRC_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
};

constexpr RgBufferState RG_TRANSFER_DST_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
};

constexpr RgBufferState RG_CS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferState RG_CS_WRITE_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
};

constexpr RgBufferState RG_CS_READ_WRITE_BUFFER =
    RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER;

constexpr RgBufferState RG_INDEX_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
    .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
};

constexpr RgBufferState RG_INDIRECT_COMMAND_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
};

struct RgTextureState {
  /// Pipeline stages in which the texture is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Types of accesses performed on the texture
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  /// Layout of the texture
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr RgTextureState RG_FS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureState RG_CS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureState RG_CS_WRITE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureState RG_CS_READ_WRITE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureState RG_COLOR_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr RgTextureState RG_READ_WRITE_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr RgTextureState RG_READ_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
};

constexpr RgTextureState RG_TRANSFER_SRC_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
};

constexpr RgTextureState RG_TRANSFER_DST_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
};

constexpr RgTextureState RG_PRESENT_TEXTURE = {
    .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
};

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
using RgBufferId = Handle<RgBuffer>;

class RgBufferToken {
public:
  RgBufferToken() = default;

  explicit RgBufferToken(u32 value) { m_value = value; }

  operator u32() const { return m_value; }

  explicit operator bool() const { return *this != RgBufferToken(); }

private:
  u32 m_value = -1;
};

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
  SmallFlatSet<RgPassId> successors;
  union {
    unsigned num_predecessors = 0;
    int schedule_time;
  };
};

struct RgBufferCreateInfo {
  /// Memory heap from which to allocate buffer
  BufferHeap heap = BufferHeap::Dynamic;
  /// Initial buffer size
  usize size = 0;
};

struct RgPhysicalBuffer {
  BufferHeap heap = {};
  usize size = 0;
  BufferView view;
};

struct RgBuffer {
  RgPhysicalBufferId parent;
  RgPassId def;
  RgPassId kill;
#if REN_RG_DEBUG
  String name;
  RgBufferId child;
#endif
};

struct RgBufferUse {
  RgBufferId buffer;
  RgBufferState usage;
};

struct RgTextureExternalInfo {
  /// Texture usage
  VkImageUsageFlags usage = 0;
};

struct RgTextureTemporalInfo {
  /// Number of temporal layers.
  u32 num_temporal_layers = 0;
  /// Texture usage in init callback.
  RgTextureState usage;
  /// Init callback for temporal layers.
  RgTextureInitCallback cb;
};

struct RgTextureCreateInfo {
  /// Texture name
  REN_RG_DEBUG_NAME_TYPE name;
  /// Texture type
  VkImageType type = VK_IMAGE_TYPE_2D;
  /// Texture format
  VkFormat format = VK_FORMAT_UNDEFINED;
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
  RgTextureState usage;
  RgTextureInitCallback cb;
};

struct RgPhysicalTexture {
  REN_RG_DEBUG_NAME_TYPE name;
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  glm::uvec3 size = {};
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
  Handle<Texture> handle;
  StorageTextureId storage_descriptor;
  RgTextureState state;
  RgTextureId init_id;
  RgTextureId id;
};

struct RgTexture {
  RgPhysicalTextureId parent;
  RgPassId def;
  RgPassId kill;
#ifdef REN_RG_DEBUG
  String name;
  RgTextureId child;
#endif
};

struct RgTextureUse {
  RgTextureId texture;
  RgTextureState usage;
};

struct RgSemaphore {
  Handle<Semaphore> handle;
#if REN_RG_DEBUG
  String name;
#endif
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

struct RgRtData {
  Vector<RgRtPass> m_passes;
#if REN_RG_DEBUG
  GenMap<String, RgPassId> m_pass_names;
#endif

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  Vector<BufferView> m_buffers;

  Vector<Handle<Texture>> m_textures;
  Vector<StorageTextureId> m_texture_storage_descriptors;

  Vector<VkMemoryBarrier2> m_memory_barriers;
  Vector<VkImageMemoryBarrier2> m_texture_barriers;
  Vector<VkSemaphoreSubmitInfo> m_semaphore_submit_info;
};

class RgPersistent {
public:
  RgPersistent(Renderer &renderer,
               TextureIdAllocator &texture_descritptor_allocator);

  [[nodiscard]] auto
  create_texture(RgTextureCreateInfo &&create_info) -> RgTextureId;

  [[nodiscard]] auto
  create_external_semaphore(RgDebugName name) -> RgSemaphoreId;

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
  usize m_num_frame_physical_textures = 0;
  HashMap<RgPhysicalTextureId, RgTextureInitInfo> m_texture_init_info;
  GenArray<RgTexture> m_textures;
  Vector<RgTextureId> m_frame_textures;
  TextureIdAllocatorScope m_texture_descriptor_allocator;

  GenArray<RgSemaphore> m_semaphores;

  RgBuildData m_build_data;
  RgRtData m_rt_data;
};

class RgBuilder {
public:
  RgBuilder(RgPersistent &rgp, Renderer &renderer);

  [[nodiscard]] auto
  create_pass(RgPassCreateInfo &&create_info) -> RgPassBuilder;

  [[nodiscard]] auto
  create_buffer(RgBufferCreateInfo &&create_info) -> RgBufferId;

  void set_external_texture(RgTextureId id, Handle<Texture> texture,
                            const RgTextureState &usage = {});

  void set_external_semaphore(RgSemaphoreId id, Handle<Semaphore> semaphore);

  auto build(DeviceBumpAllocator &device_allocator,
             UploadBumpAllocator &upload_allocator) -> RenderGraph;

private:
  friend RgPassBuilder;

  [[nodiscard]] auto
  add_buffer_use(RgBufferId buffer,
                 const RgBufferState &usage) -> RgBufferUseId;

  [[nodiscard]] auto create_virtual_buffer(RgPassId pass, RgDebugName name,
                                           RgBufferId parent) -> RgBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, RgBufferId buffer,
                                 const RgBufferState &usage) -> RgBufferToken;

  [[nodiscard]] auto write_buffer(RgPassId pass, RgDebugName name,
                                  RgBufferId buffer, const RgBufferState &usage)
      -> std::tuple<RgBufferId, RgBufferToken>;

  [[nodiscard]] auto
  add_texture_use(RgTextureId texture,
                  const RgTextureState &usage) -> RgTextureUseId;

  [[nodiscard]] auto create_virtual_texture(RgPassId pass, RgDebugName name,
                                            RgTextureId parent) -> RgTextureId;

  [[nodiscard]] auto read_texture(RgPassId pass, RgTextureId texture,
                                  const RgTextureState &usage,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto write_texture(
      RgPassId pass, RgDebugName name, RgTextureId texture,
      const RgTextureState &usage) -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto
  write_texture(RgPassId pass, RgTextureId dst, RgTextureId texture,
                const RgTextureState &usage) -> RgTextureToken;

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

  void create_resources(DeviceBumpAllocator &device_allocator,
                        UploadBumpAllocator &upload_allocator);

  auto build_pass_schedule();

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
};

class RgPassBuilder {
public:
  [[nodiscard]] auto read_buffer(RgBufferId buffer,
                                 const RgBufferState &usage) -> RgBufferToken;

  [[nodiscard]] auto write_buffer(RgDebugName name, RgBufferId buffer,
                                  const RgBufferState &usage)
      -> std::tuple<RgBufferId, RgBufferToken>;

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const RgTextureState &usage,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgDebugName name, RgTextureId texture,
                                   const RgTextureState &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto write_color_attachment(
      RgDebugName name, RgTextureId texture,
      const ColorAttachmentOperations &ops,
      u32 index = 0) -> std::tuple<RgTextureId, RgTextureToken>;

  auto read_depth_attachment(RgTextureId texture,
                             u32 temporal_layer = 0) -> RgTextureToken;

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
};

class RgRuntime {
public:
  auto get_buffer(RgBufferToken buffer) const -> const BufferView &;

  template <typename T>
  auto get_buffer_device_ptr(RgBufferToken buffer,
                             usize offset = 0) const -> DevicePtr<T> {
    DevicePtr<T> ptr =
        m_rg->m_renderer->get_buffer_device_ptr<T>(get_buffer(buffer), offset);
    ren_assert(ptr);
    return ptr;
  }

  template <typename T>
  auto map_buffer(RgBufferToken buffer, usize offset = 0) const -> T * {
    return m_rg->m_renderer->map_buffer<T>(get_buffer(buffer), offset);
  }

  auto get_texture(RgTextureToken texture) const -> Handle<Texture>;

  auto get_storage_texture_descriptor(RgTextureToken texture) const
      -> StorageTextureId;

  auto get_texture_set() const -> VkDescriptorSet;

  auto get_allocator() const -> UploadBumpAllocator &;

  template <typename T = std::byte>
  auto allocate(usize count = 1) const -> UploadBumpAllocation<T> {
    return get_allocator().allocate<T>(count);
  }

private:
  friend RenderGraph;

private:
  RenderGraph *m_rg = nullptr;
};

} // namespace ren
