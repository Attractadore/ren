#pragma once
#include "Attachments.hpp"
#include "Config.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"
#include "Support/Any.hpp"
#include "Support/DynamicBitset.hpp"
#include "Support/NewType.hpp"
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

class RenderGraph;
class RgBuilder;
class RgPassBuilder;
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

constexpr u32 RG_MAX_TEMPORAL_LAYERS = 4;

REN_NEW_TYPE(RgPassId, u32);

struct RgPassCreateInfo {
  REN_RG_DEBUG_NAME_TYPE name;
};

REN_NEW_TYPE(RgPhysicalVariableId, u32);
REN_NEW_TEMPLATE_TYPE(RgVariableId, u32, T);
using RgGenericVariableId = RgVariableId<void>;
REN_NEW_TEMPLATE_TYPE(RgVariableToken, u32, T);
using RgGenericVariableToken = RgVariableToken<void>;
REN_NEW_TEMPLATE_TYPE(RgRWVariableToken, u32, T);

REN_NEW_TYPE(RgPhysicalBufferId, u32);
REN_NEW_TYPE(RgBufferId, u32);
REN_NEW_TYPE(RgBufferToken, u32);

struct RgBufferUsage {
  /// Pipeline stages in which this buffer is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Memory accesses performed on this buffer
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
};

constexpr auto operator|(const RgBufferUsage &lhs, const RgBufferUsage &rhs)
    -> RgBufferUsage {
  return {
      .stage_mask = lhs.stage_mask | rhs.stage_mask,
      .access_mask = lhs.access_mask | rhs.access_mask,
  };
};

constexpr RgBufferUsage RG_HOST_WRITE_BUFFER = {};

constexpr RgBufferUsage RG_VS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferUsage RG_FS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferUsage RG_TRANSFER_SRC_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
};

constexpr RgBufferUsage RG_TRANSFER_DST_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
};

constexpr RgBufferUsage RG_CS_READ_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
};

constexpr RgBufferUsage RG_CS_WRITE_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
};

constexpr RgBufferUsage RG_CS_READ_WRITE_BUFFER =
    RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER;

constexpr RgBufferUsage RG_INDEX_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
    .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
};

constexpr RgBufferUsage RG_INDIRECT_COMMAND_BUFFER = {
    .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
};

struct RgBufferCreateInfo {
  /// Buffer name
  REN_RG_DEBUG_NAME_TYPE name;
  /// Memory heap from which to allocate buffer
  BufferHeap heap = BufferHeap::Dynamic;
  /// Initial buffer size
  usize size = 0;
};

REN_NEW_TYPE(RgPhysicalTextureId, u32);
REN_NEW_TYPE(RgTextureId, u32);
REN_NEW_TYPE(RgTextureToken, u32);

struct RgTextureUsage {
  /// Pipeline stages in which the texture is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Types of accesses performed on the texture
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  /// Layout of the texture
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr RgTextureUsage RG_FS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureUsage RG_CS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureUsage RG_CS_WRITE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureUsage RG_CS_READ_WRITE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr RgTextureUsage RG_COLOR_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr RgTextureUsage RG_READ_WRITE_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr RgTextureUsage RG_READ_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
};

constexpr RgTextureUsage RG_TRANSFER_SRC_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
};

constexpr RgTextureUsage RG_TRANSFER_DST_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
};

constexpr RgTextureUsage RG_PRESENT_TEXTURE = {
    .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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
  /// Number of temporal layers
  u32 num_temporal_layers = 1;
  /// Texture usage in init callback
  RgTextureUsage init_usage;
  /// Init callback for temporal layers
  RgTextureInitCallback init_cb;
};

struct RgExternalTextureCreateInfo {
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
};

REN_NEW_TYPE(RgSemaphoreId, u32);

struct RgSemaphoreCreateInfo {
  /// Semaphore name.
  REN_RG_DEBUG_NAME_TYPE name;
  /// Semaphore type.
  VkSemaphoreType type = VK_SEMAPHORE_TYPE_BINARY;
};

struct RgBufferUse {
  RgBufferId buffer;
  RgBufferUsage usage;
};

REN_NEW_TYPE(RgBufferUseId, u32);

struct RgTextureUse {
  RgTextureId texture;
  RgTextureUsage usage;
};

REN_NEW_TYPE(RgTextureUseId, u32);

struct RgSemaphoreSignal {
  RgSemaphoreId semaphore;
  VkPipelineStageFlagBits2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  u64 value = 0;
};

REN_NEW_TYPE(RgSemaphoreSignalId, u32);

struct RgHostPassInfo {
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

struct RgGraphicsPassInfo {
  StaticVector<Optional<RgColorAttachment>, MAX_COLOR_ATTACHMENTS>
      color_attachments;
  Optional<RgDepthStencilAttachment> depth_stencil_attachment;
  RgGraphicsCallback cb;
};

struct RgComputePassInfo {
  RgComputeCallback cb;
};

struct RgGenericPassInfo {
  RgCallback cb;
};

struct RgHostPass {
  RgHostCallback cb;
};

struct RgGraphicsPass {
  u32 base_color_attachment = 0;
  u32 num_color_attachments = 0;
  Optional<u32> depth_attachment;
  RgGraphicsCallback cb;
};

struct RgComputePass {
  RgComputeCallback cb;
};

struct RgGenericPass {
  RgCallback cb;
};

struct RgPassRuntimeInfo {
  RgPassId pass;
  u32 base_memory_barrier = 0;
  u32 num_memory_barriers = 0;
  u32 base_texture_barrier = 0;
  u32 num_texture_barriers = 0;
  SmallVector<RgGenericVariableId> read_variables;
  SmallVector<RgGenericVariableId> write_variables;
  SmallVector<RgBufferUseId> read_buffers;
  SmallVector<RgBufferUseId> write_buffers;
  SmallVector<RgTextureUseId> read_textures;
  SmallVector<RgTextureUseId> write_textures;
  SmallVector<RgSemaphoreSignalId> wait_semaphores;
  SmallVector<RgSemaphoreSignalId> signal_semaphores;
  Variant<RgHostPass, RgGraphicsPass, RgComputePass, RgGenericPass> data;
};

class RenderGraph {
public:
  RenderGraph(Renderer &renderer, TextureIdAllocator &tex_alloc);

  void set_texture(RgTextureId id, Handle<Texture> texture,
                   const RgTextureUsage &usage = {});

  void set_semaphore(RgSemaphoreId id, Handle<Semaphore> semaphore);

  void execute(CommandAllocator &cmd_alloc);

private:
  void rotate_resources();

  void place_barriers();

private:
  friend RgBuilder;
  friend RgRuntime;

  Vector<RgPassRuntimeInfo> m_passes;
#if REN_RG_DEBUG
  Vector<String> m_pass_names;
#endif

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  Vector<VkMemoryBarrier2> m_memory_barriers;
  Vector<VkImageMemoryBarrier2> m_texture_barriers;

  Vector<RgBufferUse> m_buffer_uses;
  Vector<RgTextureUse> m_texture_uses;
  Vector<RgSemaphoreSignal> m_semaphore_signals;

  Vector<RgPhysicalVariableId> m_physical_variables;
  Vector<Any> m_variables;

  Vector<RgPhysicalBufferId> m_physical_buffers;
  Vector<BufferView> m_buffers;
  std::array<Handle<Buffer>, NUM_BUFFER_HEAPS> m_heap_buffers;

  Vector<RgPhysicalTextureId> m_physical_textures;
  Vector<Handle<Texture>> m_textures;
  DynamicBitset m_external_textures;
  Vector<u32> m_texture_temporal_layer_count;
  Vector<RgTextureUsage> m_texture_usages;
  TextureIdAllocatorScope m_tex_alloc;
  Vector<StorageTextureId> m_storage_texture_descriptors;

  Vector<Handle<Semaphore>> m_semaphores;

  using Arena = detail::ResourceArenaImpl<Buffer, Texture, Semaphore>;

  Arena m_arena;
  Renderer *m_renderer = nullptr;
};

class RgRuntime {
public:
  template <typename T>
  auto get_variable(RgVariableToken<T> variable) const -> const T & {
    RgPhysicalVariableId physical_variable =
        m_rg->m_physical_variables[variable];
    return *m_rg->m_variables[physical_variable].get<T>();
  }

  template <typename T>
  auto get_variable(RgRWVariableToken<T> variable) const -> T & {
    RgPhysicalVariableId physical_variable =
        m_rg->m_physical_variables[variable];
    return *m_rg->m_variables[physical_variable].get<T>();
  }

  auto get_buffer(RgBufferToken buffer) const -> const BufferView &;

  template <typename T>
  auto get_buffer_device_ptr(RgBufferToken buffer, usize offset = 0) const
      -> DevicePtr<T> {
    return m_rg->m_renderer->get_buffer_device_ptr<T>(get_buffer(buffer),
                                                      offset);
  }

  template <typename T>
  auto map_buffer(RgBufferToken buffer, usize offset = 0) const -> T * {
    return m_rg->m_renderer->map_buffer<T>(get_buffer(buffer), offset);
  }

  auto get_texture(RgTextureToken texture) const -> Handle<Texture>;

  auto get_storage_texture_descriptor(RgTextureToken texture) const
      -> StorageTextureId;

  auto get_texture_set() const -> VkDescriptorSet;

private:
  friend RenderGraph;
  RenderGraph *m_rg = nullptr;
};

class RgBuilder {
public:
  RgBuilder(RenderGraph &rg);

  [[nodiscard]] auto create_pass(RgPassCreateInfo &&create_info)
      -> RgPassBuilder;

  template <typename T>
  [[nodiscard]] auto create_variable() -> RgVariableId<T> {
    RgVariableId<T> variable(
        create_virtual_variable(RgPassId(), {}, RgGenericVariableId()));
    m_rg->m_variables[m_rg->m_physical_variables[variable]]
        .template emplace<T>();
    return variable;
  }

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info)
      -> RgBufferId;

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info)
      -> RgTextureId;

  [[nodiscard]] auto
  create_external_texture(RgExternalTextureCreateInfo &&create_info)
      -> RgTextureId;

  [[nodiscard]] auto
  create_external_semaphore(RgSemaphoreCreateInfo &&create_info)
      -> RgSemaphoreId;

  void build(CommandAllocator &cmd_alloc);

private:
  friend RgPassBuilder;

  [[nodiscard]] auto get_variable_def(RgGenericVariableId variable) const
      -> RgPassId;

  [[nodiscard]] auto get_variable_kill(RgGenericVariableId variable) const
      -> RgPassId;

  [[nodiscard]] auto create_virtual_variable(RgPassId pass, RgDebugName name,
                                             RgGenericVariableId parent)
      -> RgGenericVariableId;

  template <typename T>
  [[nodiscard]] auto read_variable(RgPassId pass, RgVariableId<T> variable)
      -> RgVariableToken<T> {
    return RgVariableToken<T>(
        read_variable(pass, RgGenericVariableId(variable)));
  }

  [[nodiscard]] auto read_variable(RgPassId pass, RgGenericVariableId variable)
      -> RgGenericVariableToken;

  template <typename T>
  [[nodiscard]] auto write_variable(RgPassId pass, RgDebugName name,
                                    RgVariableId<T> variable)
      -> std::tuple<RgVariableId<T>, RgRWVariableToken<T>> {
    auto [new_variable, token] =
        write_variable(pass, std::move(name), RgGenericVariableId(variable));
    return {RgVariableId<T>(new_variable), RgRWVariableToken<T>(token)};
  }

  [[nodiscard]] auto write_variable(RgPassId pass, RgDebugName name,
                                    RgGenericVariableId variable)
      -> std::tuple<RgGenericVariableId, RgGenericVariableToken>;

  [[nodiscard]] auto get_buffer_def(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto get_buffer_kill(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto add_buffer_use(RgBufferId buffer,
                                    const RgBufferUsage &usage)
      -> RgBufferUseId;

  [[nodiscard]] auto create_virtual_buffer(RgPassId pass, RgDebugName name,
                                           RgBufferId parent) -> RgBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, RgBufferId buffer,
                                 const RgBufferUsage &usage) -> RgBufferToken;

  [[nodiscard]] auto write_buffer(RgPassId pass, RgDebugName name,
                                  RgBufferId buffer, const RgBufferUsage &usage)
      -> std::tuple<RgBufferId, RgBufferToken>;

  [[nodiscard]] auto get_texture_def(RgTextureId texture) const -> RgPassId;

  [[nodiscard]] auto get_texture_kill(RgTextureId texture) const -> RgPassId;

  [[nodiscard]] auto add_texture_use(RgTextureId texture,
                                     const RgTextureUsage &usage)
      -> RgTextureUseId;

  [[nodiscard]] auto create_virtual_texture(RgPassId pass, RgDebugName name,
                                            RgTextureId parent,
                                            u32 num_temporal_layers = 1)
      -> RgTextureId;

  [[nodiscard]] auto read_texture(RgPassId pass, RgTextureId texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgPassId pass, RgDebugName name,
                                   RgTextureId texture,
                                   const RgTextureUsage &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto add_semaphore_signal(RgSemaphoreId semaphore,
                                          VkPipelineStageFlags2 stage_mask,
                                          u64 value) -> RgSemaphoreSignalId;

  void wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                      VkPipelineStageFlags2 stage_mask, u64 value);

  void signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                        VkPipelineStageFlags2 stage_mask, u64 value);

  void set_host_callback(RgPassId pass, CRgHostCallback auto cb) {
    ren_assert(!m_passes[pass].data);
    m_passes[pass].data = RgHostPassInfo{.cb = std::move(cb)};
  }

  void set_graphics_callback(RgPassId pass, CRgGraphicsCallback auto cb) {
    ren_assert(!m_passes[pass].data or
               m_passes[pass].data.get<RgGraphicsPassInfo>());
    m_passes[pass].data.get_or_emplace<RgGraphicsPassInfo>().cb = std::move(cb);
  }

  void set_compute_callback(RgPassId pass, CRgComputeCallback auto cb) {
    ren_assert(!m_passes[pass].data);
    m_passes[pass].data = RgComputePassInfo{.cb = std::move(cb)};
  }

  void set_callback(RgPassId pass, CRgCallback auto cb) {
    ren_assert(!m_passes[pass].data);
    m_passes[pass].data = RgGenericPassInfo{.cb = std::move(cb)};
  }

  auto build_pass_schedule() -> Vector<RgPassId>;

  void dump_pass_schedule(Span<const RgPassId> schedule) const;

  void create_resources(Span<const RgPassId> schedule);

  void fill_pass_runtime_info(Span<const RgPassId> schedule);

  void init_temporal_textures(CommandAllocator &cmd_alloc) const;

private:
  RenderGraph *m_rg = nullptr;

  struct RgPassInfo {
    SmallVector<RgGenericVariableId> read_variables;
    SmallVector<RgGenericVariableId> write_variables;
    SmallVector<RgBufferUseId> read_buffers;
    SmallVector<RgBufferUseId> write_buffers;
    SmallVector<RgTextureUseId> read_textures;
    SmallVector<RgTextureUseId> write_textures;
    SmallVector<RgSemaphoreSignalId> wait_semaphores;
    SmallVector<RgSemaphoreSignalId> signal_semaphores;
    Variant<Monostate, RgHostPassInfo, RgGraphicsPassInfo, RgComputePassInfo,
            RgGenericPassInfo>
        data;
  };

  Vector<RgPassInfo> m_passes = {{}};

  struct RgBufferDesc {
    BufferHeap heap;
    VkBufferUsageFlags usage = 0;
    usize size = 0;
  };

#if REN_RG_DEBUG
  HashMap<RgGenericVariableId, String> m_variable_names;
  Vector<RgGenericVariableId> m_variable_children = {{}};
#endif
  Vector<RgPassId> m_variable_defs = {{}};
  Vector<RgPassId> m_variable_kills = {{}};

  HashMap<RgPhysicalBufferId, RgBufferDesc> m_buffer_descs;
#if REN_RG_DEBUG
  HashMap<RgBufferId, String> m_buffer_names;
  Vector<RgBufferId> m_buffer_children = {{}};
#endif
  Vector<RgPassId> m_buffer_defs = {{}};
  Vector<RgPassId> m_buffer_kills = {{}};

  struct RgTextureDesc {
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
    u32 num_mip_levels = 1;
    u32 num_array_layers = 1;
  };

  HashMap<RgPhysicalTextureId, RgTextureDesc> m_texture_descs;
  HashMap<RgPhysicalTextureId, RgTextureInitCallback> m_texture_init_callbacks;
#if REN_RG_DEBUG
  HashMap<RgTextureId, String> m_texture_names;
  Vector<RgTextureId> m_texture_children = {{}};
  Vector<RgTextureId> m_texture_parents = {{}};
#endif
  Vector<RgPassId> m_texture_defs = {{}};
  Vector<RgPassId> m_texture_kills = {{}};

#if REN_RG_DEBUG
  Vector<String> m_semaphore_names = {{}};
#endif
};

class RgPassBuilder {
public:
  template <typename T>
  [[nodiscard]] auto create_variable(RgDebugName name)
      -> std::tuple<RgVariableId<T>, RgRWVariableToken<T>> {
    return write_variable(std::move(name), m_builder->create_variable<T>());
  }

  template <typename T>
  [[nodiscard]] auto read_variable(RgVariableId<T> variable)
      -> RgVariableToken<T> {
    return m_builder->read_variable(m_pass, variable);
  }

  template <typename T>
  [[nodiscard]] auto write_variable(RgDebugName name, RgVariableId<T> variable)
      -> std::tuple<RgVariableId<T>, RgRWVariableToken<T>> {
    return m_builder->write_variable(m_pass, std::move(name), variable);
  }

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info,
                                   const RgBufferUsage &usage)
      -> std::tuple<RgBufferId, RgBufferToken>;

  [[nodiscard]] auto read_buffer(RgBufferId buffer, const RgBufferUsage &usage)
      -> RgBufferToken;

  [[nodiscard]] auto write_buffer(RgDebugName name, RgBufferId buffer,
                                  const RgBufferUsage &usage)
      -> std::tuple<RgBufferId, RgBufferToken>;

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info,
                                    const RgTextureUsage &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto read_texture(RgTextureId texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgTextureToken;

  [[nodiscard]] auto write_texture(RgDebugName name, RgTextureId texture,
                                   const RgTextureUsage &usage)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto
  create_color_attachment(RgTextureCreateInfo &&create_info,
                          const ColorAttachmentOperations &ops, u32 index = 0)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto
  write_color_attachment(RgDebugName name, RgTextureId texture,
                         const ColorAttachmentOperations &ops, u32 index = 0)
      -> std::tuple<RgTextureId, RgTextureToken>;

  [[nodiscard]] auto
  create_depth_attachment(RgTextureCreateInfo &&create_info,
                          const DepthAttachmentOperations &ops)
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
  RgPassBuilder(RgPassId pass, RgBuilder &builder);

  void add_color_attachment(u32 index, RgTextureToken texture,
                            const ColorAttachmentOperations &ops);

  void add_depth_attachment(RgTextureToken texture,
                            const DepthAttachmentOperations &ops);

private:
  friend class RgBuilder;
  RgPassId m_pass;
  RgBuilder *m_builder;
};

} // namespace ren
