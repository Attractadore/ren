#pragma once
#include "Buffer.hpp"
#include "Semaphore.hpp"
#include "Support/HashMap.hpp"
#include "Support/Optional.hpp"
#include "Support/SlotMap.hpp"
#include "Texture.hpp"

#include <functional>

namespace ren {

class CommandAllocator;
class CommandBuffer;
class Device;
class RenderGraph;
class Swapchain;

enum class RGNodeID;
enum class RGTextureID;
enum class RGBufferID;
enum RGSemaphoreID {};

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
protected:
  struct Batch {
    SmallVector<VkSemaphoreSubmitInfo, 2> wait_semaphores;
    SmallVector<VkSemaphoreSubmitInfo, 2> signal_semaphores;
    SmallVector<RGCallback, 16> barrier_cbs;
    SmallVector<RGCallback, 16> pass_cbs;
  };

protected:
  Device *m_device;
  Vector<Batch> m_batches;

  Vector<Texture> m_textures;
  HashMap<RGTextureID, unsigned> m_phys_textures;
  Vector<Buffer> m_buffers;
  HashMap<RGBufferID, unsigned> m_physical_buffers;
  Vector<VkSemaphore> m_semaphores;

  Swapchain *m_swapchain;
  RGTextureID m_swapchain_image;
  RGSemaphoreID m_acquire_semaphore;
  RGSemaphoreID m_present_semaphore;

protected:
  struct Config {
    Device *device;
    Vector<Batch> batches;
    Vector<Texture> textures;
    HashMap<RGTextureID, unsigned> phys_textures;
    Vector<Buffer> buffers;
    HashMap<RGBufferID, unsigned> physical_buffers;
    unsigned num_semaphores;
    Swapchain *swapchain;
    RGTextureID swapchain_image;
    RGSemaphoreID acquire_semaphore;
    RGSemaphoreID present_semaphore;
  };

  RenderGraph(Config config)
      : m_device(config.device), m_batches(std::move(config.batches)),
        m_textures(std::move(config.textures)),
        m_phys_textures(std::move(config.phys_textures)),
        m_buffers(std::move(config.buffers)),
        m_physical_buffers(std::move(config.physical_buffers)),
        m_semaphores(config.num_semaphores), m_swapchain(config.swapchain),
        m_swapchain_image(config.swapchain_image),
        m_acquire_semaphore(config.acquire_semaphore),
        m_present_semaphore(config.present_semaphore) {}

public:
  class Builder;

  void setTexture(RGTextureID id, Texture tex);
  const Texture &getTexture(RGTextureID tex) const;

  void set_buffer(RGBufferID id, Buffer buffer);
  auto get_buffer(RGBufferID buffer) const -> const Buffer &;

  void set_semaphore(RGSemaphoreID id, VkSemaphore semaphore);
  auto get_semaphore(RGSemaphoreID semaphore) const -> VkSemaphore;

  void execute(CommandAllocator &cmd_allocator);
};

class RenderGraph::Builder {
protected:
  struct TextureAccess {
    RGTextureID texture;
    VkAccessFlags2 accesses;
    VkPipelineStageFlags2 stages;
  };

  struct BufferAccess {
    RGBufferID buffer;
    VkAccessFlags2 accesses;
    VkPipelineStageFlags2 stages;
  };

  struct RGNode {
    SmallVector<TextureAccess> read_textures;
    SmallVector<TextureAccess> write_textures;
    SmallVector<BufferAccess> read_buffers;
    SmallVector<BufferAccess> write_buffers;
    SmallVector<VkSemaphoreSubmitInfo, 2> wait_semaphores;
    SmallVector<VkSemaphoreSubmitInfo, 2> signal_semaphores;
    RGCallback barrier_cb;
    RGCallback pass_cb;
  };

protected:
  Device *m_device;

  Vector<RGNode> m_nodes;

  using TextureMap = SlotMap<RGNodeID>;
  using VTextureKey = TextureMap::key_type;
  TextureMap m_texture_defs;
  Vector<RGTextureDesc> m_texture_descs;
  HashMap<RGTextureID, RGNodeID> m_texture_kills;
  HashMap<RGTextureID, unsigned> m_phys_textures;

  using BufferMap = SlotMap<RGNodeID>;
  using VBufferKey = BufferMap::key_type;
  BufferMap m_buffer_defs;
  Vector<RGBufferDesc> m_buffer_descs;
  HashMap<RGBufferID, RGNodeID> m_buffer_kills;
  HashMap<RGBufferID, unsigned> m_physical_buffers;

  unsigned m_num_semaphores = 0;

  Swapchain *m_swapchain = nullptr;
  RGTextureID m_swapchain_image;
  RGSemaphoreID m_acquire_semaphore;
  RGSemaphoreID m_present_semaphore;
  RGTextureID m_final_image;

  HashMap<RGNodeID, std::string> m_node_text_descs;
  HashMap<RGTextureID, std::string> m_tex_text_descs;
  HashMap<RGBufferID, std::string> m_buffer_text_descs;
  HashMap<RGSemaphoreID, std::string> m_semaphore_text_descs;

protected:
  RGNodeID createNode();
  RGNode &getNode(RGNodeID node);
  RGNodeID getNodeID(const RGNode &node) const;

  unsigned createPhysicalTexture(const RGTextureDesc &desc);
  RGTextureID createVirtualTexture(unsigned tex, RGNodeID node);

  std::pair<unsigned, RGTextureID> createTexture(RGNodeID node);
  RGNodeID getTextureDef(RGTextureID tex) const;

  Optional<RGNodeID> getTextureKill(RGTextureID tex) const;

  bool isExternalTexture(unsigned tex) const;

  unsigned getPhysTextureCount() const;

  auto create_physical_buffer(const RGBufferDesc &desc) -> unsigned;
  auto create_virtual_buffer(unsigned buffer, RGNodeID node) -> RGBufferID;
  auto create_buffer(RGNodeID node) -> std::pair<unsigned, RGBufferID>;

  auto get_buffer_def(RGBufferID buffer) const -> RGNodeID;
  auto get_buffer_kill(RGBufferID buffer) const -> Optional<RGNodeID>;

  auto get_physical_buffer_count() const -> unsigned;

  void addReadInput(RGNodeID node, RGTextureID texture, VkAccessFlags2 accesses,
                    VkPipelineStageFlags2 stages);

  [[nodiscard]] RGTextureID addWriteInput(RGNodeID node, RGTextureID texture,
                                          VkAccessFlags2 accesses,
                                          VkPipelineStageFlags2 stages);

  [[nodiscard]] RGTextureID addOutput(RGNodeID node, const RGTextureDesc &desc,
                                      VkAccessFlags2 accesses,
                                      VkPipelineStageFlags2 stages);

  [[nodiscard]] auto add_output(RGNodeID node, const RGBufferDesc &desc,
                                VkAccessFlags2 accesses,
                                VkPipelineStageFlags2 stages) -> RGBufferID;

  [[nodiscard]] RGTextureID
  addExternalTextureOutput(RGNodeID node, VkAccessFlags2 accesses,
                           VkPipelineStageFlags2 stages);

  [[nodiscard]] auto create_semaphore() -> RGSemaphoreID;
  void wait_semaphore(RGNodeID node, RGSemaphoreID semaphore, uint64_t value,
                      VkPipelineStageFlags2 stages);
  void signal_semaphore(RGNodeID node, RGSemaphoreID semaphore, uint64_t value,
                        VkPipelineStageFlags2 stages);

  void setCallback(RGNodeID node, RGCallback cb);

  void setDesc(RGNodeID node, std::string desc);
  std::string_view getDesc(RGNodeID node) const;

protected:
  void addPresentNodes();

  Vector<RGNode> schedulePasses();

  auto derive_resource_usage_flags(std::span<const RGNode> scheduled_passes)
      -> std::pair<Vector<VkImageUsageFlags>, Vector<VkBufferUsageFlags>>;

  Vector<Texture>
  createTextures(std::span<const VkImageUsageFlags> texture_usage_flags);

  auto create_buffers(std::span<const VkBufferUsageFlags> buffer_usage_flags)
      -> Vector<Buffer>;

  struct BarrierConfig {
    RGTextureID texture;
    VkAccessFlags2 src_accesses;
    VkPipelineStageFlags2 src_stages;
    VkAccessFlags2 dst_accesses;
    VkPipelineStageFlags2 dst_stages;
  };

  RGCallback generateBarrierGroup(std::span<const BarrierConfig> configs);
  void generateBarriers(std::span<RGNode> scheduled_passes);

  Vector<Batch> batchPasses(auto scheduled_passes);

public:
  Builder(Device &device) : m_device(&device) {}

  class NodeBuilder;
  [[nodiscard]] NodeBuilder addNode();
  void present(Swapchain *swapchain, RGTextureID texture);

  void setDesc(RGTextureID, std::string desc);
  std::string_view getDesc(RGTextureID tex) const;

  void set_desc(RGBufferID buffer, std::string desc);
  std::string_view get_desc(RGBufferID buffer) const;

  void set_desc(RGSemaphoreID semaphore, std::string desc);
  std::string_view get_desc(RGSemaphoreID semaphore) const;

  [[nodiscard]] RenderGraph build();
};

class RenderGraph::Builder::NodeBuilder {
  RGNodeID m_node;
  Builder *m_builder;

public:
  NodeBuilder(RGNodeID node, Builder *builder)
      : m_node(node), m_builder(builder) {}

  void addReadInput(RGTextureID texture, VkAccessFlags2 accesses,
                    VkPipelineStageFlags2 stages) {
    m_builder->addReadInput(m_node, texture, accesses, stages);
  }

  [[nodiscard]] RGTextureID addWriteInput(RGTextureID texture,
                                          VkAccessFlags2 accesses,
                                          VkPipelineStageFlags2 stages) {
    return m_builder->addWriteInput(m_node, texture, accesses, stages);
  }

  [[nodiscard]] RGTextureID addOutput(const RGTextureDesc &desc,
                                      VkAccessFlags2 accesses,
                                      VkPipelineStageFlags2 stages) {
    return m_builder->addOutput(m_node, desc, accesses, stages);
  }

  [[nodiscard]] auto add_output(const RGBufferDesc &desc,
                                VkAccessFlags2 accesses,
                                VkPipelineStageFlags2 stages) -> RGBufferID {
    return m_builder->add_output(m_node, desc, accesses, stages);
  }

  void wait_semaphore(RGSemaphoreID semaphore, uint64_t value,
                      VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_node, semaphore, value, stages);
  }

  void wait_semaphore(RGSemaphoreID semaphore, VkPipelineStageFlags2 stages) {
    m_builder->wait_semaphore(m_node, semaphore, 0, stages);
  }

  void signal_semaphore(RGSemaphoreID semaphore, uint64_t value,
                        VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_node, semaphore, value, stages);
  }

  void signal_semaphore(RGSemaphoreID semaphore, VkPipelineStageFlags2 stages) {
    m_builder->signal_semaphore(m_node, semaphore, 0, stages);
  }

  [[nodiscard]] RGTextureID
  addExternalTextureOutput(VkAccessFlags2 accesses,
                           VkPipelineStageFlags2 stages) {
    return m_builder->addExternalTextureOutput(m_node, accesses, stages);
  }

  void setCallback(RGCallback cb) {
    return m_builder->setCallback(m_node, std::move(cb));
  }

  void setDesc(std::string desc) {
    return m_builder->setDesc(m_node, std::move(desc));
  }
};

} // namespace ren
