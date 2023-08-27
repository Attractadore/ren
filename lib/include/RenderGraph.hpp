#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Config.hpp"
#include "ResourceArena.hpp"
#include "Semaphore.hpp"
#include "Support/Any.hpp"
#include "Support/Errors.hpp"
#include "Support/NewType.hpp"
#include "Support/String.hpp"
#include "Support/Variant.hpp"
#include "Texture.hpp"
#include "TextureIDAllocator.hpp"

#include <functional>

#define REN_RG_DEBUG 1

namespace ren {

namespace detail {

template <typename T> struct CallbackTag {};

} // namespace detail

class CommandAllocator;
class CommandRecorder;
class Device;
class Swapchain;
class RenderPass;
class ComputePass;
using TransferPass = CommandRecorder;

class RenderGraph;
class RgBuilder;
class RgPassBuilder;
class RgRuntime;
class RgUpdate;

template <typename F>
concept CRgBufferInitCallback =
    std::invocable<F, Device &, const BufferView &, TransferPass &>;

using RgBufferInitCallback =
    std::function<void(Device &, const BufferView &, TransferPass &)>;
static_assert(CRgBufferInitCallback<RgBufferInitCallback>);

template <typename F>
concept CRgTextureInitCallback =
    std::invocable<F, Device &, Handle<Texture>, TransferPass &>;

template <typename F, typename T>
concept CRgUpdateCallback = std::invocable<F, RgUpdate &, const T &>;

using RgUpdateCallback = std::function<void(RgUpdate &, const Any &)>;
static_assert(CRgUpdateCallback<RgUpdateCallback, Any>);

template <typename F, typename T>
concept CRgHostCallback =
    std::invocable<F, Device &, const RgRuntime &, const T &>;

using RgHostCallback =
    std::function<void(Device &, const RgRuntime &, const Any &)>;
static_assert(CRgHostCallback<RgHostCallback, Any>);

template <typename F, typename T>
concept CRgGraphicsCallback =
    std::invocable<F, Device &, const RgRuntime &, RenderPass &, const T &>;

using RgGraphicsCallback =
    std::function<void(Device &, const RgRuntime &, RenderPass &, const Any &)>;
static_assert(CRgGraphicsCallback<RgGraphicsCallback, Any>);

template <typename F, typename T>
concept CRgComputeCallback =
    std::invocable<F, Device &, const RgRuntime &, ComputePass &, const T &>;

using RgComputeCallback = std::function<void(Device &, const RgRuntime &,
                                             ComputePass &, const Any &)>;
static_assert(CRgComputeCallback<RgComputeCallback, Any>);

template <typename F, typename T>
concept CRgTransferCallback =
    std::invocable<F, Device &, const RgRuntime &, TransferPass &, const T &>;

using RgTransferCallback = std::function<void(Device &, const RgRuntime &,
                                              TransferPass &, const Any &)>;
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
  BufferHeap heap = BufferHeap::Upload;
  /// Initial buffer size
  usize size = 0;
  /// Buffer alignment
  usize alignment = DEFAULT_BUFFER_REFERENCE_ALIGNMENT;
  /// Number of temporal layers
  u32 num_temporal_layers = 1;
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
  /// Initial texture width
  u32 width = 0;
  /// Initial texture height
  u32 height = 1;
  // Initial texture depth
  u32 depth = 1;
  /// Initial number of texture mip levels
  u32 num_mip_levels = 1;
  // Initial number of texture array layers
  u32 num_array_layers = 1;
  /// Number of temporal layers
  u32 num_temporal_layers = 1;
};

REN_NEW_TYPE(RgSemaphoreId, u32);

struct RgTextureSizeInfo {
  /// New texture width
  u32 width = 0;
  /// New texture height
  u32 height = 1;
  // New texture depth
  u32 depth = 1;
  /// New number of texture mip levels
  u32 num_mip_levels = 1;
  // New number of texture array layers
  u32 num_array_layers = 1;
};

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

class RgUpdate {
public:
  auto resize_buffer(RgBufferId buffer, usize size) -> bool;

  auto resize_texture(RgTextureId texture, const RgTextureSizeInfo &size_info)
      -> bool;

private:
  friend RenderGraph;
  RgUpdate(RenderGraph &rg);

private:
  RenderGraph *m_rg = nullptr;
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
      -> StorageTextureID;

private:
  auto map_buffer_impl(RgBufferId buffer, usize offset) const -> std::byte *;

  friend RenderGraph;
  RenderGraph *m_rg = nullptr;
};

class RenderGraph {
public:
  RenderGraph(Device &device, Swapchain &swapchain,
              TextureIDAllocator &tex_alloc);

  template <typename T> auto set_pass_data(StringView pass, T data) -> bool {
    auto it = m_pass_ids.find(pass);
    if (it == m_pass_ids.end()) {
      return false;
    }
    m_pass_datas[it->second] = std::move(data);
    return true;
  }

  auto is_pass_valid(StringView pass) -> bool;

  void execute(CommandAllocator &cmd_alloc);

private:
  auto get_physical_buffer(RgBufferId buffer) const -> RgPhysicalBufferId;

  auto get_physical_texture(RgTextureId texture) const -> RgPhysicalTextureId;

  void update();

  void allocate_buffers();

  void rotate_resources();

  void init_dirty_buffers(CommandAllocator &cmd_alloc);

private:
  friend RgBuilder;
  friend RgRuntime;
  friend RgUpdate;

  Device *m_device = nullptr;

  ResourceArena m_arena;

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

  struct RgPassRuntimeInfo {
    RgPassId pass;
    u32 num_memory_barriers;
    u32 num_texture_barriers;
    u32 num_wait_semaphores;
    u32 num_signal_semaphores;
    Variant<RgHostPass, RgGraphicsPass, RgComputePass, RgTransferPass> type;
  };

  Vector<RgPassRuntimeInfo> m_passes;

#if REN_RG_DEBUG
  Vector<String> m_pass_names;
#endif

  HashMap<String, RgPassId> m_pass_ids;
  Vector<Any> m_pass_datas;
  Vector<RgUpdateCallback> m_pass_update_cbs;

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

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

  Vector<RgMemoryBarrier> m_memory_barriers;
  Vector<RgTextureBarrier> m_texture_barriers;
  Vector<RgSemaphoreSignal> m_wait_semaphores;
  Vector<RgSemaphoreSignal> m_signal_semaphores;

  struct RgBufferDesc {
    BufferHeap heap;
    usize size = 0;
    usize alignment = 0;
  };

  struct RgTemporalBufferDesc {
    u32 num_temporal_layers = 0;
  };

  Vector<RgBufferId> m_buffer_parents;
  Vector<BufferView> m_buffers;
  HashMap<RgPhysicalBufferId, RgBufferDesc> m_buffer_descs;
  std::array<std::array<Handle<Buffer>, NUM_BUFFER_HEAPS>, PIPELINE_DEPTH>
      m_heap_buffers;
  HashMap<RgPhysicalBufferId, RgTemporalBufferDesc> m_temporal_buffer_descs;
  HashSet<RgPhysicalBufferId> m_dirty_temporal_buffers;
  HashMap<RgPhysicalBufferId, RgBufferInitCallback> m_buffer_init_cbs;
  HashMap<RgPhysicalBufferId, RgMemoryBarrier> m_buffer_init_barriers;

  struct RgTextureDesc {
    u32 width = 0;
    u32 height = 0;
    u32 depth = 0;
    u32 num_mip_levels = 0;
    u32 num_array_layers = 0;
    u32 num_instances = 0;
  };

  Vector<RgTextureId> m_texture_parents;
  Vector<Handle<Texture>> m_textures;
  HashMap<RgPhysicalTextureId, RgTextureDesc> m_texture_descs;
  TextureIDAllocator *m_tex_alloc = nullptr;
  Handle<PipelineLayout> m_pipeline_layout;
  Vector<StorageTextureID> m_storage_texture_descriptors;

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

class RgBuilder {
public:
  RgBuilder(RenderGraph &rg);

  [[nodiscard]] auto create_pass(String name) -> RgPassBuilder;

  void create_buffer(RgBufferCreateInfo &&create_info);

  auto is_buffer_valid(StringView buffer) const -> bool;

  void set_buffer_init_callback(StringView buffer,
                                CRgBufferInitCallback auto cb) {
    m_buffer_init_cbs.insert(get_or_alloc_buffer(buffer), std::move(cb));
  }

#define ren_rg_buffer_init_callback                                            \
  [=](Device & device, const BufferView &buffer, TransferPass &cmd)

  void create_texture(RgTextureCreateInfo &&create_info);

  auto is_texture_valid(StringView texture) const -> bool;

  void set_texture_init_callback(StringView buffer,
                                 CRgTextureInitCallback auto cb) {
    todo();
  }

#define ren_rg_texture_init_callback                                           \
  [=](Device & device, Handle<Texture> texture, TransferPass & cmd)

  void present(StringView texture);

  void build();

private:
  friend RgPassBuilder;

  [[nodiscard]] auto get_buffer_def(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto get_buffer_kill(RgBufferId buffer) const -> RgPassId;

  [[nodiscard]] auto add_buffer_use(RgBufferId buffer,
                                    const RgBufferUsage &usage)
      -> RgBufferUseId;

  [[nodiscard]] auto get_or_alloc_buffer(StringView name,
                                         u32 temporal_layer = 0) -> RgBufferId;

  [[nodiscard]] auto read_buffer(RgPassId pass, StringView buffer,
                                 const RgBufferUsage &usage,
                                 u32 temporal_layer = 0) -> RgBufferId;

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
    m_rg->m_pass_update_cbs[pass] = [cb = std::move(cb)](RgUpdate &rg,
                                                         const Any &data) {
      cb(rg, *data.get<T>());
    };
  }

  template <typename T>
  void set_host_callback(RgPassId pass, CRgHostCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgHostPassInfo{
        .cb = [cb = std::move(cb)](
                  Device &device, const RgRuntime &rg,
                  const Any &data) { cb(device, rg, *data.get<T>()); },
    };
  }

  template <typename T>
  void set_graphics_callback(RgPassId pass, CRgGraphicsCallback<T> auto cb) {
    assert(!m_passes[pass].type or
           m_passes[pass].type.get<RgGraphicsPassInfo>());
    m_passes[pass].type.get_or_emplace<RgGraphicsPassInfo>().cb =
        [cb = std::move(cb)](Device &device, const RgRuntime &rg,
                             RenderPass &render_pass, const Any &data) {
          cb(device, rg, render_pass, *data.get<T>());
        };
  }

  template <typename T>
  void set_compute_callback(RgPassId pass, CRgComputeCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgComputePassInfo{
        .cb = [cb = std::move(cb)](
                  Device &device, const RgRuntime &rg, ComputePass &pass,
                  const Any &data) { cb(device, rg, pass, *data.get<T>()); },
    };
  }

  template <typename T>
  void set_transfer_callback(RgPassId pass, CRgTransferCallback<T> auto cb) {
    assert(!m_passes[pass].type);
    m_passes[pass].type = RgTransferPassInfo{
        .cb = [cb = std::move(cb)](
                  Device &device, const RgRuntime &rg, TransferPass &pass,
                  const Any &data) { cb(device, rg, pass, *data.get<T>()); },
    };
  }

  auto get_buffer_parent(RgBufferId buffer) -> RgBufferId;

  void build_buffer_disjoint_set();

  auto get_texture_parent(RgTextureId texture) -> RgTextureId;

  void build_texture_disjoint_set();

  auto build_pass_schedule() -> Vector<RgPassId>;

  void dump_schedule(Span<const RgPassId> schedule) const;

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
  };

  Vector<RgPassInfo> m_passes = {{}};

  Vector<RgBufferUse> m_buffer_uses;
  Vector<RgTextureUse> m_texture_uses;
  Vector<RgSemaphoreSignal> m_semaphore_signals;

  struct RgBufferDesc {
    BufferHeap heap;
    VkBufferUsageFlags usage = 0;
    usize size = 0;
    usize alignment = 0;
    u32 num_temporal_layers = 0;
  };

  HashMap<String, RgBufferId> m_buffer_ids;
  HashMap<RgPhysicalBufferId, RgBufferDesc> m_buffer_descs;
#if REN_RG_DEBUG
  HashMap<RgBufferId, String> m_buffer_names;
  Vector<RgBufferId> m_buffer_children = {{}};
#endif
  HashMap<RgBufferId, RgBufferInitCallback> m_buffer_init_cbs;
  Vector<RgPassId> m_buffer_defs = {{}};
  Vector<RgPassId> m_buffer_kills = {{}};

  struct RgTextureDesc {
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
    u32 num_mip_levels = 1;
    u32 num_array_layers = 1;
    u32 num_temporal_layers = 1;
  };

  HashMap<String, RgTextureId> m_texture_ids;
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

  [[nodiscard]] auto read_buffer(StringView buffer, const RgBufferUsage &usage,
                                 u32 temporal_layer = 0) -> RgBufferId;

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
  detail::CallbackTag<T>(), [=](Device & device, const RgRuntime &rg,          \
                                const T &data)

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
  detail::CallbackTag<T>(), [=](Device & device, const RgRuntime &rg,          \
                                RenderPass &render_pass, const T &data)

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
  detail::CallbackTag<T>(), [=](Device & device, const RgRuntime &rg,          \
                                ComputePass &pass, const T &data)

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
  detail::CallbackTag<T>(), [=](Device & device, const RgRuntime &rg,          \
                                TransferPass &cmd, const T &data)
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
