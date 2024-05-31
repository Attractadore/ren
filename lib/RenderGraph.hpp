#pragma once
#include "Attachments.hpp"
#include "Config.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"
#include "Support/Any.hpp"
#include "Support/NewType.hpp"
#include "Support/Variant.hpp"
#include "TextureIdAllocator.hpp"

#include <functional>
#include <vulkan/vulkan.h>

#ifndef REN_RG_DEBUG
#define REN_RG_DEBUG 0
#endif

namespace ren {

class CommandAllocator;
class CommandRecorder;
class Swapchain;
class RenderPass;
class ComputePass;
using TransferPass = CommandRecorder;

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
concept CRgTransferCallback =
    std::invocable<F, Renderer &, const RgRuntime &, TransferPass &>;

using RgTransferCallback =
    std::function<void(Renderer &, const RgRuntime &, TransferPass &)>;
static_assert(CRgTransferCallback<RgTransferCallback>);

constexpr u32 RG_MAX_TEMPORAL_LAYERS = 4;

REN_NEW_TYPE(RgPassId, u32);

REN_NEW_TYPE(RgPhysicalVariableId, u32);
REN_NEW_TYPE(RgVariableId, u32);
REN_NEW_TYPE(RgRWVariableId, u32);
REN_NEW_TYPE(RgParameterId, u32);

REN_NEW_TYPE(RgPhysicalBufferId, u32);
REN_NEW_TYPE(RgBufferId, u32);

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
  String name;
  /// Memory heap from which to allocate buffer
  BufferHeap heap = BufferHeap::Dynamic;
  /// Initial buffer size
  usize size = 0;
};

REN_NEW_TYPE(RgPhysicalTextureId, u32);
REN_NEW_TYPE(RgTextureId, u32);

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
  String name;
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
  /// Initial clear color or depth-stencil for temporal layers
  Variant<Monostate, glm::vec4, VkClearDepthStencilValue> clear;
};

REN_NEW_TYPE(RgSemaphoreId, u32);

struct RgBufferUse {
  RgBufferId buffer;
  RgBufferUsage usage;
};

struct RgTextureUse {
  RgTextureId texture;
  RgTextureUsage usage;
};

struct RgSemaphoreSignal {
  RgSemaphoreId semaphore;
  VkPipelineStageFlagBits2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  u64 value = 0;
};

struct RgHostPassInfo {
  RgHostCallback cb;
};

struct RgColorAttachment {
  RgTextureId texture;
  ColorAttachmentOperations ops;
};

struct RgDepthStencilAttachment {
  RgTextureId texture;
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

struct RgTransferPassInfo {
  RgTransferCallback cb;
};

struct RgMemoryBarrier {
  VkPipelineStageFlagBits2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
  VkPipelineStageFlagBits2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 dst_access_mask = VK_ACCESS_2_NONE;

public:
  operator VkMemoryBarrier2() const {
    return {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
    };
  }
};

struct RgTextureBarrier {
  RgPhysicalTextureId texture;
  VkPipelineStageFlagBits2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
  VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkPipelineStageFlagBits2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 dst_access_mask = VK_ACCESS_2_NONE;
  VkImageLayout dst_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RgHostPass {
  RgHostCallback cb;
};

struct RgGraphicsPass {
  u32 num_color_attachments = 0;
  bool has_depth_attachment = false;
  RgGraphicsCallback cb;
};

struct RgComputePass {
  RgComputeCallback cb;
};

struct RgTransferPass {
  RgTransferCallback cb;
};

class RenderGraph {
public:
  RenderGraph(Renderer &renderer, Swapchain &swapchain,
              TextureIdAllocator &tex_alloc);

  auto is_pass_valid(StringView pass) const -> bool;

  template <typename T>
  auto get_parameter(StringView parameter) -> Optional<T &> {
    auto it = m_parameter_ids.find(parameter);
    if (it == m_parameter_ids.end()) {
      return None;
    }
    RgParameterId id = it->second;
    return m_parameters[id].get<T>();
  }

  auto is_variable_valid(StringView variable) const -> bool;

  auto is_buffer_valid(StringView buffer) const -> bool;

  auto is_texture_valid(StringView texture) const -> bool;

  void execute(CommandAllocator &cmd_alloc);

private:
  auto get_physical_variable(RgVariableId variable) const
      -> RgPhysicalVariableId;

  auto get_physical_variable(RgRWVariableId variable) const
      -> RgPhysicalVariableId;

  auto get_physical_buffer(RgBufferId buffer) const -> RgPhysicalBufferId;

  auto get_physical_texture(RgTextureId texture) const -> RgPhysicalTextureId;

  void rotate_resources();

private:
  friend RgBuilder;
  friend RgRuntime;

  struct RgPassRuntimeInfo {
    RgPassId pass;
    u32 num_memory_barriers;
    u32 num_texture_barriers;
    u32 num_wait_semaphores;
    u32 num_signal_semaphores;
    Variant<RgHostPass, RgGraphicsPass, RgComputePass, RgTransferPass> type;
  };

  Vector<RgPassRuntimeInfo> m_passes;

  HashMap<String, RgPassId> m_pass_ids;

#if REN_RG_DEBUG
  Vector<String> m_pass_names;
#endif

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  Vector<RgMemoryBarrier> m_memory_barriers;
  Vector<RgTextureBarrier> m_texture_barriers;
  Vector<RgSemaphoreSignal> m_wait_semaphores;
  Vector<RgSemaphoreSignal> m_signal_semaphores;

  HashMap<String, RgParameterId> m_parameter_ids;
  Vector<Any> m_parameters;

  HashMap<String, RgVariableId> m_variable_ids;
  Vector<RgVariableId> m_variable_parents;
  Vector<Any> m_variables;

  HashMap<String, RgBufferId> m_buffer_ids;
  Vector<RgBufferId> m_buffer_parents;
  Vector<BufferView> m_buffers;
  std::array<Handle<Buffer>, NUM_BUFFER_HEAPS> m_heap_buffers;

  HashMap<String, RgTextureId> m_texture_ids;
  Vector<RgTextureId> m_texture_parents;
  Vector<Handle<Texture>> m_textures;
  HashMap<RgPhysicalTextureId, u32> m_texture_instance_counts;
  TextureIdAllocatorScope m_tex_alloc;
  Vector<StorageTextureId> m_storage_texture_descriptors;

  Vector<Handle<Semaphore>> m_semaphores;

  Swapchain *m_swapchain = nullptr;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_acquire_semaphores;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_present_semaphores;
  RgSemaphoreId m_acquire_semaphore;
  RgSemaphoreId m_present_semaphore;
  RgPhysicalTextureId m_backbuffer;

  using Arena = detail::ResourceArenaImpl<Buffer, Texture, Semaphore>;

  Arena m_arena;
  Renderer *m_renderer = nullptr;
};

class RgRuntime {
public:
  template <typename T>
  auto get_parameter(RgParameterId parameter) const -> const T & {
    ren_assert(parameter);
    auto opt = m_rg->m_parameters[parameter].get<T>();
    ren_assert(opt);
    return *opt;
  }

  template <typename T>
  auto get_variable(RgVariableId variable) const -> const T & {
    ren_assert(variable);
    return *m_rg->m_variables[m_rg->get_physical_variable(variable)].get<T>();
  }

  template <typename T>
  auto get_variable(RgRWVariableId variable) const -> T & {
    ren_assert(variable);
    return *m_rg->m_variables[m_rg->get_physical_variable(variable)].get<T>();
  }

  auto get_buffer(RgBufferId buffer) const -> const BufferView &;

  template <typename T>
  auto map_buffer(RgBufferId buffer, usize offset = 0) const -> T * {
    return (T *)map_buffer_impl(buffer, offset);
  }

  auto get_texture(RgTextureId texture) const -> Handle<Texture>;

  auto get_storage_texture_descriptor(RgTextureId texture) const
      -> StorageTextureId;

  auto get_texture_set() const -> VkDescriptorSet;

private:
  auto map_buffer_impl(RgBufferId buffer, usize offset) const -> std::byte *;

  friend RenderGraph;
  RenderGraph *m_rg = nullptr;
};

REN_NEW_TYPE(RgBufferUseId, u32);
REN_NEW_TYPE(RgTextureUseId, u32);
REN_NEW_TYPE(RgSemaphoreSignalId, u32);

struct RgClearTexture {
  Handle<Texture> texture;
  VkPipelineStageFlags2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
  VkImageLayout dst_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  union {
    glm::vec4 color;
    VkClearDepthStencilValue depth_stencil;
  } clear;
};

class RgBuilder {
public:
  RgBuilder(RenderGraph &rg);

  [[nodiscard]] auto create_pass(String name) -> RgPassBuilder;

  auto is_pass_valid(StringView pass) const -> bool;

  template <typename T> void create_parameter(String name) {
    RgParameterId id = get_or_alloc_parameter(name);
    m_rg->m_parameters[id].emplace<T>();
  }

  template <typename T> void create_variable(String name) {
    RgVariableId id = get_or_alloc_variable(name);
    m_rg->m_variable_parents[id] = id;
    m_rg->m_variables[id].emplace<T>();
  }

  auto is_variable_valid(StringView variable) const -> bool;

  void create_buffer(RgBufferCreateInfo &&create_info);

  auto is_buffer_valid(StringView buffer) const -> bool;

  void create_texture(RgTextureCreateInfo &&create_info);

  auto is_texture_valid(StringView texture) const -> bool;

  void present(StringView texture);

  void build(CommandAllocator &cmd_alloc);

private:
  friend RgPassBuilder;

  [[nodiscard]] auto get_variable_def(RgVariableId variable) const -> RgPassId;

  [[nodiscard]] auto get_variable_kill(RgVariableId variable) const -> RgPassId;

  [[nodiscard]] auto get_or_alloc_variable(StringView name) -> RgVariableId;

  [[nodiscard]] auto read_variable(RgPassId pass, StringView variable)
      -> RgVariableId;

  [[nodiscard]] auto write_variable(RgPassId pass, StringView dst_variable,
                                    StringView src_variable) -> RgRWVariableId;

  [[nodiscard]] auto get_or_alloc_parameter(StringView name) -> RgParameterId;

  [[nodiscard]] auto read_parameter(RgPassId pass, StringView name)
      -> RgParameterId;

  [[nodiscard]] auto get_buffer_def(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto get_buffer_kill(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto add_buffer_use(RgBufferId buffer,
                                    const RgBufferUsage &usage)
      -> RgBufferUseId;

  [[nodiscard]] auto get_or_alloc_buffer(StringView name) -> RgBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, StringView buffer,
                                 const RgBufferUsage &usage) -> RgBufferId;

  [[nodiscard]] auto write_buffer(RgPassId pass, StringView dst_buffer,
                                  StringView src_buffer,
                                  const RgBufferUsage &usage) -> RgBufferId;

  [[nodiscard]] auto get_texture_def(RgTextureId texture) const -> RgPassId;

  [[nodiscard]] auto get_texture_kill(RgTextureId texture) const -> RgPassId;

  [[nodiscard]] auto add_texture_use(RgTextureId texture,
                                     const RgTextureUsage &usage)
      -> RgTextureUseId;

  [[nodiscard]] auto get_or_alloc_texture(StringView name,
                                          u32 temporal_layer = 0)
      -> RgTextureId;

  [[nodiscard]] auto read_texture(RgPassId pass, StringView texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgTextureId;

  [[nodiscard]] auto write_texture(RgPassId pass, StringView dst_texture,
                                   StringView src_texture,
                                   const RgTextureUsage &usage) -> RgTextureId;

  [[nodiscard]] auto add_semaphore_signal(RgSemaphoreId semaphore,
                                          VkPipelineStageFlags2 stage_mask,
                                          u64 value) -> RgSemaphoreSignalId;

  [[nodiscard]] auto alloc_semaphore(StringView name) -> RgSemaphoreId;

  void wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                      VkPipelineStageFlags2 stage_mask, u64 value);

  void signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                        VkPipelineStageFlags2 stage_mask, u64 value);

  void set_host_callback(RgPassId pass, CRgHostCallback auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgHostPassInfo{.cb = std::move(cb)};
  }

  void set_graphics_callback(RgPassId pass, CRgGraphicsCallback auto cb) {
    assert(!m_passes[pass].type or
           m_passes[pass].type.get<RgGraphicsPassInfo>());
    m_passes[pass].type.get_or_emplace<RgGraphicsPassInfo>().cb = std::move(cb);
  }

  void set_compute_callback(RgPassId pass, CRgComputeCallback auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgComputePassInfo{.cb = std::move(cb)};
  }

  void set_transfer_callback(RgPassId pass, CRgTransferCallback auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgTransferPassInfo{.cb = std::move(cb)};
  }

  auto get_buffer_parent(RgBufferId buffer) -> RgBufferId;

  void build_buffer_disjoint_set();

  auto get_texture_parent(RgTextureId texture) -> RgTextureId;

  void build_texture_disjoint_set();

  auto passes() const;

  auto build_pass_schedule() -> Vector<RgPassId>;

  void dump_pass_schedule(Span<const RgPassId> schedule) const;

  void create_resources(Span<const RgPassId> schedule);

  void fill_pass_runtime_info(Span<const RgPassId> schedule);

  void place_barriers_and_semaphores(Span<const RgPassId> schedule,
                                     Vector<RgClearTexture> &clear_textures);

  void clear_temporal_textures(CommandAllocator &cmd_alloc,
                               Span<const RgClearTexture> clear_textures) const;

private:
  RenderGraph *m_rg = nullptr;

  struct RgPassInfo {
#if REN_RG_DEBUG
    SmallVector<RgParameterId> read_parameters;
#endif
    SmallVector<RgVariableId> read_variables;
    SmallVector<RgVariableId> write_variables;
    SmallVector<RgBufferUseId> read_buffers;
    SmallVector<RgBufferUseId> write_buffers;
    SmallVector<RgTextureUseId> read_textures;
    SmallVector<RgTextureUseId> write_textures;
    SmallVector<RgSemaphoreSignalId> wait_semaphores;
    SmallVector<RgSemaphoreSignalId> signal_semaphores;
    Variant<Monostate, RgHostPassInfo, RgGraphicsPassInfo, RgComputePassInfo,
            RgTransferPassInfo>
        type;
  };

  Vector<RgPassInfo> m_passes = {{}};

  Vector<RgBufferUse> m_buffer_uses;
  Vector<RgTextureUse> m_texture_uses;
  Vector<RgSemaphoreSignal> m_semaphore_signals;

  struct RgBufferDesc {
    BufferHeap heap;
    VkBufferUsageFlags usage = 0;
    usize size = 0;
  };

#if REN_RG_DEBUG
  HashMap<RgParameterId, String> m_parameter_names;
#endif

#if REN_RG_DEBUG
  HashMap<RgVariableId, String> m_variable_names;
  Vector<RgVariableId> m_variable_children = {{}};
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
    u32 num_temporal_layers = 1;
    Variant<Monostate, glm::vec4, VkClearDepthStencilValue> clear;
  };

  HashMap<RgPhysicalTextureId, RgTextureDesc> m_texture_descs;
#if REN_RG_DEBUG
  HashMap<RgTextureId, String> m_texture_names;
  Vector<RgTextureId> m_texture_children = {{}};
#endif
  Vector<RgPassId> m_texture_defs = {{}};
  Vector<RgPassId> m_texture_kills = {{}};

  HashMap<String, RgSemaphoreId> m_semaphore_ids;
#if REN_RG_DEBUG
  Vector<String> m_semaphore_names = {{}};
#endif
};

struct RgNoPassData {};

class RgPassBuilder {
public:
  [[nodiscard]] auto read_parameter(StringView parameter) -> RgParameterId;

  template <typename T>
  [[nodiscard]] auto create_variable(StringView variable) -> RgRWVariableId {
    String init_name = fmt::format("rg-init-{}", variable);
    m_builder->create_variable<T>(init_name);
    return write_variable(variable, init_name);
  }

  [[nodiscard]] auto read_variable(StringView variable) -> RgVariableId;

  [[nodiscard]] auto write_variable(StringView dst_variable,
                                    StringView src_variable) -> RgRWVariableId;

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info,
                                   const RgBufferUsage &usage) -> RgBufferId;

  [[nodiscard]] auto read_buffer(StringView buffer, const RgBufferUsage &usage)
      -> RgBufferId;

  [[nodiscard]] auto write_buffer(StringView dst_buffer, StringView src_buffer,
                                  const RgBufferUsage &usage) -> RgBufferId;

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info,
                                    const RgTextureUsage &usage) -> RgTextureId;

  [[nodiscard]] auto read_texture(StringView texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgTextureId;

  [[nodiscard]] auto write_texture(StringView dst_texture,
                                   StringView src_texture,
                                   const RgTextureUsage &usage) -> RgTextureId;

  auto create_color_attachment(RgTextureCreateInfo &&create_info,
                               const ColorAttachmentOperations &ops,
                               u32 index = 0) -> RgTextureId;

  auto write_color_attachment(StringView dst_texture, StringView src_texture,
                              const ColorAttachmentOperations &ops,
                              u32 index = 0) -> RgTextureId;

  auto create_depth_attachment(RgTextureCreateInfo &&create_info,
                               const DepthAttachmentOperations &ops)
      -> RgTextureId;

  auto read_depth_attachment(StringView texture) -> RgTextureId;

  auto write_depth_attachment(StringView dst_texture, StringView src_texture,
                              const DepthAttachmentOperations &ops)
      -> RgTextureId;

  void set_host_callback(CRgHostCallback auto cb) {
    m_builder->set_host_callback(m_pass, std::move(cb));
  }

  void set_graphics_callback(CRgGraphicsCallback auto cb) {
    m_builder->set_graphics_callback(m_pass, std::move(cb));
  }

  void set_compute_callback(CRgComputeCallback auto cb) {
    m_builder->set_compute_callback(m_pass, std::move(cb));
  }

  void set_transfer_callback(CRgTransferCallback auto cb) {
    m_builder->set_transfer_callback(m_pass, std::move(cb));
  }

private:
  RgPassBuilder(RgPassId pass, RgBuilder &builder);

  void add_color_attachment(u32 index, RgTextureId texture,
                            const ColorAttachmentOperations &ops);

  void add_depth_attachment(RgTextureId texture,
                            const DepthAttachmentOperations &ops);

  void
  wait_semaphore(RgSemaphoreId semaphore,
                 VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE,
                 u64 value = 0);

  void
  signal_semaphore(RgSemaphoreId semaphore,
                   VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE,
                   u64 value = 0);

private:
  friend class RgBuilder;
  RgPassId m_pass;
  RgBuilder *m_builder;
};

} // namespace ren
