#include "RenderGraph.hpp"
#include "Device.hpp"
#include "Support/HashSet.hpp"
#include "Support/PriorityQueue.hpp"

#include <range/v3/action.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {
void RGNodeBuilder::addReadInput(RGTextureID texture,
                                 MemoryAccessFlags accesses,
                                 PipelineStageFlags stages) {
  m_builder->addReadInput(m_node, texture, accesses, stages);
}

RGTextureID RGNodeBuilder::addWriteInput(RGTextureID texture,
                                         MemoryAccessFlags accesses,
                                         PipelineStageFlags stages) {
  return m_builder->addWriteInput(m_node, texture, accesses, stages);
}

RGTextureID RGNodeBuilder::addOutput(const RGTextureDesc &desc,
                                     MemoryAccessFlags accesses,
                                     PipelineStageFlags stages) {
  return m_builder->addOutput(m_node, desc, accesses, stages);
}

RGTextureID RGNodeBuilder::addExternalTextureOutput(MemoryAccessFlags accesses,
                                                    PipelineStageFlags stages) {
  return m_builder->addExternalTextureOutput(m_node, accesses, stages);
}

void RGNodeBuilder::addWaitSync(RGSyncID sync) {
  return m_builder->addWaitSync(m_node, sync);
}

RGSyncID RGNodeBuilder::addSignalSync(const RGSyncDesc &desc) {
  return m_builder->addSignalSync(m_node, desc);
}

RGSyncID RGNodeBuilder::addExternalSignalSync() {
  return m_builder->addExternalSignalSync(m_node);
}

void RGNodeBuilder::setCallback(PassCallback cb) {
  return m_builder->setCallback(m_node, std::move(cb));
}

void RGNodeBuilder::setDesc(std::string name) {
  return m_builder->setDesc(m_node, std::move(name));
}

RGNodeID RenderGraphBuilder::createNode() {
  auto node = m_nodes.size();
  m_nodes.emplace_back();
  return static_cast<RGNodeID>(node);
}

auto RenderGraphBuilder::getNode(RGNodeID node) -> RGNode & {
  auto idx = static_cast<size_t>(node);
  return m_nodes[idx];
}

RGNodeID RenderGraphBuilder::getNodeID(const RGNode &node) const {
  return static_cast<RGNodeID>(&node - m_nodes.data());
}

RGTextureID RenderGraphBuilder::defineTexture(RGNodeID node) {
  auto tex = static_cast<RGTextureID>(m_next_tex++);
  m_tex_defs[tex] = node;
  m_tex_aliases[tex] = tex;
  return tex;
}

RGTextureID RenderGraphBuilder::defineTexture(const RGTextureDesc &desc,
                                              RGNodeID node) {
  auto tex = defineTexture(node);
  m_tex_descs[tex] = desc;
  return tex;
}

RGNodeID RenderGraphBuilder::getTextureDef(RGTextureID tex) const {
  auto it = m_tex_defs.find(tex);
  assert(it != m_tex_defs.end() && "Undefined texture");
  return it->second;
}

RGTextureID RenderGraphBuilder::redefineTexture(RGTextureID tex,
                                                RGNodeID node) {
  auto new_tex = static_cast<RGTextureID>(m_next_tex++);
  m_tex_defs[new_tex] = node;
  m_tex_redefs[tex] = node;
  m_tex_aliases[new_tex] = m_tex_aliases[tex];
  return new_tex;
}

std::optional<RGNodeID>
RenderGraphBuilder::getTextureRedef(RGTextureID tex) const {
  auto it = m_tex_redefs.find(tex);
  if (it != m_tex_redefs.end()) {
    return it->second;
  }
  return std::nullopt;
}

RGSyncID RenderGraphBuilder::defineSync(RGNodeID node) {
  auto sync = static_cast<RGSyncID>(m_next_sync++);
  m_sync_defs[sync] = node;
  return sync;
}

RGSyncID RenderGraphBuilder::defineSync(const RGSyncDesc &desc, RGNodeID node) {
  auto sync = defineSync(node);
  m_sync_descs[sync] = desc;
  return sync;
}

RGNodeID RenderGraphBuilder::getSyncDef(RGSyncID sync) const {
  auto it = m_sync_defs.find(sync);
  assert(it != m_sync_defs.end() && "Undefined sync object");
  return it->second;
}

RGNodeBuilder RenderGraphBuilder::addNode() {
  auto node = createNode();
  return RGNodeBuilder(node, this);
}

void RenderGraphBuilder::addReadInput(RGNodeID node, RGTextureID tex,
                                      MemoryAccessFlags accesses,
                                      PipelineStageFlags stages) {
  getNode(node).read_textures.push_back({
      .tex = tex,
      .accesses = accesses,
      .stages = stages,
  });
}

RGTextureID RenderGraphBuilder::addWriteInput(RGNodeID node, RGTextureID tex,
                                              MemoryAccessFlags accesses,
                                              PipelineStageFlags stages) {
  auto new_tex = redefineTexture(tex, node);
  getNode(node).write_textures.push_back({
      .tex = new_tex,
      .accesses = accesses,
      .stages = stages,
  });
  return new_tex;
}

RGTextureID RenderGraphBuilder::addOutput(RGNodeID node,
                                          const RGTextureDesc &desc,
                                          MemoryAccessFlags accesses,
                                          PipelineStageFlags stages) {
  auto tex = defineTexture(desc, node);
  getNode(node).write_textures.push_back({
      .tex = tex,
      .accesses = accesses,
      .stages = stages,
  });
  return tex;
}

RGTextureID RenderGraphBuilder::addExternalTextureOutput(
    RGNodeID node, MemoryAccessFlags accesses, PipelineStageFlags stages) {
  auto tex = defineTexture(node);
  getNode(node).write_textures.push_back({
      .tex = tex,
      .accesses = accesses,
      .stages = stages,
  });
  return tex;
}

void RenderGraphBuilder::addWaitSync(RGNodeID node, RGSyncID sync) {
  getNode(node).wait_syncs.push_back({.sync = sync});
}

RGSyncID RenderGraphBuilder::addSignalSync(RGNodeID node,
                                           const RGSyncDesc &desc) {
  auto sync = defineSync(desc, node);
  getNode(node).signal_syncs.push_back({.sync = sync});
  return sync;
}

RGSyncID RenderGraphBuilder::addExternalSignalSync(RGNodeID node) {
  auto sync = defineSync(node);
  getNode(node).signal_syncs.push_back({.sync = sync});
  return sync;
}

void RenderGraphBuilder::setCallback(RGNodeID node, PassCallback cb) {
  getNode(node).pass_cb = std::move(cb);
}

void RenderGraphBuilder::setDesc(RGNodeID node, std::string name) {
  m_node_text_descs.insert_or_assign(node, std::move(name));
}

std::string_view RenderGraphBuilder::getDesc(RGNodeID node) const {
  auto it = m_node_text_descs.find(node);
  if (it != m_node_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraphBuilder::setDesc(RGTextureID tex, std::string name) {
  m_tex_text_descs.insert_or_assign(tex, std::move(name));
}

std::string_view RenderGraphBuilder::getDesc(RGTextureID tex) const {
  auto it = m_tex_text_descs.find(tex);
  if (it != m_tex_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraphBuilder::setDesc(RGSyncID sync, std::string name) {
  m_sync_text_descs.insert_or_assign(sync, std::move(name));
}

std::string_view RenderGraphBuilder::getDesc(RGSyncID sync) const {
  auto it = m_sync_text_descs.find(sync);
  if (it != m_sync_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraphBuilder::setSwapchain(Swapchain *swapchain) {
  m_swapchain = swapchain;
}

void RenderGraphBuilder::setFinalImage(RGTextureID tex) { m_final_image = tex; }

auto RenderGraphBuilder::schedulePasses() -> Vector<RGNode> {
  auto node_count = m_nodes.size();

  Vector<SmallVector<RGNodeID, 16>> edge_list(node_count);
  HashMap<RGNodeID, unsigned> predecessor_count;

  auto get_successors = [&](RGNodeID node) -> decltype(auto) {
    return edge_list[static_cast<size_t>(node)];
  };

  auto add_edge = [&](RGNodeID from, RGNodeID to) {
    auto &node_edges = get_successors(from);
    if (not ranges::contains(node_edges, to)) {
      node_edges.push_back(to);
      predecessor_count[to]++;
    }
  };

  struct QueueEntry {
    int max_dep_sched_time;
    RGNodeID node;

    auto operator<=>(const QueueEntry &) const = default;
  };

  // Schedule nodes whose dependencies were scheduled the longest time ago first
  MinQueue<QueueEntry> unscheduled_nodes;

  SmallVector<RGNodeID, 16> successors;
  auto get_dependants = [&](const RGNode &node) -> const auto & {
    successors.clear();
    for (const auto &rtex : node.read_textures) {
      if (auto redef = getTextureRedef(rtex.tex)) {
        // Reads must happen before writes
        successors.push_back(*redef);
      }
    }
    return successors;
  };

  SmallVector<RGNodeID, 16> predecessors;
  auto get_dependencies = [&](const RGNode &node) -> const auto & {
    predecessors.clear();
    for (const auto &rtex : node.read_textures) {
      // Reads must happen after creation
      predecessors.push_back(getTextureDef(rtex.tex));
    }
    for (const auto &wtex : node.write_textures) {
      // Writes must happen after creation
      auto def = getTextureDef(wtex.tex);
      if (def != getNodeID(node)) {
        predecessors.push_back(def);
      }
    }
    for (const auto &wsync : node.wait_syncs) {
      // Waits on sync objects must happen after they are signaled
      // TODO: this is not the case for timeline semaphores
      predecessors.push_back(getSyncDef(wsync.sync));
    }
    return predecessors;
  };

  SmallVector<RGNodeID, 16> outputs;
  auto get_outputs = [&](const RGNode &node) -> const auto & {
    outputs.clear();
    for (const auto &wtex : node.write_textures) {
      outputs.push_back(getTextureDef(wtex.tex));
    }
    for (const auto &ssync : node.signal_syncs) {
      outputs.push_back(getSyncDef(ssync.sync));
    }
    return outputs;
  };

  // Build DAG
  for (const auto &node : m_nodes) {
    auto &predecessors = get_dependencies(node);

    auto id = getNodeID(node);
    for (auto p : predecessors) {
      add_edge(p, id);
    }
    for (auto s : get_dependants(node)) {
      add_edge(id, s);
    }

    if (predecessors.empty()) {
      // This is a node with no dependencies and can be scheduled right away
      unscheduled_nodes.push({-1, id});
    }
  }

  Vector<RGNodeID> scheduled_pass_ids(m_nodes.size());
  HashMap<RGNodeID, int> node_sched_time;

  for (int i = 0; i < scheduled_pass_ids.size(); ++i) {
    assert(not unscheduled_nodes.empty());
    auto [dep_sched_time, node] = unscheduled_nodes.top();
    unscheduled_nodes.pop();
    assert(dep_sched_time < i);
    scheduled_pass_ids[i] = node;
    node_sched_time[node] = i;

    for (auto s : get_successors(node)) {
      if (--predecessor_count[s] == 0) {
        int max_dep_sched_time = 0;
        for (auto d : get_dependencies(getNode(s))) {
          max_dep_sched_time = std::max(max_dep_sched_time, node_sched_time[d]);
        }
        unscheduled_nodes.push({max_dep_sched_time, s});
      }
    }
  }
  assert(unscheduled_nodes.empty());

  auto scheduled_passes = scheduled_pass_ids |
                          ranges::views::transform([&](RGNodeID node) {
                            return std::move(getNode(node));
                          }) |
                          ranges::to<Vector<RGNode>>;
  m_nodes.clear();

  return scheduled_passes;
}

namespace {
TextureUsageFlags
getTextureUsageFlagsFromAccessesAndStages(MemoryAccessFlags accesses,
                                          PipelineStageFlags stages) {
  using enum MemoryAccess;
  using enum PipelineStage;
  TextureUsageFlags flags;
  if (accesses.isSet(ColorWrite)) {
    flags |= TextureUsage::RenderTarget;
  } else if (accesses.isSet(TransferRead)) {
    flags |= TextureUsage::TransferSRC;
  } else if (accesses.isSet(TransferWrite)) {
    flags |= TextureUsage::TransferDST;
  } else if (accesses.isSet(StorageRead) or accesses.isSet(StorageWrite)) {
    flags |= TextureUsage::Storage;
  }
  return flags;
}
} // namespace

HashMap<RGTextureID, TextureUsageFlags>
RenderGraphBuilder::deriveTextureUsageFlags(
    std::span<const RGNode> scheduled_passes) {
  HashMap<RGTextureID, TextureUsageFlags> texture_usage;
  for (const auto &pass : scheduled_passes) {
    auto textures =
        ranges::views::concat(pass.read_textures, pass.write_textures);
    for (const auto &tex : textures) {
      auto src_tex = m_tex_aliases[tex.tex];
      texture_usage[src_tex] |=
          getTextureUsageFlagsFromAccessesAndStages(tex.accesses, tex.stages);
    }
  }
  return texture_usage;
}

HashMap<RGTextureID, Texture> RenderGraphBuilder::createTextures(
    const HashMap<RGTextureID, TextureUsageFlags> &texture_usage_flags) {
  HashMap<RGTextureID, Texture> textures;
  for (const auto &[tex, desc] : m_tex_descs) {
    auto usage = [&, tex = tex] {
      auto it = texture_usage_flags.find(tex);
      assert(it != texture_usage_flags.end() &&
             "Texture usage flags are unknown");
      return it->second;
    }();
    textures[tex] = m_device->createTexture({
        .type = desc.type,
        .format = desc.format,
        .usage = usage,
        .width = desc.width,
        .height = desc.height,
        .layers = desc.layers,
        .levels = desc.levels,
    });
  }
  return textures;
}

HashMap<RGSyncID, SyncObject> RenderGraphBuilder::createSyncObjects() {
  HashMap<RGSyncID, SyncObject> syncs;
  for (const auto &[sync, desc] : m_sync_descs) {
    syncs[sync] = m_device->createSyncObject({.type = desc.type});
  }
  return syncs;
}

void RenderGraphBuilder::generateBarriers(std::span<RGNode> scheduled_passes) {
  struct ResourceAccess {
    MemoryAccessFlags accesses;
    PipelineStageFlags stages;
  };
  HashMap<RGTextureID, ResourceAccess> latest_accesses;
  SmallVector<BarrierConfig, 16> barrier_configs;
  for (auto &pass : scheduled_passes) {
    auto v =
        ranges::views::concat(pass.read_textures, pass.write_textures) |
        ranges::views::transform([&](const TextureAccess &tex_access) {
          auto src_access = std::exchange(
              latest_accesses[m_tex_aliases[tex_access.tex]],
              {.accesses = tex_access.accesses, .stages = tex_access.stages});
          return BarrierConfig{
              .texture = tex_access.tex,
              .src_accesses = src_access.accesses,
              .src_stages = src_access.stages,
              .dst_accesses = tex_access.accesses,
              .dst_stages = tex_access.stages,
          };
        });
    barrier_configs.assign(v.begin(), v.end());
    pass.barrier_cb = generateBarrierGroup(barrier_configs);
  }
}

auto RenderGraphBuilder::batchPasses(auto scheduled_passes) -> Vector<Batch> {
  scheduled_passes |= ranges::actions::remove_if(
      [](RGNode &node) { return !node.pass_cb and !node.barrier_cb; });

  Vector<Batch> batches;
  bool begin_new_batch = true;
  for (auto pass : scheduled_passes) {
    if (!pass.wait_syncs.empty()) {
      begin_new_batch = true;
    }
    if (begin_new_batch) {
      batches.emplace_back();
      begin_new_batch = false;
    }
    batches.back().barrier_cbs.emplace_back(std::move(pass.barrier_cb));
    batches.back().pass_cbs.emplace_back(std::move(pass.pass_cb));
    if (!pass.signal_syncs.empty()) {
      begin_new_batch = true;
    }
  }

  return batches;
}

auto RenderGraphBuilder::build() -> std::unique_ptr<RenderGraph> {
  addPresentNodes();
  auto scheduled_passes = schedulePasses();
  auto texture_usage_flags = deriveTextureUsageFlags(scheduled_passes);
  auto textures = createTextures(texture_usage_flags);
  auto syncs = createSyncObjects();
  generateBarriers(scheduled_passes);
  auto batches = batchPasses(std::move(scheduled_passes));
  return createRenderGraph(std::move(batches), std::move(textures),
                           std::move(m_tex_aliases), std::move(syncs));
}

void RGResources::setTexture(RGTextureID id, Texture tex) {
  bool inserted = m_textures.emplace(std::pair(id, std::move(tex))).second;
  assert(inserted && "Texture already defined");
}

const Texture &RGResources::getTexture(RGTextureID tex) const {
  auto it = m_tex_aliases.find(tex);
  assert(it != m_tex_aliases.end() && "Undefined texture");
  return m_textures.find(it->second)->second;
}

void RGResources::setSyncObject(RGSyncID id, SyncObject sync) {
  bool inserted = m_syncs.emplace(std::pair(id, std::move(sync))).second;
  assert(inserted && "Sync object already defined");
}

const SyncObject &RGResources::getSyncObject(RGSyncID sync) const {
  auto it = m_syncs.find(sync);
  assert(it != m_syncs.end() && "Undefined sync object");
  return it->second;
}
} // namespace ren
