#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/HashSet.hpp"
#include "Support/NewType.hpp"
#include "Support/Optional.hpp"
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
class CommandBuffer;
class Device;
class RenderGraph;
class ResourceArena;
class Swapchain;

REN_NEW_TYPE(RGPassID, unsigned);
REN_NEW_TYPE(RGTextureID, unsigned);
REN_NEW_TYPE(RGBufferID, unsigned);

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
};

struct RGBufferCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  BufferHeap heap = BufferHeap::Upload;
  size_t size = 0;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  bool preserve = false;
};

struct RGBufferImportInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  BufferView buffer;
  RGBufferState state;
};

struct RGBufferExportInfo {
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
};

struct RGTextureCreateInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  union {
    struct {
      u32 width;
      u32 height;
      union {
        u32 depth;
        u32 num_array_layers;
      };
    };
    glm::uvec3 size = {1, 1, 1};
  };
  u32 num_mip_levels = 1;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool preserve = false;
};

struct RGTextureImportInfo {
  REN_RENDER_GRAPH_DEBUG_NAME_FIELD;
  TextureView texture;
  RGTextureState state;
};

struct RGTextureExportInfo {
  TextureView texture;
  RGTextureState state;
};

struct RGSemaphoreSignalInfo {
  Handle<Semaphore> semaphore;
  u64 value = 0;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
};

class RGRuntime {
  Vector<unsigned> m_buffers;
  Vector<BufferView> m_physical_buffers;
  Vector<unsigned> m_textures;
  Vector<TextureView> m_physical_textures;

private:
  friend class RenderGraph;

  struct Config {
    Vector<unsigned> buffers;
    Vector<BufferView> physical_buffers;
    Vector<unsigned> textures;
    Vector<TextureView> physical_textures;
  };

  RGRuntime(Config config)
      : m_buffers(std::move(config.buffers)),
        m_physical_buffers(std::move(config.physical_buffers)),
        m_textures(std::move(config.textures)),
        m_physical_textures(std::move(config.physical_textures)) {}

public:
  auto get_buffer(RGBufferID buffer) const -> const BufferView &;

  auto get_texture(RGTextureID texture) const -> const TextureView &;
};

using RGCallback =
    std::function<void(Device &device, RGRuntime &rg, CommandBuffer &cmd)>;

class RenderGraph {
  struct RGBatch {
    SmallVector<RGSemaphoreSignalInfo> wait_semaphores;
    SmallVector<RGSemaphoreSignalInfo> signal_semaphores;
    Vector<RGCallback> barrier_cbs;
    Vector<RGCallback> pass_cbs;
#if REN_RENDER_GRAPH_DEBUG_NAMES
    Vector<std::string> pass_names;
#endif
  };

  Vector<RGBatch> m_batches;

  RGRuntime m_runtime;

  Vector<RGBufferState> m_buffer_states;
  Vector<RGTextureState> m_texture_states;

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

private:
  struct Config {
    Vector<RGBatch> batches;
    Vector<unsigned> buffers;
    Vector<BufferView> physical_buffers;
    Vector<RGBufferState> buffer_states;
    Vector<unsigned> textures;
    Vector<TextureView> physical_textures;
    Vector<RGTextureState> texture_states;
    Swapchain *swapchain;
    Handle<Semaphore> present_semaphore;
  };

  RenderGraph(Config config)
      : m_batches(std::move(config.batches)),
        m_runtime({
            .buffers = std::move(config.buffers),
            .physical_buffers = std::move(config.physical_buffers),
            .textures = std::move(config.textures),
            .physical_textures = std::move(config.physical_textures),
        }),
        m_buffer_states(std::move(config.buffer_states)),
        m_texture_states(std::move(config.texture_states)),
        m_swapchain(config.swapchain),
        m_present_semaphore(config.present_semaphore) {}

public:
  class Builder;

  auto export_buffer(RGBufferID buffer) const -> RGBufferExportInfo;

  auto export_texture(RGTextureID texture) const -> RGTextureExportInfo;

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
    RGCallback barrier_cb;
    RGCallback pass_cb;
  };

  Vector<RGPass> m_passes = {{}};

  Vector<unsigned> m_buffers = {{}};
  Vector<BufferView> m_physical_buffers;
  Vector<RGBufferState> m_buffer_states;
  HashMap<unsigned, BufferCreateInfo> m_buffer_create_infos;
  HashSet<unsigned> m_preserved_buffers;
  HashMap<RGBufferID, RGPassID> m_buffer_defs;
  HashMap<RGBufferID, RGPassID> m_buffer_kills;

  Vector<unsigned> m_textures = {{}};
  Vector<TextureView> m_physical_textures;
  Vector<RGTextureState> m_texture_states;
  HashMap<unsigned, TextureCreateInfo> m_texture_create_infos;
  HashSet<unsigned> m_preserved_textures;
  HashMap<RGTextureID, RGPassID> m_texture_defs;
  HashMap<RGTextureID, RGPassID> m_texture_kills;

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

#if REN_RENDER_GRAPH_DEBUG_NAMES
  Vector<std::string> m_pass_names = {{}};
  Vector<std::string> m_buffer_names = {{}};
  Vector<std::string> m_texture_names = {{}};
#endif

private:
  [[nodiscard]] auto init_new_pass(RGPassType type, RGDebugName name)
      -> RGPassID;

private:
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

public:
  [[nodiscard]] auto import_texture(RGTextureImportInfo &&import_info)
      -> RGTextureID;

private:
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

public:
  [[nodiscard]] auto import_buffer(RGBufferImportInfo &&import_info)
      -> RGBufferID;

private:
  void wait_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);
  void signal_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);

  void set_callback(RGPassID pass, RGCallback cb);

private:
  void create_buffers(const Device &device, ResourceArena &frame_arena,
                      ResourceArena &next_frame_arena);

  void create_textures(const Device &device, ResourceArena &frame_arena,
                       ResourceArena &next_frame_arena);

  auto schedule_passes() -> Vector<RGPassID>;

  void insert_barriers(Device &device);

  auto batch_passes(std::span<const RGPassID> schedule) -> Vector<RGBatch>;

private:
  void print_resources() const;

  void print_passes(std::span<const RGPassID> passes) const;

public:
  class PassBuilder;
  [[nodiscard]] auto create_pass(RGPassCreateInfo &&create_info) -> PassBuilder;

  void present(Swapchain &swapchain, RGTextureID texture,
               Handle<Semaphore> acquire_semaphore,
               Handle<Semaphore> present_semaphore);

  [[nodiscard]] auto build(Device &device, ResourceArena &frame_arena,
                           ResourceArena &next_frame_arena) -> RenderGraph;
};
using RGBuilder = RenderGraph::Builder;

class RenderGraph::Builder::PassBuilder {
  RGPassID m_pass;
  Builder *m_builder;

public:
  PassBuilder(RGPassID pass, Builder &builder)
      : m_pass(pass), m_builder(&builder) {}

  void read_buffer(RGBufferReadInfo &&read_info) {
    m_builder->read_buffer(m_pass, std::move(read_info));
  }

  [[nodiscard]] auto write_buffer(RGBufferWriteInfo &&write_info)
      -> RGBufferID {
    return m_builder->write_buffer(m_pass, std::move(write_info));
  }

  [[nodiscard]] auto create_buffer(RGBufferCreateInfo &&create_info)
      -> RGBufferID {
    return m_builder->create_buffer(m_pass, std::move(create_info));
  }

  void read_texture(RGTextureReadInfo &&read_info) {
    m_builder->read_texture(m_pass, std::move(read_info));
  }

  [[nodiscard]] RGTextureID write_texture(RGTextureWriteInfo &&write_info) {
    return m_builder->write_texture(m_pass, std::move(write_info));
  }

  [[nodiscard]] RGTextureID create_texture(RGTextureCreateInfo &&create_info) {
    return m_builder->create_texture(m_pass, std::move(create_info));
  }

  void wait_semaphore(RGSemaphoreSignalInfo &&signal_info) {
    m_builder->wait_semaphore(m_pass, std::move(signal_info));
  }

  void signal_semaphore(RGSemaphoreSignalInfo &&signal_info) {
    m_builder->signal_semaphore(m_pass, std::move(signal_info));
  }

  void set_callback(RGCallback cb) {
    return m_builder->set_callback(m_pass, std::move(cb));
  }
};
using RGPassBuilder = RenderGraph::Builder::PassBuilder;

} // namespace ren
