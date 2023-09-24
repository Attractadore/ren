#pragma once
#include "Attachments.hpp"
#include "Config.hpp"
#include "ResourceArena.hpp"
#include "Support/Any.hpp"
#include "Support/NewType.hpp"
#include "Support/Variant.hpp"
#include "TextureIdAllocator.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#ifndef REN_RG_DEBUG
#define REN_RG_DEBUG 0
#endif

namespace ren {

namespace detail {

template <typename T> struct CallbackTag {};

} // namespace detail

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
class RgUpdate;

template <typename F, typename T>
concept CRgUpdateCallback =
    std::invocable<F, RgUpdate &, const T &> and
    std::same_as<std::invoke_result_t<F, RgUpdate &, const T &>, bool>;

using RgUpdateCallback = std::function<bool(RgUpdate &, const Any &)>;
static_assert(CRgUpdateCallback<RgUpdateCallback, Any>);

template <typename F, typename T>
concept CRgHostCallback = std::invocable<F, const RgRuntime &, const T &>;

using RgHostCallback = std::function<void(const RgRuntime &, const Any &)>;
static_assert(CRgHostCallback<RgHostCallback, Any>);

template <typename F, typename T>
concept CRgGraphicsCallback =
    std::invocable<F, const RgRuntime &, RenderPass &, const T &>;

using RgGraphicsCallback =
    std::function<void(const RgRuntime &, RenderPass &, const Any &)>;
static_assert(CRgGraphicsCallback<RgGraphicsCallback, Any>);

template <typename F, typename T>
concept CRgComputeCallback =
    std::invocable<F, const RgRuntime &, ComputePass &, const T &>;

using RgComputeCallback =
    std::function<void(const RgRuntime &, ComputePass &, const Any &)>;
static_assert(CRgComputeCallback<RgComputeCallback, Any>);

template <typename F, typename T>
concept CRgTransferCallback =
    std::invocable<F, const RgRuntime &, TransferPass &, const T &>;

using RgTransferCallback =
    std::function<void(const RgRuntime &, TransferPass &, const Any &)>;
static_assert(CRgTransferCallback<RgTransferCallback, Any>);

constexpr u32 RG_MAX_TEMPORAL_LAYERS = 4;

REN_NEW_TYPE(RgPassId, u32);

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

class RgRuntime {
public:
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

struct RgPublicTextureDesc {
  VkImageType type;
  VkFormat format;
  union {
    struct {
      u32 width;
      u32 height;
      u32 depth;
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels;
  u32 num_array_layers;
};

class RgUpdate {
public:
  void resize_buffer(RgBufferId buffer, usize size);

  auto get_texture_desc(RgTextureId texture) const -> RgPublicTextureDesc;

private:
  RgUpdate(RenderGraph &rg);

private:
  friend RenderGraph;
  RenderGraph *m_rg = nullptr;
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
  RenderGraph(Swapchain &swapchain, TextureIdAllocator &tex_alloc);

  template <typename T> auto set_pass_data(StringView pass, T data) -> bool {
    auto it = m_pass_ids.find(pass);
    if (it == m_pass_ids.end()) {
      return false;
    }
    RgPassId pass_id = it->second;
    const Any &pass_data = m_pass_datas[pass_id] = std::move(data);
    const RgUpdateCallback &update_callback = m_pass_update_callbacks[pass_id];
    if (update_callback) {
      RgUpdate upd(*this);
      return update_callback(upd, pass_data);
    }
    return true;
  }

  auto is_pass_valid(StringView pass) const -> bool;

  auto is_buffer_valid(StringView buffer) const -> bool;

  auto is_texture_valid(StringView texture) const -> bool;

  void execute(CommandAllocator &cmd_alloc);

private:
  auto physical_buffers() const;

  auto get_physical_buffer(RgBufferId buffer) const -> RgPhysicalBufferId;

  auto get_physical_texture(RgTextureId texture) const -> RgPhysicalTextureId;

  void allocate_buffers();

  void rotate_resources();

private:
  friend RgBuilder;
  friend RgRuntime;
  friend RgUpdate;

  ResourceArena m_arena;

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
  Vector<Any> m_pass_datas;

#if REN_RG_DEBUG
  Vector<String> m_pass_names;
#endif

  Vector<RgUpdateCallback> m_pass_update_callbacks;

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  Vector<RgMemoryBarrier> m_memory_barriers;
  Vector<RgTextureBarrier> m_texture_barriers;
  Vector<RgSemaphoreSignal> m_wait_semaphores;
  Vector<RgSemaphoreSignal> m_signal_semaphores;

  struct RgBufferDesc {
    BufferHeap heap;
    usize size = 0;
  };

  HashMap<String, RgBufferId> m_buffer_ids;
  Vector<RgBufferId> m_buffer_parents;
  Vector<BufferView> m_buffers;
  HashMap<RgPhysicalBufferId, RgBufferDesc> m_buffer_descs;
  std::array<VkBufferUsageFlags, NUM_BUFFER_HEAPS> m_heap_buffer_usage_flags =
      {};
  std::array<std::array<BufferView, NUM_BUFFER_HEAPS>, PIPELINE_DEPTH>
      m_heap_buffers;

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

  void create_buffer(RgBufferCreateInfo &&create_info);

  auto is_buffer_valid(StringView buffer) const -> bool;

  void create_texture(RgTextureCreateInfo &&create_info);

  auto is_texture_valid(StringView texture) const -> bool;

  void present(StringView texture);

  void build(CommandAllocator &cmd_alloc);

private:
  friend RgPassBuilder;

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

  template <typename T>
  void set_update_callback(RgPassId pass, CRgUpdateCallback<T> auto cb) {
    m_rg->m_pass_update_callbacks[pass] =
        [cb = std::move(cb)](RgUpdate &rg, const Any &data) {
          return cb(rg, *data.get<T>());
        };
  }

  template <typename T>
  void set_host_callback(RgPassId pass, CRgHostCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgHostPassInfo{
        .cb = [cb = std::move(cb)](const RgRuntime &rg,
                                   const Any &data) { cb(rg, *data.get<T>()); },
    };
  }

  template <typename T>
  void set_graphics_callback(RgPassId pass, CRgGraphicsCallback<T> auto cb) {
    assert(!m_passes[pass].type or
           m_passes[pass].type.get<RgGraphicsPassInfo>());
    m_passes[pass].type.get_or_emplace<RgGraphicsPassInfo>().cb =
        [cb = std::move(cb)](const RgRuntime &rg, RenderPass &render_pass,
                             const Any &data) {
          cb(rg, render_pass, *data.get<T>());
        };
  }

  template <typename T>
  void set_compute_callback(RgPassId pass, CRgComputeCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgComputePassInfo{
        .cb = [cb = std::move(cb)](
                  const RgRuntime &rg, ComputePass &pass,
                  const Any &data) { cb(rg, pass, *data.get<T>()); },
    };
  }

  template <typename T>
  void set_transfer_callback(RgPassId pass, CRgTransferCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgTransferPassInfo{
        .cb = [cb = std::move(cb)](
                  const RgRuntime &rg, TransferPass &pass,
                  const Any &data) { cb(rg, pass, *data.get<T>()); },
    };
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
    SmallVector<RgBufferUseId> read_buffers;
    SmallVector<RgBufferUseId> write_buffers;
    SmallVector<RgTextureUseId> read_textures;
    SmallVector<RgTextureUseId> write_textures;
    SmallVector<RgSemaphoreSignalId> wait_semaphores;
    SmallVector<RgSemaphoreSignalId> signal_semaphores;
    Variant<Monostate, RgHostPassInfo, RgGraphicsPassInfo, RgComputePassInfo,
            RgTransferPassInfo>
        type;
    RgUpdateCallback update_cb;
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

  [[nodiscard]] auto
  create_color_attachment(RgTextureCreateInfo &&create_info,
                          const ColorAttachmentOperations &ops, u32 index = 0)
      -> RgTextureId;

  [[nodiscard]] auto
  create_depth_attachment(RgTextureCreateInfo &&create_info,
                          const DepthAttachmentOperations &ops) -> RgTextureId;

  [[nodiscard]] auto
  write_color_attachment(StringView dst_texture, StringView src_texture,
                         const ColorAttachmentOperations &ops, u32 index = 0)
      -> RgTextureId;

  [[nodiscard]] auto
  write_depth_attachment(StringView dst_texture, StringView src_texture,
                         const DepthAttachmentOperations &ops) -> RgTextureId;

  template <typename T> void set_update_callback(CRgUpdateCallback<T> auto cb) {
    m_builder->set_update_callback<T>(m_pass, std::move(cb));
  }

  template <typename T>
  void set_update_callback(detail::CallbackTag<T>,
                           CRgUpdateCallback<T> auto cb) {
    set_update_callback<T>(std::move(cb));
  }

#define ren_rg_update_callback(T)                                              \
  detail::CallbackTag<T>(), [=](RgUpdate & rg, const T &data)

  template <typename T> void set_host_callback(CRgHostCallback<T> auto cb) {
    m_builder->set_host_callback<T>(m_pass, std::move(cb));
  }

  template <typename T>
  void set_host_callback(detail::CallbackTag<T>, CRgHostCallback<T> auto cb) {
    set_host_callback<T>(std::move(cb));
  }

#define ren_rg_host_callback(T)                                                \
  detail::CallbackTag<T>(), [=](const RgRuntime &rg, const T &data)

  template <typename T>
  void set_graphics_callback(CRgGraphicsCallback<T> auto cb) {
    m_builder->set_graphics_callback<T>(m_pass, std::move(cb));
  }

  template <typename T>
  void set_graphics_callback(detail::CallbackTag<T>,
                             CRgGraphicsCallback<T> auto cb) {
    set_graphics_callback<T>(std::move(cb));
  }

#define ren_rg_graphics_callback(T)                                            \
  detail::CallbackTag<T>(), [=](const RgRuntime &rg, RenderPass &render_pass,  \
                                const T &data)

  template <typename T>
  void set_compute_callback(CRgComputeCallback<T> auto cb) {
    m_builder->set_compute_callback<T>(m_pass, std::move(cb));
  }

  template <typename T>
  void set_compute_callback(detail::CallbackTag<T>,
                            CRgComputeCallback<T> auto cb) {
    set_compute_callback<T>(std::move(cb));
  }

#define ren_rg_compute_callback(T)                                             \
  detail::CallbackTag<T>(), [=](const RgRuntime &rg, ComputePass &pass,        \
                                const T &data)

  template <typename T>
  void set_transfer_callback(CRgTransferCallback<T> auto cb) {
    m_builder->set_transfer_callback<T>(m_pass, std::move(cb));
  }

  template <typename T>
  void set_transfer_callback(detail::CallbackTag<T>,
                             CRgTransferCallback<T> auto cb) {
    set_transfer_callback<T>(std::move(cb));
  }

#define ren_rg_transfer_callback(T)                                            \
  detail::CallbackTag<T>(), [=](const RgRuntime &rg, TransferPass &cmd,        \
                                const T &data)
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
