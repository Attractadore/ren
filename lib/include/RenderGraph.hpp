#pragma once
#include "Attachments.hpp"
#include "Buffer.hpp"
#include "Config.hpp"
#include "Descriptors.hpp"
#include "ResourceArena.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/HashSet.hpp"
#include "Support/NewType.hpp"
#include "Support/Optional.hpp"
#include "Support/SecondaryMap.hpp"
#include "Support/Variant.hpp"
#include "Texture.hpp"

#include <functional>

#ifndef REN_RENDER_GRAPH_DEBUG_NAMES
#define REN_RENDER_GRAPH_DEBUG_NAMES 0
#endif

namespace ren {

#if REN_RENDER_GRAPH_DEBUG_NAMES

using RGDebugName = std::string;

#define REN_RENDER_GRAPH_DEBUG_NAME_FIELD RGDebugName name = "Unknown"

#else

struct RGDebugName {
  RGDebugName() = default;
  RGDebugName(const char *) {}
  RGDebugName(const std::string &) {}
  RGDebugName(std::string_view) {}
};

#define REN_RENDER_GRAPH_DEBUG_NAME_FIELD [[no_unique_address]] RGDebugName name

#endif

class CommandAllocator;
class CommandRecorder;
class ComputePass;
class Device;
class RenderGraph;
class RenderPass;
class Swapchain;

REN_NEW_TYPE(RGPassID, unsigned);
REN_DEFINE_SLOTMAP_KEY(RGBufferID);
REN_DEFINE_SLOTMAP_KEY(RGTextureID);

enum class RGPassType {
  Host,
  Graphics,
  Compute,
  Transfer,
};

struct RGPassCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  RGPassType type = RGPassType::Host;
};

struct RGBufferState {
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
};

struct RGBufferReadInfo {
  RGBufferID buffer;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
};

struct RGBufferWriteInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  RGBufferID buffer;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  bool temporal = false;
};

struct RGBufferCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  BufferHeap heap = BufferHeap::Upload;
  size_t size = 0;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  bool temporal = false;
};

struct RGBufferImportInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  BufferView buffer;
  RGBufferState state;
};

struct RGTextureState {
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RGTextureReadInfo {
  RGTextureID texture;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RGTextureWriteInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  RGTextureID texture;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool temporal = false;
};

struct RGTextureCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  union {
    struct {
      u32 width = 1;
      u32 height = 1;
      union {
        u32 depth;
        u32 num_array_layers = 1;
      };
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels = 1;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool temporal = false;
};

struct RGColorAttachmentWriteInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  u32 index = 0;
  RGTextureID texture;
  bool temporal = false;
};

struct RGColorAttachmentCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  u32 index = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  union {
    struct {
      u32 width = 1;
      u32 height = 1;
      u32 num_array_layers = 1;
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels = 1;
  Optional<glm::vec4> clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  bool temporal = false;
};

struct RGDepthStencilAttachmentReadInfo {
  RGTextureID texture;
  bool read_depth = true;
  bool read_stencil = false;
};

struct RGDepthAttachmentWriteOperations {
  Optional<float> clear_depth = 0.0f;
};

struct RGStencilAttachmentWriteOperations {
  Optional<u8> clear_stencil;
};

struct RGDepthStencilAttachmentWriteInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  RGTextureID texture;
  Optional<RGDepthAttachmentWriteOperations> depth_ops =
      RGDepthAttachmentWriteOperations();
  Optional<RGStencilAttachmentWriteOperations> stencil_ops;
  bool temporal = false;
};

struct RGDepthStencilAttachmentCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  VkFormat format = VK_FORMAT_UNDEFINED;
  union {
    struct {
      u32 width = 1;
      u32 height = 1;
      u32 num_array_layers = 1;
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels = 1;
  Optional<float> clear_depth = 0.0f;
  Optional<u8> clear_stencil;
  bool temporal = false;
};

struct RGTextureImportInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  TextureView texture;
  RGTextureState state;
};

struct RGSemaphoreSignalInfo {
  Handle<Semaphore> semaphore;
  u64 value = 0;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
};

class RGRuntime {
  SlotMap<unsigned, RGBufferID> m_buffers;
  Vector<BufferView> m_physical_buffers;

  SlotMap<unsigned, RGTextureID> m_textures;
  Vector<TextureView> m_physical_textures;

private:
  friend class RenderGraph;

public:
  auto get_buffer(RGBufferID buffer) const -> const BufferView &;

  auto get_texture(RGTextureID texture) const -> const TextureView &;
};

using RGHostPassCallback = std::function<void(Device &device, RGRuntime &rg)>;

using RGGraphicsPassCallback =
    std::function<void(Device &device, RGRuntime &rg, RenderPass &render_pass)>;

using RGComputePassCallback =
    std::function<void(Device &device, RGRuntime &rg, ComputePass &pass)>;

using RGTransferPassCallback =
    std::function<void(Device &device, RGRuntime &rg, CommandRecorder &cmd)>;

using RGPassCallback =
    std::function<void(Device &device, RGRuntime &rg, CommandRecorder &cmd)>;

class RenderGraph {
  struct RGBatch {
    SmallVector<RGSemaphoreSignalInfo> wait_semaphores;
    SmallVector<RGSemaphoreSignalInfo> signal_semaphores;
    Vector<RGPassCallback> pass_cbs;
#if REN_RENDER_GRAPH_DEBUG_NAMES
    Vector<std::string> pass_names;
#endif
  };

  ResourceArena m_arena;

  Vector<RGBatch> m_batches;

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

  RGRuntime m_runtime;

#if REN_RENDER_GRAPH_DEBUG_NAMES
  SecondaryMap<std::string, RGBufferID> m_buffer_names;
  SecondaryMap<std::string, RGTextureID> m_texture_names;
#endif

  Vector<RGBufferState> m_buffer_states;
  Vector<RGTextureState> m_texture_states;

  HashSet<RGBufferID> m_temporal_buffers;
  HashSet<RGTextureID> m_temporal_textures;

  HashSet<unsigned> m_external_buffers;
  HashSet<unsigned> m_external_textures;

private:
  void retain_temporal_resources();

public:
  RenderGraph(Device &device);

  class Builder;

  void execute(Device &device, CommandAllocator &cmd_allocator);
};

class RenderGraph::Builder {
  struct RGTextureAccess {
    RGTextureID texture;
    RGTextureState state;
  };

  struct RGBufferAccess {
    RGBufferID buffer;
    RGBufferState state;
  };

  struct RGPass {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    SmallVector<RGTextureAccess> read_textures;
    SmallVector<RGTextureAccess> write_textures;
    SmallVector<RGBufferAccess> read_buffers;
    SmallVector<RGBufferAccess> write_buffers;
    SmallVector<RGSemaphoreSignalInfo> wait_semaphores;
    SmallVector<RGSemaphoreSignalInfo> signal_semaphores;
    RGPassCallback cb;
  };

  RenderGraph *m_rg = nullptr;

  Vector<RGPass> m_passes = {{}};

  HashMap<unsigned, BufferCreateInfo> m_buffer_create_infos;
  HashMap<RGBufferID, RGPassID> m_buffer_defs;
  HashMap<RGBufferID, RGPassID> m_buffer_kills;

  HashMap<unsigned, TextureCreateInfo> m_texture_create_infos;
  HashMap<RGTextureID, RGPassID> m_texture_defs;
  HashMap<RGTextureID, RGPassID> m_texture_kills;

#if REN_RENDER_GRAPH_DEBUG_NAMES
  Vector<std::string> m_pass_names = {{}};
#endif

private:
  [[nodiscard]] auto init_new_pass(RGPassType type, RGDebugName name)
      -> RGPassID;

  [[nodiscard]] auto init_new_texture(Optional<RGPassID> pass,
                                      Optional<RGTextureID> from_texture,
                                      RGDebugName name) -> RGTextureID;

  auto get_texture_def(RGTextureID tex) const -> Optional<RGPassID>;
  auto get_texture_kill(RGTextureID tex) const -> Optional<RGPassID>;

  void read_texture(RGPassID pass, RGTextureReadInfo &&read_info);

  [[nodiscard]] auto write_texture(RGPassID pass,
                                   RGTextureWriteInfo &&write_info)
      -> RGTextureID;

  [[nodiscard]] auto create_texture(RGPassID pass,
                                    RGTextureCreateInfo &&create_info)
      -> RGTextureID;

  [[nodiscard]] auto init_new_buffer(Optional<RGPassID> pass,
                                     Optional<RGBufferID> from_buffer,
                                     RGDebugName name) -> RGBufferID;

  auto get_buffer_def(RGBufferID tex) const -> Optional<RGPassID>;
  auto get_buffer_kill(RGBufferID tex) const -> Optional<RGPassID>;

  void read_buffer(RGPassID pass, RGBufferReadInfo &&read_info);

  [[nodiscard]] auto write_buffer(RGPassID pass, RGBufferWriteInfo &&write_info)
      -> RGBufferID;

  [[nodiscard]] auto create_buffer(RGPassID pass,
                                   RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  void wait_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);
  void signal_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);

  void set_callback(RGPassID pass, RGPassCallback cb);

  void create_buffers(const Device &device);

  void create_textures(const Device &device);

  auto schedule_passes() -> Vector<RGPassID>;

  void insert_barriers(Device &device);

  auto batch_passes(std::span<const RGPassID> schedule) -> Vector<RGBatch>;

  void print_resources() const;

  void print_passes(std::span<const RGPassID> passes) const;

public:
  Builder(RenderGraph &rg);

  class PassBuilder;
  [[nodiscard]] auto create_pass(RGPassCreateInfo &&create_info) -> PassBuilder;

  [[nodiscard]] auto import_buffer(RGBufferImportInfo &&import_info)
      -> RGBufferID;

  [[nodiscard]] auto import_texture(RGTextureImportInfo &&import_info)
      -> RGTextureID;

  void present(Swapchain &swapchain, RGTextureID texture,
               Handle<Semaphore> acquire_semaphore,
               Handle<Semaphore> present_semaphore);

  void build(Device &device);
};
using RGBuilder = RenderGraph::Builder;

class RenderGraph::Builder::PassBuilder {
  RGPassID m_pass;
  Builder *m_builder;

  struct RGColorAttachment {
    RGTextureID texture;
    ColorAttachmentOperations ops;
  };

  struct RGDepthStencilAttachment {
    RGTextureID texture;
    Optional<DepthAttachmentOperations> depth_ops;
    Optional<StencilAttachmentOperations> stencil_ops;
  };

  struct RGRenderPassBeginInfo {
    StaticVector<Optional<RGColorAttachment>, MAX_COLOR_ATTACHMENTS>
        color_attachments;
    Optional<RGDepthStencilAttachment> depth_stencil_attachment;
  };

  RGRenderPassBeginInfo m_render_pass;

  void set_color_attachment(RGColorAttachment, u32 index);

public:
  PassBuilder(RGPassID pass, Builder &builder);

  void read_buffer(RGBufferReadInfo &&read_info);

  [[nodiscard]] auto write_buffer(RGBufferWriteInfo &&write_info) -> RGBufferID;

  [[nodiscard]] auto create_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  void read_indirect_buffer(RGBufferReadInfo &&read_info);

  void read_index_buffer(RGBufferReadInfo &&read_info);

  void read_vertex_shader_buffer(RGBufferReadInfo &&read_info);

  void read_fragment_shader_buffer(RGBufferReadInfo &&read_info);

  [[nodiscard]] auto create_uniform_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  void read_compute_buffer(RGBufferReadInfo &&read_info);

  [[nodiscard]] auto write_compute_buffer(RGBufferWriteInfo &&write_info)
      -> RGBufferID;

  [[nodiscard]] auto create_compute_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  void read_transfer_buffer(RGBufferReadInfo &&read_info);

  [[nodiscard]] auto write_transfer_buffer(RGBufferWriteInfo &&write_info)
      -> RGBufferID;

  [[nodiscard]] auto create_transfer_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  [[nodiscard]] auto create_upload_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID;

  void read_texture(RGTextureReadInfo &&read_info) {
    m_builder->read_texture(m_pass, std::move(read_info));
  }

  [[nodiscard]] RGTextureID write_texture(RGTextureWriteInfo &&write_info) {
    return m_builder->write_texture(m_pass, std::move(write_info));
  }

  [[nodiscard]] RGTextureID create_texture(RGTextureCreateInfo &&create_info) {
    return m_builder->create_texture(m_pass, std::move(create_info));
  }

  /// Access color attachment with LOAD_OP_LOAD and STORE_OP_STORE
  [[nodiscard]] auto
  write_color_attachment(RGColorAttachmentWriteInfo &&write_info)
      -> RGTextureID;

  /// Create and access color attachment with LOAD_OP_CLEAR or LOAD_OP_DONT_CARE
  /// and STORE_OP_STORE
  [[nodiscard]] auto
  create_color_attachment(RGColorAttachmentCreateInfo &&create_info)
      -> RGTextureID;

  /// Access depth-stencil attachment with LOAD_OP_LOAD and STORE_OP_NONE
  void
  read_depth_stencil_attachment(RGDepthStencilAttachmentReadInfo &&read_info);

  /// Access depth-stencil attachment with LOAD_OP_LOAD or LOAD_OP_CLEAR and
  /// STORE_OP_STORE
  [[nodiscard]] auto
  write_depth_stencil_attachment(RGDepthStencilAttachmentWriteInfo &&write_info)
      -> RGTextureID;

  /// Create and access depth-stencil attachment with LOAD_OP_CLEAR and
  /// STORE_OP_STORE
  [[nodiscard]] auto create_depth_stencil_attachment(
      RGDepthStencilAttachmentCreateInfo &&create_info) -> RGTextureID;

  void read_storage_texture(RGTextureReadInfo &&read_info);

  [[nodiscard]] auto write_storage_texture(RGTextureWriteInfo &&write_info)
      -> RGTextureID;

  [[nodiscard]] auto create_storage_texture(RGTextureCreateInfo &&create_info)
      -> RGTextureID;

  void read_sampled_texture(RGTextureReadInfo &&read_info);

  void read_transfer_texture(RGTextureReadInfo &&read_info);

  [[nodiscard]] auto write_transfer_texture(RGTextureWriteInfo &&write_info)
      -> RGTextureID;

  [[nodiscard]] auto create_transfer_texture(RGTextureCreateInfo &&create_info)
      -> RGTextureID;

  void wait_semaphore(RGSemaphoreSignalInfo &&signal_info) {
    m_builder->wait_semaphore(m_pass, std::move(signal_info));
  }

  void signal_semaphore(RGSemaphoreSignalInfo &&signal_info) {
    m_builder->signal_semaphore(m_pass, std::move(signal_info));
  }

  void set_host_callback(RGHostPassCallback cb);

  void set_graphics_callback(RGGraphicsPassCallback cb);

  void set_compute_callback(RGComputePassCallback cb);

  void set_transfer_callback(RGTransferPassCallback cb);
};
using RGPassBuilder = RenderGraph::Builder::PassBuilder;

} // namespace ren
