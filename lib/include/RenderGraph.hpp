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

namespace ren {

class CommandAllocator;
class CommandBuffer;
class Device;
class RenderGraph;
class ResourceArena;
class Swapchain;

REN_NEW_TYPE(RGPassID, unsigned);
REN_NEW_TYPE(RGTextureID, unsigned);
REN_NEW_TYPE(RGBufferID, unsigned);

struct RGBufferState {
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
};

struct RGBufferReadInfo {
  RGBufferID buffer;
  RGBufferState state;
};

struct RGBufferWriteInfo {
  std::string name;
  RGBufferID buffer;
  RGBufferState state;
};

struct RGBufferCreateInfo {
  std::string name;
  REN_DEBUG_NAME_FIELD("RenderGraph Buffer");
  BufferHeap heap = BufferHeap::Upload;
  size_t size = 0;
  RGBufferState state;
  bool preserve = false;
};

struct RGBufferImportInfo {
  std::string name;
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
  RGTextureState state;
};

struct RGTextureWriteInfo {
  std::string name;
  RGTextureID texture;
  RGTextureState state;
};

struct RGTextureCreateInfo {
  std::string name;
  REN_DEBUG_NAME_FIELD("RenderGraph Texture");
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
  RGTextureState state;
  bool preserve = false;
};

struct RGTextureImportInfo {
  std::string name;
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

using RGCallback =
    std::function<void(Device &device, RenderGraph &rg, CommandBuffer &cmd)>;

struct RGBatch {
  SmallVector<RGSemaphoreSignalInfo> wait_semaphores;
  SmallVector<RGSemaphoreSignalInfo> signal_semaphores;
  Vector<RGCallback> barrier_cbs;
  Vector<RGCallback> pass_cbs;
  Vector<std::string> pass_names;
};

class RenderGraph {
  Vector<RGBatch> m_batches;

  Vector<unsigned> m_buffers;
  Vector<BufferView> m_physical_buffers;
  Vector<RGBufferState> m_buffer_states;

  Vector<unsigned> m_textures;
  Vector<TextureView> m_physical_textures;
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
        m_buffers(std::move(config.buffers)),
        m_physical_buffers(std::move(config.physical_buffers)),
        m_buffer_states(std::move(config.buffer_states)),
        m_textures(std::move(config.textures)),
        m_physical_textures(std::move(config.physical_textures)),
        m_texture_states(std::move(config.texture_states)),
        m_swapchain(config.swapchain),
        m_present_semaphore(config.present_semaphore) {}

public:
  class Builder;

  auto get_buffer(RGBufferID buffer) const -> const BufferView &;

  auto get_texture(RGTextureID texture) const -> const TextureView &;

  auto export_buffer(RGBufferID buffer) const -> RGBufferExportInfo;

  auto export_texture(RGTextureID texture) const -> RGTextureExportInfo;

  void execute(Device &device, CommandAllocator &cmd_allocator);
};

struct RGTextureAccess {
  RGTextureID texture;
  RGTextureState state;
};

struct RGBufferAccess {
  RGBufferID buffer;
  RGBufferState state;
};

struct RGPass {
  SmallVector<RGTextureAccess> read_textures;
  SmallVector<RGTextureAccess> write_textures;
  SmallVector<RGBufferAccess> read_buffers;
  SmallVector<RGBufferAccess> write_buffers;
  SmallVector<RGSemaphoreSignalInfo> wait_semaphores;
  SmallVector<RGSemaphoreSignalInfo> signal_semaphores;
  RGCallback barrier_cb;
  RGCallback pass_cb;
};

class RenderGraph::Builder {
  Vector<RGPass> m_passes = {{}};
  Vector<std::string> m_pass_names = {{}};

  Vector<unsigned> m_buffers = {{}};
  Vector<BufferView> m_physical_buffers;
  Vector<RGBufferState> m_buffer_states;
  HashMap<unsigned, BufferCreateInfo> m_buffer_create_infos;
  HashSet<unsigned> m_preserved_buffers;
  HashMap<RGBufferID, RGPassID> m_buffer_defs;
  HashMap<RGBufferID, RGPassID> m_buffer_kills;

  HashMap<RGBufferID, RGBufferID> m_buffer_parents;
  Vector<std::string> m_buffer_names = {{}};

  Vector<unsigned> m_textures = {{}};
  Vector<TextureView> m_physical_textures;
  Vector<RGTextureState> m_texture_states;
  HashMap<unsigned, TextureCreateInfo> m_texture_create_infos;
  HashSet<unsigned> m_preserved_textures;
  HashMap<RGTextureID, RGPassID> m_texture_defs;
  HashMap<RGTextureID, RGPassID> m_texture_kills;

  HashMap<RGTextureID, RGTextureID> m_texture_parents;
  Vector<std::string> m_texture_names = {{}};

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

private:
  [[nodiscard]] auto init_new_pass(std::string name) -> RGPassID;
  RGPassID getPassID(const RGPass &pass) const;

private:
  [[nodiscard]] auto init_new_texture(Optional<RGPassID> pass,
                                      Optional<RGTextureID> from_texture,
                                      std::string name) -> RGTextureID;

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
                                     std::string name) -> RGBufferID;

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

  void print_resources() const;

  auto schedule_passes() -> Vector<RGPassID>;

  void print_passes(std::span<const RGPassID> passes) const;

  void insert_barriers(Device &device);

  auto batch_passes(std::span<const RGPassID> schedule) -> Vector<RGBatch>;

public:
  class PassBuilder;
  [[nodiscard]] PassBuilder create_pass(std::string name);

  void present(Swapchain &swapchain, RGTextureID texture,
               Handle<Semaphore> acquire_semaphore,
               Handle<Semaphore> present_semaphore);

  [[nodiscard]] RenderGraph build(Device &device, ResourceArena &frame_arena,
                                  ResourceArena &next_frame_arena);
};

class RenderGraph::Builder::PassBuilder {
  RGPassID m_pass;
  Builder *m_builder;

public:
  PassBuilder(RGPassID pass, Builder *builder)
      : m_pass(pass), m_builder(builder) {}

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

} // namespace ren
