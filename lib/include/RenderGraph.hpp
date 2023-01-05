#pragma once
#include "Buffer.hpp"
#include "Def.hpp"
#include "PipelineStages.hpp"
#include "Support/HashMap.hpp"
#include "Support/Optional.hpp"
#include "Support/SlotMap.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

#include <functional>

namespace ren {
class CommandAllocator;
class CommandBuffer;
class RenderGraph;

enum class RGNodeID;
enum class RGTextureID;
enum class RGBufferID;
enum class RGSyncID;

struct RGTextureDesc {
  TextureType type = TextureType::e2D;
  Format format;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned short depth = 1;
    unsigned short layers;
  };
  unsigned short levels = 1;
};

struct RGBufferDesc {
  BufferLocation location;
  unsigned size;
};

struct RGSyncDesc {
  SyncType type;
};

using RGCallback = std::function<void(CommandBuffer &cmd, RenderGraph &rg)>;

class RenderGraph {
protected:
  struct Batch {
    SmallVector<RGCallback, 16> barrier_cbs;
    SmallVector<RGCallback, 16> pass_cbs;
  };

protected:
  Swapchain *m_swapchain;
  Vector<Batch> m_batches;

  Vector<Texture> m_textures;
  HashMap<RGTextureID, unsigned> m_phys_textures;
  Vector<Buffer> m_buffers;
  HashMap<RGBufferID, unsigned> m_physical_buffers;
  Vector<SyncObject> m_syncs;

protected:
  struct Config {
    Swapchain *swapchain;
    Vector<Batch> batches;
    Vector<Texture> textures;
    HashMap<RGTextureID, unsigned> phys_textures;
    Vector<Buffer> buffers;
    HashMap<RGBufferID, unsigned> physical_buffers;
    Vector<SyncObject> syncs;
  };

  RenderGraph(Config config)
      : m_swapchain(config.swapchain), m_batches(std::move(config.batches)),
        m_textures(std::move(config.textures)),
        m_phys_textures(std::move(config.phys_textures)),
        m_buffers(std::move(config.buffers)),
        m_physical_buffers(std::move(config.physical_buffers)),
        m_syncs(std::move(config.syncs)) {}

public:
  class Builder;
  virtual ~RenderGraph() = default;

  void setTexture(RGTextureID id, Texture tex);
  const Texture &getTexture(RGTextureID tex) const;

  void set_buffer(RGBufferID id, Buffer buffer);
  auto get_buffer(RGBufferID buffer) const -> const Buffer &;

  void setSyncObject(RGSyncID id, SyncObject sync);
  const SyncObject &getSyncObject(RGSyncID sync) const;

  virtual void execute() = 0;
};

class RenderGraph::Builder {
protected:
  struct TextureAccess {
    RGTextureID texture;
    MemoryAccessFlags accesses;
    PipelineStageFlags stages;
  };

  struct BufferAccess {
    RGBufferID buffer;
    MemoryAccessFlags accesses;
    PipelineStageFlags stages;
  };

  struct SyncAccess {
    RGSyncID sync;
  };

  struct RGNode {
    SmallVector<TextureAccess> read_textures;
    SmallVector<TextureAccess> write_textures;
    SmallVector<BufferAccess> read_buffers;
    SmallVector<BufferAccess> write_buffers;
    SmallVector<SyncAccess> wait_syncs;
    SmallVector<SyncAccess> signal_syncs;
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

  Vector<RGNodeID> m_sync_defs;
  Vector<RGSyncDesc> m_sync_descs;

  Swapchain *m_swapchain = nullptr;
  RGTextureID m_final_image;

  HashMap<RGNodeID, std::string> m_node_text_descs;
  HashMap<RGTextureID, std::string> m_tex_text_descs;
  HashMap<RGBufferID, std::string> m_buffer_text_descs;
  HashMap<RGSyncID, std::string> m_sync_text_descs;

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

  RGSyncID createSync(RGNodeID node);
  RGNodeID getSyncDef(RGSyncID sync) const;

  bool isExternalSync(unsigned sync) const;

  unsigned getSyncObjectCount() const;

  void addReadInput(RGNodeID node, RGTextureID texture,
                    MemoryAccessFlags accesses, PipelineStageFlags stages);

  [[nodiscard]] RGTextureID addWriteInput(RGNodeID node, RGTextureID texture,
                                          MemoryAccessFlags accesses,
                                          PipelineStageFlags stages);

  [[nodiscard]] RGTextureID addOutput(RGNodeID node, const RGTextureDesc &desc,
                                      MemoryAccessFlags accesses,
                                      PipelineStageFlags stages);

  [[nodiscard]] auto add_output(RGNodeID node, const RGBufferDesc &desc,
                                MemoryAccessFlags accesses,
                                PipelineStageFlags stages) -> RGBufferID;

  [[nodiscard]] RGTextureID addExternalTextureOutput(RGNodeID node,
                                                     MemoryAccessFlags accesses,
                                                     PipelineStageFlags stages);

  void addWaitSync(RGNodeID node, RGSyncID sync);

  [[nodiscard]] RGSyncID addSignalSync(RGNodeID node, const RGSyncDesc &desc);

  [[nodiscard]] RGSyncID addExternalSignalSync(RGNodeID node);

  void setCallback(RGNodeID node, RGCallback cb);

  void setDesc(RGNodeID node, std::string desc);
  std::string_view getDesc(RGNodeID node) const;

protected:
  virtual void addPresentNodes() = 0;

  Vector<RGNode> schedulePasses();

  auto derive_resource_usage_flags(std::span<const RGNode> scheduled_passes)
      -> std::pair<Vector<TextureUsageFlags>, Vector<BufferUsageFlags>>;

  Vector<Texture>
  createTextures(std::span<const TextureUsageFlags> texture_usage_flags);

  auto create_buffers(std::span<const BufferUsageFlags> buffer_usage_flags)
      -> Vector<Buffer>;

  Vector<SyncObject> createSyncObjects();

  struct BarrierConfig {
    RGTextureID texture;
    MemoryAccessFlags src_accesses;
    PipelineStageFlags src_stages;
    MemoryAccessFlags dst_accesses;
    PipelineStageFlags dst_stages;
  };

  virtual RGCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) = 0;
  void generateBarriers(std::span<RGNode> scheduled_passes);

  Vector<Batch> batchPasses(auto scheduled_passes);

  virtual auto create_render_graph(Config config)
      -> std::unique_ptr<RenderGraph> = 0;

public:
  Builder(Device *device) : m_device(device) {}
  virtual ~Builder() = default;

  class NodeBuilder;
  [[nodiscard]] NodeBuilder addNode();
  void setSwapchain(Swapchain *swapchain);
  void setFinalImage(RGTextureID texture);

  void setDesc(RGTextureID, std::string desc);
  std::string_view getDesc(RGTextureID tex) const;

  void set_desc(RGBufferID buffer, std::string desc);
  std::string_view get_desc(RGBufferID buffer) const;

  void setDesc(RGSyncID, std::string desc);
  std::string_view getDesc(RGSyncID sync) const;

  [[nodiscard]] std::unique_ptr<RenderGraph> build();
};

class RenderGraph::Builder::NodeBuilder {
  RGNodeID m_node;
  Builder *m_builder;

public:
  NodeBuilder(RGNodeID node, Builder *builder)
      : m_node(node), m_builder(builder) {}

  void addReadInput(RGTextureID texture, MemoryAccessFlags accesses,
                    PipelineStageFlags stages) {
    m_builder->addReadInput(m_node, texture, accesses, stages);
  }

  [[nodiscard]] RGTextureID addWriteInput(RGTextureID texture,
                                          MemoryAccessFlags accesses,
                                          PipelineStageFlags stages) {
    return m_builder->addWriteInput(m_node, texture, accesses, stages);
  }

  [[nodiscard]] RGTextureID addOutput(const RGTextureDesc &desc,
                                      MemoryAccessFlags accesses,
                                      PipelineStageFlags stages) {
    return m_builder->addOutput(m_node, desc, accesses, stages);
  }

  [[nodiscard]] auto add_output(const RGBufferDesc &desc,
                                MemoryAccessFlags accesses,
                                PipelineStageFlags stages) -> RGBufferID {
    return m_builder->add_output(m_node, desc, accesses, stages);
  }

  [[nodiscard]] RGTextureID
  addExternalTextureOutput(MemoryAccessFlags accesses,
                           PipelineStageFlags stages) {
    return m_builder->addExternalTextureOutput(m_node, accesses, stages);
  }

  void addWaitSync(RGSyncID sync) {
    return m_builder->addWaitSync(m_node, sync);
  }

  [[nodiscard]] RGSyncID addSignalSync(const RGSyncDesc &desc) {
    return m_builder->addSignalSync(m_node, desc);
  }

  [[nodiscard]] RGSyncID addExternalSignalSync() {
    return m_builder->addExternalSignalSync(m_node);
  }

  void setCallback(RGCallback cb) {
    return m_builder->setCallback(m_node, std::move(cb));
  }

  void setDesc(std::string desc) {
    return m_builder->setDesc(m_node, std::move(desc));
  }
};
} // namespace ren
