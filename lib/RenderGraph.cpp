#include "RenderGraph.hpp"
#include "Device.hpp"
#include "Support/FlatSet.hpp"
#include "Support/HashSet.hpp"
#include "Support/PriorityQueue.hpp"
#include "Support/Views.hpp"

#include <range/v3/action.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

#include <bit>

namespace ren {
RGNodeID RenderGraph::Builder::createNode() {
  auto node = m_nodes.size();
  m_nodes.emplace_back();
  return static_cast<RGNodeID>(node);
}

auto RenderGraph::Builder::getNode(RGNodeID node) -> RGNode & {
  auto idx = static_cast<size_t>(node);
  return m_nodes[idx];
}

RGNodeID RenderGraph::Builder::getNodeID(const RGNode &node) const {
  return static_cast<RGNodeID>(&node - m_nodes.data());
}

unsigned
RenderGraph::Builder::createPhysicalTexture(const RGTextureDesc &desc) {
  auto tex = getPhysTextureCount();
  m_texture_descs.push_back(desc);
  return tex;
}

RGTextureID RenderGraph::Builder::createVirtualTexture(unsigned tex,
                                                       RGNodeID node) {
  auto vtex = std::bit_cast<RGTextureID>(m_texture_defs.insert(node));
  m_phys_textures[vtex] = tex;
  return vtex;
}

std::pair<unsigned, RGTextureID>
RenderGraph::Builder::createTexture(RGNodeID node) {
  auto tex = createPhysicalTexture({});
  auto vtex = createVirtualTexture(tex, node);
  return {tex, vtex};
}

RGNodeID RenderGraph::Builder::getTextureDef(RGTextureID tex) const {
  auto it = m_texture_defs.find(std::bit_cast<VTextureKey>(tex));
  assert(it != m_texture_defs.end() && "Undefined texture");
  return it->second;
}

std::optional<RGNodeID>
RenderGraph::Builder::getTextureKill(RGTextureID tex) const {
  auto it = m_texture_kills.find(tex);
  if (it != m_texture_kills.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool RenderGraph::Builder::isExternalTexture(unsigned tex) const {
  return m_texture_descs[tex].format == Format::Undefined;
}

unsigned RenderGraph::Builder::getPhysTextureCount() const {
  return m_texture_descs.size();
}

RGSyncID RenderGraph::Builder::createSync(RGNodeID node) {
  auto sync = static_cast<RGSyncID>(getSyncObjectCount());
  m_sync_defs.push_back(node);
  m_sync_descs.emplace_back();
  return sync;
}

RGNodeID RenderGraph::Builder::getSyncDef(RGSyncID sync) const {
  return m_sync_defs[static_cast<unsigned>(sync)];
}

namespace {
constexpr auto EXTERNAL_SYNC_TYPE = SyncType(0xe8a14a1);
}

bool RenderGraph::Builder::isExternalSync(unsigned sync) const {
  return m_sync_descs[sync].type == EXTERNAL_SYNC_TYPE;
}

unsigned RenderGraph::Builder::getSyncObjectCount() const {
  return m_sync_defs.size();
}

auto RenderGraph::Builder::addNode() -> NodeBuilder {
  return {createNode(), this};
}

void RenderGraph::Builder::addReadInput(RGNodeID node, RGTextureID tex,
                                        MemoryAccessFlags accesses,
                                        PipelineStageFlags stages) {
  getNode(node).read_textures.push_back({
      .texture = tex,
      .accesses = accesses,
      .stages = stages,
  });
}

RGTextureID RenderGraph::Builder::addWriteInput(RGNodeID node, RGTextureID tex,
                                                MemoryAccessFlags accesses,
                                                PipelineStageFlags stages) {
  m_texture_kills[tex] = node;
  auto new_tex = createVirtualTexture(m_phys_textures[tex], node);
  getNode(node).write_textures.push_back({
      .texture = new_tex,
      .accesses = accesses,
      .stages = stages,
  });
  return new_tex;
}

RGTextureID RenderGraph::Builder::addOutput(RGNodeID node,
                                            const RGTextureDesc &desc,
                                            MemoryAccessFlags accesses,
                                            PipelineStageFlags stages) {
  auto [tex, vtex] = createTexture(node);
  m_texture_descs[tex] = desc;
  getNode(node).write_textures.push_back({
      .texture = vtex,
      .accesses = accesses,
      .stages = stages,
  });
  return vtex;
}

RGTextureID RenderGraph::Builder::addExternalTextureOutput(
    RGNodeID node, MemoryAccessFlags accesses, PipelineStageFlags stages) {
  auto [_, tex] = createTexture(node);
  getNode(node).write_textures.push_back({
      .texture = tex,
      .accesses = accesses,
      .stages = stages,
  });
  return tex;
}

void RenderGraph::Builder::addWaitSync(RGNodeID node, RGSyncID sync) {
  getNode(node).wait_syncs.push_back({.sync = sync});
}

RGSyncID RenderGraph::Builder::addSignalSync(RGNodeID node,
                                             const RGSyncDesc &desc) {
  auto sync = createSync(node);
  m_sync_descs[static_cast<unsigned>(sync)] = desc;
  getNode(node).signal_syncs.push_back({.sync = sync});
  return sync;
}

RGSyncID RenderGraph::Builder::addExternalSignalSync(RGNodeID node) {
  auto sync = createSync(node);
  m_sync_descs[static_cast<unsigned>(sync)] = {.type = EXTERNAL_SYNC_TYPE};
  getNode(node).signal_syncs.push_back({.sync = sync});
  return sync;
}

void RenderGraph::Builder::setCallback(RGNodeID node, RGCallback cb) {
  getNode(node).pass_cb = std::move(cb);
}

void RenderGraph::Builder::setDesc(RGNodeID node, std::string name) {
  m_node_text_descs.insert_or_assign(node, std::move(name));
}

std::string_view RenderGraph::Builder::getDesc(RGNodeID node) const {
  auto it = m_node_text_descs.find(node);
  if (it != m_node_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraph::Builder::setDesc(RGTextureID tex, std::string name) {
  m_tex_text_descs.insert_or_assign(tex, std::move(name));
}

std::string_view RenderGraph::Builder::getDesc(RGTextureID tex) const {
  auto it = m_tex_text_descs.find(tex);
  if (it != m_tex_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraph::Builder::setDesc(RGSyncID sync, std::string name) {
  m_sync_text_descs.insert_or_assign(sync, std::move(name));
}

std::string_view RenderGraph::Builder::getDesc(RGSyncID sync) const {
  auto it = m_sync_text_descs.find(sync);
  if (it != m_sync_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraph::Builder::setSwapchain(Swapchain *swapchain) {
  m_swapchain = swapchain;
}

void RenderGraph::Builder::setFinalImage(RGTextureID tex) {
  m_final_image = tex;
}

auto RenderGraph::Builder::schedulePasses() -> Vector<RGNode> {
  Vector<SmallFlatSet<RGNodeID>> successors(m_nodes.size());
  Vector<int> predecessor_count(m_nodes.size());

  auto get_node_idx = [](RGNodeID node) { return static_cast<size_t>(node); };

  auto get_dag_successors = [&](RGNodeID node) -> auto & {
    return successors[get_node_idx(node)];
  };

  auto add_edge = [&](RGNodeID from, RGNodeID to) {
    if (get_dag_successors(from).insert(to).second) {
      ++predecessor_count[get_node_idx(to)];
    }
  };

  auto get_texture_def = [&](const TextureAccess &tex_access) {
    return getTextureDef(tex_access.texture);
  };
  auto get_texture_kill = [&](const TextureAccess &tex_access) {
    return getTextureKill(tex_access.texture);
  };
  auto get_sync_def = [&](const SyncAccess &sync_access) {
    return getSyncDef(sync_access.sync);
  };

  SmallVector<RGNodeID> dependents;
  auto get_dependants = [&](const RGNode &node) -> const auto & {
    // Reads must happen before writes
    dependents.assign(node.read_textures | filter_map(get_texture_kill));
    return dependents;
  };

  SmallVector<RGNodeID> dependencies;
  auto get_dependencies = [&](const RGNode &node) -> const auto & {
    dependencies.assign(concat(
        // Reads must happen after creation
        node.read_textures | map(get_texture_def),
        // Writes must happen after creation
        node.write_textures | map(get_texture_def) |
            filter([&](RGNodeID def) { return def != getNodeID(node); }),
        // Waits on sync objects must happen after they are signaled
        // TODO: this is not the case for timeline semaphores
        node.wait_syncs | map(get_sync_def)));
    return dependencies;
  };

  SmallVector<RGNodeID> outputs;
  auto get_outputs = [&](const RGNode &node) -> const auto & {
    outputs.assign(concat(node.write_textures | map(get_texture_def),
                          node.signal_syncs | map(get_sync_def)));
    return outputs;
  };

  struct QueueEntry {
    int max_dep_sched_time;
    RGNodeID node;

    auto operator<=>(const QueueEntry &) const = default;
  };

  // Schedule passes whose dependencies were scheduled the longest time ago
  // first
  MinQueue<QueueEntry> unsched_passes;

  // Build DAG
  for (const auto &pass : m_nodes) {
    const auto &predecessors = get_dependencies(pass);

    auto id = getNodeID(pass);
    for (auto p : predecessors) {
      add_edge(p, id);
    }
    for (auto s : get_dependants(pass)) {
      add_edge(id, s);
    }

    if (predecessors.empty()) {
      // This is a pass with no dependencies and it can be scheduled right away
      unsched_passes.push({-1, id});
    }
  }

  Vector<RGNodeID> sched_passes;
  sched_passes.reserve(m_nodes.size());
  auto &pass_sched_time = predecessor_count;

  while (not unsched_passes.empty()) {
    auto [dep_sched_time, pass] = unsched_passes.top();
    unsched_passes.pop();

    int time = sched_passes.size();
    assert(dep_sched_time < time);
    sched_passes.push_back(pass);
    pass_sched_time[get_node_idx(pass)] = time;

    for (auto s : get_dag_successors(pass)) {
      if (--predecessor_count[get_node_idx(s)] == 0) {
        int max_dep_sched_time = ranges::max(
            concat(once(-1), get_dependencies(getNode(s)) | map([&](auto d) {
                               return pass_sched_time[get_node_idx(d)];
                             })));
        unsched_passes.push({max_dep_sched_time, s});
      }
    }
  }

  auto passes = sched_passes |
                map([&](RGNodeID pass) { return std::move(getNode(pass)); }) |
                ranges::to<Vector>;
  m_nodes.clear();

  return passes;
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

Vector<TextureUsageFlags> RenderGraph::Builder::deriveTextureUsageFlags(
    std::span<const RGNode> scheduled_passes) {
  Vector<TextureUsageFlags> texture_usage(getPhysTextureCount());
  for (const auto &pass : scheduled_passes) {
    auto tex_accesses =
        ranges::views::concat(pass.read_textures, pass.write_textures);
    for (const auto &tex_access : tex_accesses) {
      auto tex = m_phys_textures[tex_access.texture];
      texture_usage[tex] |= getTextureUsageFlagsFromAccessesAndStages(
          tex_access.accesses, tex_access.stages);
    }
  }
  return texture_usage;
}

Vector<Texture> RenderGraph::Builder::createTextures(
    std::span<const TextureUsageFlags> texture_usage_flags) {
  Vector<Texture> textures(getPhysTextureCount());
  for (unsigned tex = 0; tex < getPhysTextureCount(); ++tex) {
    if (not isExternalTexture(tex)) {
      const auto &desc = m_texture_descs[tex];
      textures[tex] = m_device->createTexture({
          .type = desc.type,
          .format = desc.format,
          .usage = texture_usage_flags[tex],
          .width = desc.width,
          .height = desc.height,
          .layers = desc.layers,
          .levels = desc.levels,
      });
    }
  }
  return textures;
}

Vector<SyncObject> RenderGraph::Builder::createSyncObjects() {
  Vector<SyncObject> syncs(getSyncObjectCount());
  for (unsigned sync = 0; sync < getSyncObjectCount(); ++sync) {
    if (not isExternalSync(sync)) {
      const auto &desc = m_sync_descs[sync];
      syncs[sync] = m_device->createSyncObject({.type = desc.type});
    }
  }
  return syncs;
}

void RenderGraph::Builder::generateBarriers(
    std::span<RGNode> scheduled_passes) {
  struct ResourceAccess {
    MemoryAccessFlags accesses;
    PipelineStageFlags stages;
  };
  Vector<ResourceAccess> latest_accesses(getPhysTextureCount());
  SmallVector<BarrierConfig, 16> barrier_configs;
  for (auto &pass : scheduled_passes) {
    barrier_configs.assign(
        concat(pass.read_textures, pass.write_textures) |
        map([&](const TextureAccess &tex_access) {
          auto src_access = std::exchange(
              latest_accesses[m_phys_textures[tex_access.texture]],
              {.accesses = tex_access.accesses, .stages = tex_access.stages});
          return BarrierConfig{
              .texture = tex_access.texture,
              .src_accesses = src_access.accesses,
              .src_stages = src_access.stages,
              .dst_accesses = tex_access.accesses,
              .dst_stages = tex_access.stages,
          };
        }));
    pass.barrier_cb = generateBarrierGroup(barrier_configs);
  }
}

auto RenderGraph::Builder::batchPasses(auto scheduled_passes) -> Vector<Batch> {
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

auto RenderGraph::Builder::build() -> std::unique_ptr<RenderGraph> {
  addPresentNodes();
  auto scheduled_passes = schedulePasses();
  auto texture_usage_flags = deriveTextureUsageFlags(scheduled_passes);
  auto textures = createTextures(texture_usage_flags);
  auto syncs = createSyncObjects();
  generateBarriers(scheduled_passes);
  auto batches = batchPasses(std::move(scheduled_passes));
  return createRenderGraph(std::move(batches), std::move(textures),
                           std::move(m_phys_textures), std::move(syncs));
}

void RenderGraph::setTexture(RGTextureID vtex, Texture texture) {
  auto tex = m_phys_textures[vtex];
  assert(!m_textures[tex].handle.get() && "Texture already defined");
  m_textures[tex] = std::move(texture);
}

const Texture &RenderGraph::getTexture(RGTextureID tex) const {
  auto it = m_phys_textures.find(tex);
  assert(it != m_phys_textures.end() && "Undefined texture");
  return m_textures[it->second];
}

void RenderGraph::setSyncObject(RGSyncID id, SyncObject sync) {
  auto idx = static_cast<unsigned>(id);
  assert(!m_syncs[idx].handle.get() && "Sync object already defined");
  m_syncs[idx] = std::move(sync);
}

const SyncObject &RenderGraph::getSyncObject(RGSyncID sync) const {
  return m_syncs[static_cast<unsigned>(sync)];
}
} // namespace ren
