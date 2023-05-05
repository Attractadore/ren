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
class Swapchain;

REN_NEW_TYPE(RGPassID, unsigned);
REN_NEW_TYPE(RGTextureID, unsigned);
REN_NEW_TYPE(RGBufferID, unsigned);

struct RGTextureDesc {
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned short depth = 1;
    unsigned short array_layers;
  };
  unsigned short mip_levels = 1;
};

struct RGBufferDesc {
  BufferHeap heap;
  unsigned size;
};

using RGCallback = std::function<void(CommandBuffer &cmd, RenderGraph &rg)>;

class RenderGraph {
  struct Batch {
    Vector<VkSemaphoreSubmitInfo> wait_semaphores;
    Vector<VkSemaphoreSubmitInfo> signal_semaphores;
    Vector<RGCallback> barrier_cbs;
    Vector<RGCallback> pass_cbs;
  };

  Device *m_device = nullptr;
  DescriptorSetAllocator *m_set_allocator = nullptr;

  Vector<Batch> m_batches;

  Vector<Texture> m_textures;
  Vector<Buffer> m_buffers;
  Vector<Semaphore> m_semaphores;

  Swapchain *m_swapchain = nullptr;
  SemaphoreRef m_present_semaphore;

private:
  struct Config {
    Vector<Batch> batches;
    Vector<Texture> textures;
    Vector<Buffer> buffers;
    Vector<Semaphore> semaphores;
    Swapchain *swapchain;
    SemaphoreRef present_semaphore;
  };

  RenderGraph(Config config)
      : m_batches(std::move(config.batches)),
        m_textures(std::move(config.textures)),
        m_semaphores(std::move(config.semaphores)),
        m_buffers(std::move(config.buffers)), m_swapchain(config.swapchain),
        m_present_semaphore(config.present_semaphore) {}

public:
  class Builder;

  [[nodiscard]] auto allocate_descriptor_set(DescriptorSetLayoutRef layout)
      -> DescriptorSetWriter;

  auto get_texture(RGTextureID tex) const -> TextureRef;

  auto get_buffer(RGBufferID buffer) const -> BufferRef;

  void execute(Device &device, DescriptorSetAllocator &set_allocator,
               CommandAllocator &cmd_allocator);
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
    SmallVector<VkSemaphoreSubmitInfo> wait_semaphores;
    SmallVector<VkSemaphoreSubmitInfo> signal_semaphores;
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

  Vector<Texture> m_textures = {{}};
  Vector<TextureState> m_texture_states = {{}};
  HashMap<RGTextureID, RGPassID> m_texture_defs;
  HashMap<RGTextureID, RGPassID> m_texture_kills;
  Vector<unsigned> m_physical_textures = {{}};
  HashMap<RGTextureID, RGTextureDesc> m_texture_descs;
  Vector<VkImageUsageFlags> m_texture_usage_flags = {{}};

  struct BufferState {
    VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  };

  Vector<Buffer> m_buffers = {{}};
  Vector<BufferState> m_buffer_states = {{}};
  HashMap<RGBufferID, RGPassID> m_buffer_defs;
  HashMap<RGBufferID, RGPassID> m_buffer_kills;
  Vector<unsigned> m_physical_buffers = {{}};
  HashMap<RGBufferID, RGBufferDesc> m_buffer_descs;
  Vector<VkBufferUsageFlags> m_buffer_usage_flags = {{}};

  Vector<Semaphore> m_semaphores;

  Swapchain *m_swapchain = nullptr;
  SemaphoreRef m_present_semaphore;

  HashMap<RGPassID, std::string> m_pass_text_descs;
  HashMap<RGTextureID, std::string> m_tex_text_descs;
  HashMap<RGBufferID, std::string> m_buffer_text_descs;

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

  [[nodiscard]] auto create_texture(RGPassID pass, const RGTextureDesc &desc,
                                    VkAccessFlags2 accesses,
                                    VkPipelineStageFlags2 stages,
                                    VkImageLayout layout) -> RGTextureID;

public:
  [[nodiscard]] auto import_texture(Texture texture, VkAccessFlags2 accesses,
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

  [[nodiscard]] auto create_buffer(RGPassID pass, const RGBufferDesc &desc,
                                   VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID;

public:
  [[nodiscard]] auto import_buffer(Buffer buffer, VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID;

private:
  void wait_semaphore(RGPassID pass, Semaphore semaphore, uint64_t value,
                      VkPipelineStageFlags2 stages);
  void signal_semaphore(RGPassID pass, Semaphore semaphore, uint64_t value,
                        VkPipelineStageFlags2 stages);

  void set_callback(RGPassID pass, RGCallback cb);

  void set_desc(RGPassID pass, std::string desc);
  auto get_desc(RGPassID pass) const -> std::string_view;

private:
  void create_textures(Device &device);

  void create_buffers(Device &device);

  void schedule_passes();

  void insert_barriers();

  void batch_passes();

public:
  class PassBuilder;
  [[nodiscard]] PassBuilder create_pass();

  void present(Swapchain &swapchain, RGTextureID texture,
               Semaphore present_semaphore, Semaphore acquire_semaphore);

  void set_desc(RGTextureID, std::string desc);
  auto get_desc(RGTextureID tex) const -> std::string_view;

  void set_desc(RGBufferID buffer, std::string desc);
  auto get_desc(RGBufferID buffer) const -> std::string_view;

  [[nodiscard]] RenderGraph build(Device &device);
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

  [[nodiscard]] RGTextureID create_texture(const RGTextureDesc &desc,
                                           VkAccessFlags2 accesses,
                                           VkPipelineStageFlags2 stages,
                                           VkImageLayout layout) {
    return m_builder->create_texture(m_pass, desc, accesses, stages, layout);
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

  [[nodiscard]] auto create_buffer(const RGBufferDesc &desc,
                                   VkAccessFlags2 accesses,
                                   VkPipelineStageFlags2 stages) -> RGBufferID {
    return m_builder->create_buffer(m_pass, desc, accesses, stages);
  }

  void wait_semaphore(Semaphore semaphore, uint64_t value,
                      VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_pass, std::move(semaphore), value, stages);
  }

  void wait_semaphore(Semaphore semaphore, VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_pass, std::move(semaphore), 0, stages);
  }

  void signal_semaphore(Semaphore semaphore, uint64_t value,
                        VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_pass, std::move(semaphore), value, stages);
  }

  void signal_semaphore(Semaphore semaphore, VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_pass, std::move(semaphore), 0, stages);
  }

  void set_callback(RGCallback cb) {
    return m_builder->set_callback(m_pass, std::move(cb));
  }

  void set_desc(std::string desc) {
    return m_builder->set_desc(m_pass, std::move(desc));
  }
};

} // namespace ren
