#pragma once
#include "Def.hpp"
#include "PipelineStages.hpp"
#include "Support/HashMap.hpp"
#include "Support/SlotMap.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

#include <functional>
#include <optional>

namespace ren {
class CommandAllocator;
class CommandBuffer;
class RenderGraph;

enum class RGNodeID;
enum class RGTextureID;
enum class RGSyncID;

struct RGTextureDesc {
  TextureType type = TextureType::e2D;
  Format format;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned depth = 1;
    unsigned layers;
  };
  unsigned levels = 1;
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
  Vector<SyncObject> m_syncs;

public:
  class Builder;
  RenderGraph(Swapchain *swapchain, Vector<Batch> batches,
              Vector<Texture> textures,
              HashMap<RGTextureID, unsigned> phys_textures,
              Vector<SyncObject> syncs)
      : m_swapchain(swapchain), m_batches(std::move(batches)),
        m_textures(std::move(textures)),
        m_phys_textures(std::move(phys_textures)), m_syncs(std::move(syncs)) {}
  virtual ~RenderGraph() = default;

  void setTexture(RGTextureID id, Texture tex);
  const Texture &getTexture(RGTextureID tex) const;

  void setSyncObject(RGSyncID id, SyncObject sync);
  const SyncObject &getSyncObject(RGSyncID sync) const;

  virtual void execute(CommandAllocator *cmd_pool) = 0;
};

class RenderGraph::Builder {
protected:
  struct TextureAccess {
    RGTextureID texture;
    MemoryAccessFlags accesses;
    PipelineStageFlags stages;
  };

  struct SyncAccess {
    RGSyncID sync;
  };

  struct RGNode {
    SmallVector<TextureAccess> read_textures;
    SmallVector<TextureAccess> write_textures;
    SmallVector<SyncAccess> wait_syncs;
    SmallVector<SyncAccess> signal_syncs;
    RGCallback barrier_cb;
    RGCallback pass_cb;
  };

protected:
  Device *m_device;

  Vector<RGNode> m_nodes;

  SlotMap<RGNodeID> m_texture_defs;
  using VTextureKey = decltype(m_texture_defs)::key_type;
  Vector<RGTextureDesc> m_texture_descs;
  HashMap<RGTextureID, RGNodeID> m_texture_kills;
  HashMap<RGTextureID, unsigned> m_phys_textures;

  Vector<RGNodeID> m_sync_defs;
  Vector<RGSyncDesc> m_sync_descs;

  Swapchain *m_swapchain = nullptr;
  RGTextureID m_final_image;

  HashMap<RGNodeID, std::string> m_node_text_descs;
  HashMap<RGTextureID, std::string> m_tex_text_descs;
  HashMap<RGSyncID, std::string> m_sync_text_descs;

protected:
  RGNodeID createNode();
  RGNode &getNode(RGNodeID node);
  RGNodeID getNodeID(const RGNode &node) const;

  unsigned createPhysicalTexture(const RGTextureDesc &desc);
  RGTextureID createVirtualTexture(unsigned tex, RGNodeID node);

  std::pair<unsigned, RGTextureID> createTexture(RGNodeID node);
  RGNodeID getTextureDef(RGTextureID tex) const;

  std::optional<RGNodeID> getTextureKill(RGTextureID tex) const;

  bool isExternalTexture(unsigned tex) const;

  unsigned getPhysTextureCount() const;

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

  Vector<TextureUsageFlags>
  deriveTextureUsageFlags(std::span<const RGNode> scheduled_passes);

  Vector<Texture>
  createTextures(std::span<const TextureUsageFlags> texture_usage_flags);

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

  virtual std::unique_ptr<RenderGraph>
  createRenderGraph(Vector<Batch> batches, Vector<Texture> textures,
                    HashMap<RGTextureID, unsigned> phys_textures,
                    Vector<SyncObject> syncs) = 0;

public:
  Builder(Device *device) : m_device(device) {}
  virtual ~Builder() = default;

  class NodeBuilder;
  [[nodiscard]] NodeBuilder addNode();
  void setSwapchain(Swapchain *swapchain);
  void setFinalImage(RGTextureID texture);

  void setDesc(RGTextureID, std::string desc);
  std::string_view getDesc(RGTextureID tex) const;

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
