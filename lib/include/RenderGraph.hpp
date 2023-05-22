#pragma once
#include "Buffer.hpp"
#include "Descriptors.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/NewType.hpp"
#include "Support/Optional.hpp"
#include "Texture.hpp"

#include <functional>

namespace ren {

class CommandAllocator;
class CommandBuffer;
class DescriptorSetAllocator;
class Device;
class RenderGraph;
class ResourceArena;
class Swapchain;

REN_NEW_TYPE(RGPassID, unsigned);
REN_NEW_TYPE(RGTextureID, unsigned);
REN_NEW_TYPE(RGBufferID, unsigned);

struct RGTextureCreateInfo {
  REN_DEBUG_NAME_FIELD("RenderGraph Texture");
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned short depth = 1;
    unsigned short array_layers;
  };
  unsigned short mip_levels = 1;
};

struct RGBufferCreateInfo {
  REN_DEBUG_NAME_FIELD("RenderGraph Buffer");
  BufferHeap heap = BufferHeap::Upload;
  size_t size = 0;
};

using RGCallback =
    std::function<void(Device &device, RenderGraph &rg, CommandBuffer &cmd)>;

class RenderGraph {
  struct SemaphoreSignal {
    Handle<Semaphore> semaphore;
    u64 value;
    VkPipelineStageFlags2 stages;
  };

  struct Batch {
    SmallVector<SemaphoreSignal> wait_semaphores;
    SmallVector<SemaphoreSignal> signal_semaphores;
    Vector<RGCallback> barrier_cbs;
    Vector<RGCallback> pass_cbs;
  };

  Vector<Batch> m_batches;

  Vector<TextureView> m_textures;
  Vector<BufferView> m_buffers;

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

private:
  struct Config {
    Vector<Batch> batches;
    Vector<TextureView> textures;
    Vector<BufferView> buffers;
    Swapchain *swapchain;
    Handle<Semaphore> present_semaphore;
  };

  RenderGraph(Config config)
      : m_batches(std::move(config.batches)),
        m_textures(std::move(config.textures)),
        m_buffers(std::move(config.buffers)), m_swapchain(config.swapchain),
        m_present_semaphore(config.present_semaphore) {}

public:
  class Builder;

  auto get_texture(RGTextureID texture) const -> TextureView;

  auto get_buffer(RGBufferID buffer) const -> BufferView;

  void execute(Device &device, CommandAllocator &cmd_allocator);
};

class RenderGraph::Builder {
  struct TextureAccess {
    RGTextureID texture;
    VkAccessFlags2 accesses;
    VkPipelineStageFlags2 stages;
    VkImageLayout layout;
  };

  struct BufferAccess {
    RGBufferID buffer;
    VkAccessFlags2 accesses;
    VkPipelineStageFlags2 stages;
  };

  struct Pass {
    SmallVector<TextureAccess> read_textures;
    SmallVector<TextureAccess> write_textures;
    SmallVector<BufferAccess> read_buffers;
    SmallVector<BufferAccess> write_buffers;
    SmallVector<SemaphoreSignal> wait_semaphores;
    SmallVector<SemaphoreSignal> signal_semaphores;
    RGCallback barrier_cb;
    RGCallback pass_cb;
  };

  Vector<Pass> m_passes = {{}};
  Vector<Batch> m_batches;

  struct TextureState {
    VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  Vector<TextureView> m_textures = {{}};
  Vector<TextureState> m_texture_states = {{}};
  HashMap<RGTextureID, RGPassID> m_texture_defs;
  HashMap<RGTextureID, RGPassID> m_texture_kills;
  Vector<unsigned> m_physical_textures = {{}};
  HashMap<RGTextureID, RGTextureCreateInfo> m_texture_create_infos;
  Vector<VkImageUsageFlags> m_texture_usage_flags = {{}};

  struct BufferState {
    VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  };

  Vector<BufferView> m_buffers = {{}};
  Vector<BufferState> m_buffer_states = {{}};
  HashMap<RGBufferID, RGPassID> m_buffer_defs;
  HashMap<RGBufferID, RGPassID> m_buffer_kills;
  Vector<unsigned> m_physical_buffers = {{}};
  HashMap<RGBufferID, RGBufferCreateInfo> m_buffer_create_infos;
  Vector<VkBufferUsageFlags> m_buffer_usage_flags = {{}};

  Swapchain *m_swapchain = nullptr;
  Handle<Semaphore> m_present_semaphore;

  HashMap<RGPassID, std::string> m_pass_text_descs;

private:
  [[nodiscard]] auto init_new_pass() -> RGPassID;
  RGPassID getPassID(const Pass &pass) const;

private:
  [[nodiscard]] auto init_new_texture(Optional<RGPassID> pass,
                                      Optional<RGTextureID> from_texture)
      -> RGTextureID;

  auto get_texture_def(RGTextureID tex) const -> Optional<RGPassID>;
  auto get_texture_kill(RGTextureID tex) const -> Optional<RGPassID>;

  void add_read_texture(RGPassID pass, RGTextureID texture,
                        VkAccessFlags2 accesses, VkPipelineStageFlags2 stages,
                        VkImageLayout layout);

  [[nodiscard]] auto add_write_texture(RGPassID pass, RGTextureID texture,
                                       VkAccessFlags2 accesses,
                                       VkPipelineStageFlags2 stages,
                                       VkImageLayout layout) -> RGTextureID;

  [[nodiscard]] auto create_texture(RGPassID pass,
                                    RGTextureCreateInfo &&create_info,
                                    VkAccessFlags2 accesses,
                                    VkPipelineStageFlags2 stages,
                                    VkImageLayout layout) -> RGTextureID;

public:
  [[nodiscard]] auto import_texture(TextureView texture,
                                    VkAccessFlags2 accesses,
                                    VkPipelineStageFlags2 stages,
                                    VkImageLayout layout) -> RGTextureID;

private:
  [[nodiscard]] auto init_new_buffer(Optional<RGPassID> pass,
                                     Optional<RGBufferID> from_buffer)
      -> RGBufferID;

  auto get_buffer_def(RGBufferID tex) const -> Optional<RGPassID>;
  auto get_buffer_kill(RGBufferID tex) const -> Optional<RGPassID>;

  void add_read_buffer(RGPassID pass, RGBufferID buffer,
                       VkAccessFlags2 accesses, VkPipelineStageFlags2 stages);

  [[nodiscard]] auto add_write_buffer(RGPassID pass, RGBufferID buffer,
                                      VkAccessFlags2 accesses,
                                      VkPipelineStageFlags2 stages)
      -> RGBufferID;

  [[nodiscard]] auto create_buffer(RGPassID pass,
                                   RGBufferCreateInfo &&create_info,
                                   VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID;

public:
  [[nodiscard]] auto import_buffer(BufferView buffer, VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID;

private:
  void wait_semaphore(RGPassID pass, Handle<Semaphore> semaphore,
                      uint64_t value, VkPipelineStageFlags2 stages);
  void signal_semaphore(RGPassID pass, Handle<Semaphore> semaphore,
                        uint64_t value, VkPipelineStageFlags2 stages);

  void set_callback(RGPassID pass, RGCallback cb);

  void set_desc(RGPassID pass, std::string desc);
  auto get_desc(RGPassID pass) const -> std::string_view;

private:
  void create_textures(const Device &device, ResourceArena &arena);

  void create_buffers(const Device &device, ResourceArena &arena);

  void schedule_passes();

  void insert_barriers(Device &device);

  void batch_passes();

public:
  class PassBuilder;
  [[nodiscard]] PassBuilder create_pass();

  void present(Swapchain &swapchain, RGTextureID texture,
               Handle<Semaphore> acquire_semaphore,
               Handle<Semaphore> present_semaphore);

  [[nodiscard]] RenderGraph build(Device &device, ResourceArena &arena);
};

class RenderGraph::Builder::PassBuilder {
  RGPassID m_pass;
  Builder *m_builder;

public:
  PassBuilder(RGPassID pass, Builder *builder)
      : m_pass(pass), m_builder(builder) {}

  void read_texture(RGTextureID texture, VkAccessFlags2 accesses,
                    VkPipelineStageFlags2 stages, VkImageLayout layout) {
    m_builder->add_read_texture(m_pass, texture, accesses, stages, layout);
  }

  [[nodiscard]] RGTextureID write_texture(RGTextureID texture,
                                          VkAccessFlags2 accesses,
                                          VkPipelineStageFlags2 stages,
                                          VkImageLayout layout) {
    return m_builder->add_write_texture(m_pass, texture, accesses, stages,
                                        layout);
  }

  [[nodiscard]] RGTextureID create_texture(RGTextureCreateInfo &&create_info,
                                           VkAccessFlags2 accesses,
                                           VkPipelineStageFlags2 stages,
                                           VkImageLayout layout) {
    return m_builder->create_texture(m_pass, std::move(create_info), accesses,
                                     stages, layout);
  }

  void read_buffer(RGBufferID buffer, VkAccessFlags2 accesses,
                   VkPipelineStageFlags2 stages) {
    m_builder->add_read_buffer(m_pass, buffer, accesses, stages);
  }

  [[nodiscard]] auto add_write_buffer(RGBufferID buffer,
                                      VkAccessFlags2 accesses,
                                      VkPipelineStageFlags2 stages)
      -> RGBufferID {
    return m_builder->add_write_buffer(m_pass, buffer, accesses, stages);
  }

  [[nodiscard]] auto create_buffer(RGBufferCreateInfo &&create_info,
                                   VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID {
    return m_builder->create_buffer(m_pass, std::move(create_info), accesses,
                                    stages);
  }

  void wait_semaphore(Handle<Semaphore> semaphore, uint64_t value,
                      VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_pass, semaphore, value, stages);
  }

  void wait_semaphore(Handle<Semaphore> semaphore,
                      VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_pass, semaphore, 0, stages);
  }

  void signal_semaphore(Handle<Semaphore> semaphore, uint64_t value,
                        VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_pass, semaphore, value, stages);
  }

  void signal_semaphore(Handle<Semaphore> semaphore,
                        VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_pass, semaphore, 0, stages);
  }

  void set_callback(RGCallback cb) {
    return m_builder->set_callback(m_pass, std::move(cb));
  }

  void set_desc(std::string desc) {
    return m_builder->set_desc(m_pass, std::move(desc));
  }
};

} // namespace ren
