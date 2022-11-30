#pragma once
#include "Def.hpp"
#include "PipelineStages.hpp"
#include "Support/HashMap.hpp"
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

#include <functional>
#include <optional>

namespace ren {
enum class RGTextureID;
enum class RGSyncID;
enum class RGNodeID;

class RenderGraphBuilder;
class CommandAllocator;
class CommandBuffer;
class RGResources;

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

using PassCallback =
    std::function<void(CommandBuffer &cmd, RGResources &resources)>;

class RGNodeBuilder {
  friend class RenderGraphBuilder;

  RGNodeID m_node;
  RenderGraphBuilder *m_builder;

  RGNodeBuilder(RGNodeID node, RenderGraphBuilder *builder)
      : m_node(node), m_builder(builder) {}

public:
  void addReadInput(RGTextureID texture, MemoryAccessFlags accesses,
                    PipelineStageFlags stages);

  [[nodiscard]] RGTextureID addWriteInput(RGTextureID texture,
                                          MemoryAccessFlags accesses,
                                          PipelineStageFlags stages);

  [[nodiscard]] RGTextureID addOutput(const RGTextureDesc &desc,
                                      MemoryAccessFlags accesses,
                                      PipelineStageFlags stages);

  [[nodiscard]] RGTextureID addExternalTextureOutput(MemoryAccessFlags accesses,
                                                     PipelineStageFlags stages);

  void addWaitSync(RGSyncID sync);

  [[nodiscard]] RGSyncID addSignalSync(const RGSyncDesc &desc);

  [[nodiscard]] RGSyncID addExternalSignalSync();

  void setCallback(PassCallback cb);

  void setDesc(std::string desc);
};

class RenderGraphBuilder {
protected:
  struct TextureAccess {
    RGTextureID tex;
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
    PassCallback barrier_cb;
    PassCallback pass_cb;
  };

protected:
  Device *m_device;

  Vector<RGNode> m_nodes;

  HashMap<RGTextureID, RGNodeID> m_tex_defs;
  HashMap<RGTextureID, RGTextureDesc> m_tex_descs;
  HashMap<RGTextureID, RGNodeID> m_tex_redefs;
  HashMap<RGTextureID, RGTextureID> m_tex_aliases;

  HashMap<RGSyncID, RGNodeID> m_sync_defs;
  HashMap<RGSyncID, RGSyncDesc> m_sync_descs;

  Swapchain *m_swapchain = nullptr;
  RGTextureID m_final_image;

  unsigned m_next_tex = 0;
  unsigned m_next_sync = 0;

  HashMap<RGNodeID, std::string> m_node_text_descs;
  HashMap<RGTextureID, std::string> m_tex_text_descs;
  HashMap<RGSyncID, std::string> m_sync_text_descs;

protected:
  RGNodeID createNode();
  RGNode &getNode(RGNodeID node);
  RGNodeID getNodeID(const RGNode &node) const;

  RGTextureID defineTexture(RGNodeID node);
  RGTextureID defineTexture(const RGTextureDesc &desc, RGNodeID node);
  RGNodeID getTextureDef(RGTextureID tex) const;

  RGTextureID redefineTexture(RGTextureID tex, RGNodeID node);
  std::optional<RGNodeID> getTextureRedef(RGTextureID tex) const;

  RGSyncID defineSync(RGNodeID node);
  RGSyncID defineSync(const RGSyncDesc &desc, RGNodeID node);
  RGNodeID getSyncDef(RGSyncID sync) const;

public:
  RenderGraphBuilder(Device *device) : m_device(device) {}
  virtual ~RenderGraphBuilder() = default;

  [[nodiscard]] RGNodeBuilder addNode();
  void setSwapchain(Swapchain *swapchain);
  void setFinalImage(RGTextureID texture);

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

  void setCallback(RGNodeID node, PassCallback cb);

  void setDesc(RGNodeID node, std::string desc);
  std::string_view getDesc(RGNodeID node) const;

  void setDesc(RGTextureID, std::string desc);
  std::string_view getDesc(RGTextureID tex) const;

  void setDesc(RGSyncID, std::string desc);
  std::string_view getDesc(RGSyncID sync) const;

  class RenderGraph;
  [[nodiscard]] std::unique_ptr<RenderGraph> build();

protected:
  virtual void addPresentNodes() = 0;

  Vector<RGNode> schedulePasses();

  [[nodiscard]] HashMap<RGTextureID, TextureUsageFlags>
  deriveTextureUsageFlags(std::span<const RGNode> scheduled_passes);

  [[nodiscard]] HashMap<RGTextureID, Texture> createTextures(
      const HashMap<RGTextureID, TextureUsageFlags> &texture_usage_flags);

  [[nodiscard]] HashMap<RGSyncID, SyncObject> createSyncObjects();

  struct BarrierConfig {
    RGTextureID texture;
    MemoryAccessFlags src_accesses;
    PipelineStageFlags src_stages;
    MemoryAccessFlags dst_accesses;
    PipelineStageFlags dst_stages;
  };

  virtual PassCallback
  generateBarrierGroup(std::span<const BarrierConfig> configs) = 0;
  void generateBarriers(std::span<RGNode> scheduled_passes);

  struct Batch {
    SmallVector<PassCallback, 16> barrier_cbs;
    SmallVector<PassCallback, 16> pass_cbs;
  };

  Vector<Batch> batchPasses(auto scheduled_passes);

  virtual std::unique_ptr<RenderGraph>
  createRenderGraph(Vector<Batch> batches,
                    HashMap<RGTextureID, Texture> textures,
                    HashMap<RGTextureID, RGTextureID> texture_aliases,
                    HashMap<RGSyncID, SyncObject> syncs) = 0;
};

class RGResources {
  HashMap<RGTextureID, Texture> m_textures;
  HashMap<RGTextureID, RGTextureID> m_tex_aliases;
  HashMap<RGSyncID, SyncObject> m_syncs;

public:
  RGResources(HashMap<RGTextureID, Texture> textures,
              HashMap<RGTextureID, RGTextureID> texture_aliases,
              HashMap<RGSyncID, SyncObject> syncs)
      : m_textures(std::move(textures)),
        m_tex_aliases(std::move(texture_aliases)), m_syncs(std::move(syncs)) {}

  void setTexture(RGTextureID id, Texture tex);
  const Texture &getTexture(RGTextureID tex) const;

  void setSyncObject(RGSyncID id, SyncObject sync);
  const SyncObject &getSyncObject(RGSyncID sync) const;
};

class RenderGraphBuilder::RenderGraph {
protected:
  friend RenderGraphBuilder;

  Swapchain *m_swapchain;
  Vector<Batch> m_batches;
  RGResources m_resources;

  using Batch = Batch;

public:
  RenderGraph(Swapchain *swapchain, Vector<Batch> batches,
              HashMap<RGTextureID, Texture> textures,
              HashMap<RGTextureID, RGTextureID> texture_aliases,
              HashMap<RGSyncID, SyncObject> syncs)
      : m_swapchain(swapchain), m_batches(std::move(batches)),
        m_resources(std::move(textures), std::move(texture_aliases),
                    std::move(syncs)) {}
  virtual ~RenderGraph() = default;

  virtual void execute(CommandAllocator *cmd_pool) = 0;
};

using RenderGraph = RenderGraphBuilder::RenderGraph;
} // namespace ren
