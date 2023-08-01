#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Config.hpp"
#include "ResourceArena.hpp"
#include "Semaphore.hpp"
#include "Support/Optional.hpp"
#include "Support/Span.hpp"
#include "Support/Variant.hpp"
#include "Texture.hpp"
#include "TextureIDAllocator.hpp"

#include <functional>

#ifndef REN_RENDER_GRAPH_DEBUG_NAMES
#define REN_RENDER_GRAPH_DEBUG_NAMES 0
#endif

namespace ren {

#if REN_RENDER_GRAPH_DEBUG_NAMES

using RgDebugName = std::string;

#define REN_RG_DEBUG_NAME_FIELD RgDebugName name = "Unknown"

#else

struct RgDebugName {
  RgDebugName() = default;
  RgDebugName(const char *) {}
  RgDebugName(const std::string &) {}
  RgDebugName(std::string_view) {}
};

#define REN_RG_DEBUG_NAME_FIELD [[no_unique_address]] RgDebugName name

#endif

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
class RenderGraph;

template <typename F, typename T>
concept CRgBudgetCallback = std::invocable<F, RenderGraph &, const T &>;

template <typename F, typename T>
concept CRgHostCallback =
    std::invocable<F, Device &, const RgRuntime &, const T &>;

template <typename F, typename T>
concept CRgGraphicsCallback =
    std::invocable<F, Device &, const RgRuntime &, RenderPass &, const T &>;

template <typename F, typename T>
concept CRgComputeCallback =
    std::invocable<F, Device &, const RgRuntime &, ComputePass &, const T &>;

template <typename F, typename T>
concept CRgTransferCallback =
    std::invocable<F, Device &, const RgRuntime &, TransferPass &, const T &>;

REN_DEFINE_SLOTMAP_KEY(RgPass);

enum class RgPassType {
  /// Pass will contain host commands
  Host,
  /// Pass will contain graphics commands
  Graphics,
  /// Pass will contain compute commands on the graphics queue
  Compute,
  /// Pass will contain transfer commands on the graphics queue
  Transfer,
};

struct RgPassCreateInfo {
  /// Debug name for this pass
  REN_RG_DEBUG_NAME_FIELD;
  /// Type of this pass
  RgPassType type = RgPassType::Host;
};

REN_DEFINE_SLOTMAP_KEY(RgBuffer);

/// Handle used to get a device buffer handle from the render graph runtime
class RgRtBuffer {
private:
  friend class RgRuntime;
  friend class RenderGraph;
  RgBuffer buffer;

public:
  operator RgBuffer() const { return buffer; }

  explicit operator bool() const { return not buffer.is_null(); }
};

struct RgBufferAccess {
  /// Pipeline stages in which this buffer is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Memory accesses performed on this buffer
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
};

struct RgBufferCreateInfo {
  /// Buffer debug name
  REN_RG_DEBUG_NAME_FIELD;
  /// Existing ID to use
  RgBuffer target;
  /// Initial buffer size
  usize size = 0;
  /// Memory heap from which to allocate buffer
  BufferHeap heap = BufferHeap::Upload;
  /// How many previous frames' instances to keep
  u32 temporal_count = 0;
  /// Initial data for previous frames' instances
  TempSpan<const std::byte> temporal_init;
};

struct RgBufferReadInfo {
  /// Buffer to read
  RgBuffer buffer;
  /// Which previous frame's instance to read
  u32 temporal_offset = 0;
};

struct RgBufferWriteInfo {
  /// Debug name for modified buffer
  REN_RG_DEBUG_NAME_FIELD;
  /// Existing ID to use
  RgBuffer target;
  /// Buffer to modify
  RgBuffer buffer;
  /// How many previous frames' instances to keep
  u32 temporal_count = 0;
  /// Initial data for previous frames' instances
  TempSpan<const std::byte> temporal_init;
};

struct RgBufferResizeInfo {
  /// Buffer to resize
  RgBuffer buffer;
  /// New size for this buffer
  usize size = 0;
  /// Initial data for previous frames' instances
  TempSpan<const std::byte> temporal_init;
};

REN_DEFINE_SLOTMAP_KEY(RgTexture);

/// Handle used to get a device texture handle from the render graph runtime
class RgRtTexture {
private:
  friend class RgRuntime;
  friend class RenderGraph;
  RgTexture texture;
};

struct RgTextureAccess {
  /// Pipeline stages in which the texture is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Types of accesses performed on the texture
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  /// Layout of the texture
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RgTextureCreateInfo {
  /// Texture debug name
  REN_RG_DEBUG_NAME_FIELD;
  /// Existing ID to use
  RgTexture target;
  /// Texture type
  VkImageType type = VK_IMAGE_TYPE_2D;
  /// Texture format
  VkFormat format = VK_FORMAT_UNDEFINED;
  /// Initial texture size
  glm::uvec3 size = {0, 0, 0};
  /// Number of mip levels
  u32 num_mip_levels = 1;
  /// How many previous frames' instances to keep
  u32 temporal_count = 0;
  /// Initial color or depth-stencil for previous frames' instances
  Optional<Variant<glm::vec4, ClearDepthStencil>> temporal_init;
};

struct RgTextureReadInfo {
  /// Texture to read
  RgTexture texture;
  /// Which previous frame's instance to read
  u32 temporal_offset = 0;
};

struct RgTextureWriteInfo {
  /// Name for modified texture
  REN_RG_DEBUG_NAME_FIELD;
  /// Existing ID to use
  RgTexture target;
  /// Texture to modify
  RgTexture texture;
  /// How many previous frames' instances to keep
  u32 temporal_count = 0;
  /// Initial color or depth and stencil for previous frames' instances
  Optional<Variant<glm::vec4, ClearDepthStencil>> temporal_init;
};

struct RgTextureResizeInfo {
  /// Texture to resize
  RgTexture texture;
  /// New texture size
  glm::uvec3 size = {0, 0, 0};
  /// Initial color or depth and stencil for previous frames' instances
  Optional<Variant<glm::vec4, ClearDepthStencil>> temporal_init;
};

class RgRuntime {
public:
  auto get_buffer(RgRtBuffer buffer) const -> const BufferView &;

  auto get_texture(RgRtTexture texture) const -> const TextureView &;

  auto get_sampled_texture(RgRtTexture texture) const
      -> std::tuple<const TextureView &, SampledTextureID>;

  auto get_storage_texture(RgRtTexture texture) const
      -> std::tuple<const TextureView &, StorageTextureID>;
};

class RenderGraph {
  using RgArena = detail::ResourceArenaImpl<Buffer, Texture, Semaphore>;
  RgArena m_arena;

  Device *m_device = nullptr;

  Swapchain *m_swapchain = nullptr;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_acquire_semaphores;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_present_semaphores;
  TextureIDAllocator *m_tex_alloc = nullptr;

  RgRuntime m_runtime;

public:
  RenderGraph(Device &device, Swapchain &swapchain,
              TextureIDAllocator &tex_alloc);

  class Builder;

  void resize_buffer(RgBufferResizeInfo &&resize_info);
  void resize_texture(RgTextureResizeInfo &&resize_info);

  template <typename T> void set_pass_data(RgPass pass, T data) { todo(); }

  void execute(Device &device, CommandAllocator &cmd_alloc);
};

class RenderGraph::Builder {
private:
  [[nodiscard]] auto create_buffer(RgPass pass,
                                   RgBufferCreateInfo &&create_info,
                                   const RgBufferAccess &access)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  auto read_buffer(RgPass pass, RgBufferReadInfo &&read_info,
                   const RgBufferAccess &access) -> RgRtBuffer;

  [[nodiscard]] auto write_buffer(RgPass pass, RgBufferWriteInfo &&write_info,
                                  const RgBufferAccess &access)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto create_texture(RgPass pass,
                                    RgTextureCreateInfo &&create_info,
                                    const RgTextureAccess &access)
      -> std::tuple<RgTexture, RgRtTexture>;

  auto read_texture(RgPass pass, RgTextureReadInfo &&read_info,
                    const RgTextureAccess &access) -> RgRtTexture;

  [[nodiscard]] auto write_texture(RgPass pass, RgTextureWriteInfo &&write_info,
                                   const RgTextureAccess &access)
      -> std::tuple<RgTexture, RgRtTexture>;

public:
  Builder(RenderGraph &rg);

  class PassBuilder;
  [[nodiscard]] auto create_pass(RgPassCreateInfo &&create_info) -> PassBuilder;

  [[nodiscard]] auto declare_buffer() -> RgBuffer;

  [[nodiscard]] auto declare_texture() -> RgTexture;

  void present(RgTexture texture);

  void build();
};
using RgBuilder = RenderGraph::Builder;

struct RgNoPassData;

class RgBuilder::PassBuilder {
  RgPass m_pass;
  RgBuilder *m_builder;

public:
  PassBuilder(RgPass pass, RgBuilder &builder);

  [[nodiscard]] auto create_buffer(RgBufferCreateInfo &&create_info,
                                   const RgBufferAccess &access)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto create_compute_buffer(RgBufferCreateInfo &&create_info)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto create_uniform_buffer(RgBufferCreateInfo &&create_info)
      -> RgRtBuffer;

  [[nodiscard]] auto create_transfer_buffer(RgBufferCreateInfo &&create_info)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto create_upload_buffer(RgBufferCreateInfo &&create_info)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto read_buffer(RgBufferReadInfo &&read_info,
                                 const RgBufferAccess &access) -> RgRtBuffer;

  [[nodiscard]] auto read_indirect_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto read_index_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto read_vertex_shader_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto read_fragment_shader_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto read_compute_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto read_transfer_buffer(RgBufferReadInfo &&read_info)
      -> RgRtBuffer;

  [[nodiscard]] auto write_buffer(RgBufferWriteInfo &&write_info,
                                  const RgBufferAccess &access)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto write_compute_buffer(RgBufferWriteInfo &&write_info)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto write_transfer_buffer(RgBufferWriteInfo &&write_info)
      -> std::tuple<RgBuffer, RgRtBuffer>;

  [[nodiscard]] auto create_texture(RgTextureCreateInfo &&create_info,
                                    const RgTextureAccess &access)
      -> std::tuple<RgTexture, RgRtTexture>;

  /// Create color attachment with LOAD_OP_CLEAR or LOAD_OP_DONT_CARE and
  /// STORE_OP_STORE
  [[nodiscard]] auto
  create_color_attachment(RgTextureCreateInfo &&create_info,
                          const ColorAttachmentOperations &ops) -> RgTexture;

  /// Create depth attachment with LOAD_OP_CLEAR and STORE_OP_STORE
  [[nodiscard]] auto
  create_depth_attachment(RgTextureCreateInfo &&create_info,
                          const DepthAttachmentOperations &ops) -> RgTexture;

  [[nodiscard]] auto create_storage_texture(RgTextureCreateInfo &&create_info)
      -> std::tuple<RgTexture, RgRtTexture>;

  [[nodiscard]] auto create_transfer_texture(RgTextureCreateInfo &&create_info)
      -> std::tuple<RgTexture, RgRtTexture>;

  [[nodiscard]] auto read_texture(RgTextureReadInfo &&read_info,
                                  const RgTextureAccess &access) -> RgRtTexture;

  /// Read depth attachment with LOAD_OP_LOAD and STORE_OP_NONE
  [[nodiscard]] auto read_depth_attachment(RgTextureReadInfo &&read_info,
                                           const DepthAttachmentOperations &ops)
      -> RgRtTexture;

  [[nodiscard]] auto read_storage_texture(RgTextureReadInfo &&read_info)
      -> RgRtTexture;

  [[nodiscard]] auto read_sampled_texture(RgTextureReadInfo &&read_info)
      -> RgRtTexture;

  [[nodiscard]] auto read_transfer_texture(RgTextureReadInfo &&read_info)
      -> RgRtTexture;

  [[nodiscard]] auto write_texture(RgTextureWriteInfo &&write_info,
                                   const RgTextureAccess &access)
      -> std::tuple<RgTexture, RgRtTexture>;

  /// Write color attachment with LOAD_OP_LOAD and STORE_OP_STORE
  [[nodiscard]] auto
  write_color_attachment(RgTextureWriteInfo &&write_info,
                         const ColorAttachmentOperations &ops)
      -> std::tuple<RgTexture, RgRtTexture>;

  /// Write depth attachment with LOAD_OP_LOAD and STORE_OP_STORE
  [[nodiscard]] auto
  write_depth_attachment(RgTextureWriteInfo &&write_info,
                         const DepthAttachmentOperations &ops)
      -> std::tuple<RgTexture, RgRtTexture>;

  [[nodiscard]] auto write_storage_texture(RgTextureWriteInfo &&write_info)
      -> std::tuple<RgTexture, RgRtTexture>;

  [[nodiscard]] auto write_transfer_texture(RgTextureWriteInfo &&write_info)
      -> std::tuple<RgTexture, RgRtTexture>;

  template <typename T> void set_size_callback(CRgBudgetCallback<T> auto cb) {
    todo();
  }

  template <typename T>
  void set_size_callback(detail::CallbackTag<T>, CRgBudgetCallback<T> auto cb) {
    set_size_callback<T>(std::move(cb));
  }

#define ren_rg_size_callback(T)                                                \
  detail::CallbackTag<T>(), [=](RenderGraph & rg, const T &data)

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

  operator RgPass() const;
};
using RgPassBuilder = RgBuilder::PassBuilder;

} // namespace ren
