#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Config.hpp"
#include "ResourceArena.hpp"
#include "Semaphore.hpp"
#include "Support/Any.hpp"
#include "Support/NewType.hpp"
#include "Support/String.hpp"
#include "Support/Variant.hpp"
#include "Texture.hpp"
#include "TextureIDAllocator.hpp"

#include <functional>

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

class RgRuntime;
class RgUpdate;
class RenderGraph;

template <typename F>
concept CRgBufferInitCallback =
    std::invocable<F, Device &, const BufferView &, TransferPass &>;

template <typename F>
concept CRgTextureInitCallback =
    std::invocable<F, Device &, Handle<Texture>, TransferPass &>;

template <typename F, typename T>
concept CRgUpdateCallback = std::invocable<F, RgUpdate &, const T &>;

template <typename F, typename T>
concept CRgHostCallback =
    std::invocable<F, Device &, const RgRuntime &, const T &>;

using RgHostCallback =
    std::function<void(Device &, const RgRuntime &, const Any &)>;

template <typename F, typename T>
concept CRgGraphicsCallback =
    std::invocable<F, Device &, const RgRuntime &, RenderPass &, const T &>;

using RgGraphicsCallback =
    std::function<void(Device &, const RgRuntime &, RenderPass &, const Any &)>;

template <typename F, typename T>
concept CRgComputeCallback =
    std::invocable<F, Device &, const RgRuntime &, ComputePass &, const T &>;

using RgComputeCallback = std::function<void(Device &, const RgRuntime &,
                                             ComputePass &, const Any &)>;

template <typename F, typename T>
concept CRgTransferCallback =
    std::invocable<F, Device &, const RgRuntime &, TransferPass &, const T &>;

using RgTransferCallback = std::function<void(Device &, const RgRuntime &,
                                              TransferPass &, const Any &)>;

REN_NEW_TYPE(RgRtBuffer, u32);

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
  /// Number of temporal layers
  u32 num_temporal_layers = 1;
};

REN_NEW_TYPE(RgRtTexture, u32);

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
  union {
    // Initial texture depth. Only for 3D textures
    u32 depth = 1;
    // Initial number of texture array layers. Only for non-3D textures
    u32 num_array_layers;
  };
  /// Initial number of texture mip levels
  u32 num_mip_levels = 1;
  /// Number of temporal layers
  u32 num_temporal_layers = 1;
};

REN_NEW_TYPE(RgRtSemaphore, u32);

struct RgTextureSizeInfo {
  /// New texture width
  u32 width = 0;
  /// New texture height
  u32 height = 1;
  union {
    // New texture depth. Only for 3D textures
    u32 depth = 1;
    // New number of texture array layers. Only for non-3D textures
    u32 num_array_layers;
  };
  /// New number of texture mip levels
  u32 num_mip_levels = 1;
};

class RgUpdate {
public:
  auto resize_buffer(RgRtBuffer buffer, usize size) -> bool;

  auto resize_texture(RgRtTexture texture, const RgTextureSizeInfo &size_info)
      -> bool;

private:
  RenderGraph *m_rg = nullptr;
};

class RgRuntime {
public:
  auto get_buffer(RgRtBuffer buffer) const -> const BufferView &;

  template <typename T>
  auto map_buffer(RgRtBuffer buffer, usize offset = 0) const -> T * {
    return (T *)map_buffer<std::byte>(buffer);
  }

  template <>
  auto map_buffer<std::byte>(RgRtBuffer buffer, usize offset) const
      -> std::byte *;

  auto get_texture(RgRtTexture texture) const -> Handle<Texture>;

  auto get_storage_texture_descriptor(RgRtTexture texture) const
      -> StorageTextureID;

private:
  friend RenderGraph;
  RenderGraph *m_rg = nullptr;
};

class RenderGraph {
public:
  RenderGraph(Device &device, Swapchain &swapchain,
              TextureIDAllocator &tex_alloc);

  template <typename T> auto set_pass_data(StringView pass, T data) -> bool {
    auto it = m_pass_data.find(pass);
    if (it != m_pass_data.end()) {
      it->second = std::move(data);
      return true;
    }
    return false;
  }

  auto is_pass_valid(StringView pass) -> bool;

  void execute(CommandAllocator &cmd_alloc);

private:
  friend RgRuntime;
  friend RgUpdate;

  Device *m_device = nullptr;

  using RgArena = detail::ResourceArenaImpl<Buffer, Texture, Semaphore>;
  RgArena m_arena;

  struct RgColorAttachment {
    RgRtTexture texture;
    ColorAttachmentOperations ops;
  };

  struct RgDepthStencilAttachment {
    RgRtTexture texture;
    Optional<DepthAttachmentOperations> depth_ops;
    Optional<StencilAttachmentOperations> stencil_ops;
  };

  enum class RgPassType {
    Host,
    Graphics,
    Compute,
    Transfer,
  };

  struct RgHostPass {
    RgHostCallback cb;
  };

  struct RgGraphicsPass {
    glm::uvec2 viewport;
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
    String name;
    u32 num_memory_barriers;
    u32 num_texture_barriers;
    u32 num_wait_semaphores;
    u32 num_signal_semaphores;
    RgPassType type;
  };

  Vector<RgPassRuntimeInfo> m_passes;
  Vector<RgHostPass> m_host_passes;
  Vector<RgGraphicsPass> m_graphics_passes;
  Vector<RgComputePass> m_compute_passes;
  Vector<RgTransferPass> m_transfer_passes;

  Vector<Optional<RgColorAttachment>> m_color_attachments;
  Vector<RgDepthStencilAttachment> m_depth_stencil_attachments;

  struct RgMemoryBarrier {
    VkPipelineStageFlagBits2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
    VkPipelineStageFlagBits2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dst_access_mask = VK_ACCESS_2_NONE;
  };

  struct RgTextureBarrier {
    RgRtTexture texture;
    VkPipelineStageFlagBits2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlagBits2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dst_access_mask = VK_ACCESS_2_NONE;
    VkImageLayout dst_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  struct RgSemaphoreSignal {
    RgRtSemaphore semaphore;
    VkPipelineStageFlagBits2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
    u64 value = 0;
  };

  Vector<RgMemoryBarrier> m_memory_barriers;
  Vector<RgTextureBarrier> m_texture_barriers;
  Vector<RgSemaphoreSignal> m_wait_semaphores;
  Vector<RgSemaphoreSignal> m_signal_semaphores;

  Vector<BufferView> m_buffers;

  Vector<Handle<Texture>> m_textures;
  TextureIDAllocator *m_tex_alloc = nullptr;
  Vector<StorageTextureID> m_storage_texture_descriptors;

  Vector<Handle<Semaphore>> m_semaphores;

  HashMap<String, Any> m_pass_data;

  Swapchain *m_swapchain = nullptr;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_acquire_semaphores;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_present_semaphores;
  RgRtSemaphore m_acquire_semaphore;
  RgRtSemaphore m_present_semaphore;
  RgRtTexture m_backbuffer;
};

class RgPassBuilder;

class RgBuilder {
public:
  RgBuilder(RenderGraph &rg);

  [[nodiscard]] auto create_pass(String name) -> RgPassBuilder;

  void create_buffer(RgBufferCreateInfo &&create_info);

  auto is_buffer_valid(StringView buffer) const -> bool;

  void set_buffer_init_callback(StringView buffer,
                                CRgBufferInitCallback auto cb) {
    todo();
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

  [[nodiscard]] auto read_buffer(StringView pass, StringView buffer,
                                 const RgBufferUsage &usage,
                                 u32 temporal_layer = 0) -> RgRtBuffer;

  [[nodiscard]] auto write_buffer(StringView pass, String dst_buffer,
                                  StringView src_buffer,
                                  const RgBufferUsage &usage) -> RgRtBuffer;

  [[nodiscard]] auto read_texture(StringView pass, StringView texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgRtTexture;

  [[nodiscard]] auto write_texture(StringView pass, String dst_texture,
                                   StringView src_texture,
                                   const RgTextureUsage &usage) -> RgRtTexture;

private:
  RenderGraph *m_rg = nullptr;
};

struct RgNoPassData;

class RgPassBuilder {
  String m_pass;
  RgBuilder *m_builder;

public:
  RgPassBuilder(String pass, RgBuilder &builder);

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info,
                                   const RgBufferUsage &usage) -> RgRtBuffer;

  [[nodiscard]] auto read_buffer(StringView buffer, const RgBufferUsage &usage,
                                 u32 temporal_layer = 0) -> RgRtBuffer;

  [[nodiscard]] auto write_buffer(String dst_buffer, StringView src_buffer,
                                  const RgBufferUsage &usage) -> RgRtBuffer;

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info,
                                    const RgTextureUsage &usage) -> RgRtTexture;

  [[nodiscard]] auto read_texture(StringView texture,
                                  const RgTextureUsage &usage,
                                  u32 temporal_layer = 0) -> RgRtTexture;

  [[nodiscard]] auto write_texture(String dst_texture, StringView src_texture,
                                   const RgTextureUsage &usage) -> RgRtTexture;

  [[nodiscard]] auto
  create_color_attachment(RgTextureCreateInfo &&create_info,
                          const ColorAttachmentOperations &ops) -> RgRtTexture;

  [[nodiscard]] auto
  create_depth_attachment(RgTextureCreateInfo &&create_info,
                          const DepthAttachmentOperations &ops) -> RgRtTexture;

  [[nodiscard]] auto
  write_color_attachment(StringView dst_texture, StringView src_texture,
                         const ColorAttachmentOperations &ops) -> RgRtTexture;

  [[nodiscard]] auto
  write_depth_attachment(StringView dst_texture, StringView src_texture,
                         const DepthAttachmentOperations &ops) -> RgRtTexture;

  template <typename T> void set_update_callback(CRgUpdateCallback<T> auto cb) {
    todo();
  }

  template <typename T>
  void set_update_callback(detail::CallbackTag<T>,
                           CRgUpdateCallback<T> auto cb) {
    set_update_callback<T>(std::move(cb));
  }

#define ren_rg_update_callback(T)                                              \
  detail::CallbackTag<T>(), [=](RgUpdate & rg, const T &data)

  template <typename T> void set_host_callback(CRgHostCallback<T> auto cb) {
    todo();
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
    todo();
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
    todo();
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
    todo();
  }

  template <typename T>
  void set_transfer_callback(detail::CallbackTag<T>,
                             CRgTransferCallback<T> auto cb) {
    set_transfer_callback<T>(std::move(cb));
  }

#define ren_rg_transfer_callback(T)                                            \
  detail::CallbackTag<T>(), [=](Device & device, const RgRuntime &rg,          \
                                TransferPass &cmd, const T &data)
};

} // namespace ren
