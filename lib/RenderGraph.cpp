#include "RenderGraph.hpp"
#include "Device.hpp"
#include "Support/FlatSet.hpp"
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

auto RenderGraph::Builder::getTextureKill(RGTextureID tex) const
    -> Optional<RGNodeID> {
  auto it = m_texture_kills.find(tex);
  if (it != m_texture_kills.end()) {
    return it->second;
  }
  return None;
}

bool RenderGraph::Builder::isExternalTexture(unsigned tex) const {
  return m_texture_descs[tex].format == Format::Undefined;
}

unsigned RenderGraph::Builder::getPhysTextureCount() const {
  return m_texture_descs.size();
}

auto RenderGraph::Builder::create_physical_buffer(const RGBufferDesc &desc)
    -> unsigned {
  auto buffer = get_physical_buffer_count();
  m_buffer_descs.push_back(desc);
  return buffer;
}

auto RenderGraph::Builder::create_virtual_buffer(unsigned buffer, RGNodeID node)
    -> RGBufferID {
  auto vbuffer = std::bit_cast<RGBufferID>(m_buffer_defs.insert(node));
  m_physical_buffers[vbuffer] = buffer;
  return vbuffer;
}

auto RenderGraph::Builder::create_buffer(RGNodeID node)
    -> std::pair<unsigned, RGBufferID> {
  auto buffer = create_physical_buffer({});
  auto vbuffer = create_virtual_buffer(buffer, node);
  return {buffer, vbuffer};
}

auto RenderGraph::Builder::get_buffer_def(RGBufferID buffer) const -> RGNodeID {
  auto it = m_buffer_defs.find(std::bit_cast<VBufferKey>(buffer));
  assert(it != m_buffer_defs.end() && "Undefined buffer");
  return it->second;
}

auto RenderGraph::Builder::get_buffer_kill(RGBufferID buffer) const
    -> Optional<RGNodeID> {
  auto it = m_buffer_kills.find(buffer);
  if (it != m_buffer_kills.end()) {
    return it->second;
  }
  return None;
}

auto RenderGraph::Builder::get_physical_buffer_count() const -> unsigned {
  return m_buffer_descs.size();
}

auto RenderGraph::Builder::create_semaphore() -> RGSemaphoreID {
  return static_cast<RGSemaphoreID>(m_num_semaphores++);
}

void RenderGraph::Builder::wait_semaphore(RGNodeID node,
                                          RGSemaphoreID semaphore,
                                          uint64_t value,
                                          VkPipelineStageFlags2 stages) {
  getNode(node).wait_semaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = reinterpret_cast<VkSemaphore>(semaphore),
      .value = value,
      .stageMask = stages,
  });
}

void RenderGraph::Builder::signal_semaphore(RGNodeID node,
                                            RGSemaphoreID semaphore,
                                            uint64_t value,
                                            VkPipelineStageFlags2 stages) {
  getNode(node).signal_semaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = reinterpret_cast<VkSemaphore>(semaphore),
      .value = value,
      .stageMask = stages,
  });
}

auto RenderGraph::Builder::addNode() -> NodeBuilder {
  return {createNode(), this};
}

void RenderGraph::Builder::addReadInput(RGNodeID node, RGTextureID tex,
                                        VkAccessFlags2 accesses,
                                        VkPipelineStageFlags2 stages) {
  getNode(node).read_textures.push_back({
      .texture = tex,
      .accesses = accesses,
      .stages = stages,
  });
}

RGTextureID RenderGraph::Builder::addWriteInput(RGNodeID node, RGTextureID tex,
                                                VkAccessFlags2 accesses,
                                                VkPipelineStageFlags2 stages) {
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
                                            VkAccessFlags2 accesses,
                                            VkPipelineStageFlags2 stages) {
  auto [tex, vtex] = createTexture(node);
  m_texture_descs[tex] = desc;
  getNode(node).write_textures.push_back({
      .texture = vtex,
      .accesses = accesses,
      .stages = stages,
  });
  return vtex;
}

RGBufferID RenderGraph::Builder::add_output(RGNodeID node,
                                            const RGBufferDesc &desc,
                                            VkAccessFlags2 accesses,
                                            VkPipelineStageFlags2 stages) {
  auto [buffer, vbuffer] = create_buffer(node);
  m_buffer_descs[buffer] = desc;
  getNode(node).write_buffers.push_back({
      .buffer = vbuffer,
      .accesses = accesses,
      .stages = stages,
  });
  return vbuffer;
}

RGTextureID RenderGraph::Builder::addExternalTextureOutput(
    RGNodeID node, VkAccessFlags2 accesses, VkPipelineStageFlags2 stages) {
  auto [_, tex] = createTexture(node);
  getNode(node).write_textures.push_back({
      .texture = tex,
      .accesses = accesses,
      .stages = stages,
  });
  return tex;
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

void RenderGraph::Builder::set_desc(RGBufferID buffer, std::string name) {
  m_buffer_text_descs.insert_or_assign(buffer, std::move(name));
}

std::string_view RenderGraph::Builder::get_desc(RGBufferID buffer) const {
  auto it = m_buffer_text_descs.find(buffer);
  if (it != m_buffer_text_descs.end()) {
    return it->second;
  }
  return "";
}

void RenderGraph::Builder::set_desc(RGSemaphoreID semaphore, std::string name) {
  m_semaphore_text_descs.insert_or_assign(semaphore, std::move(name));
}

std::string_view RenderGraph::Builder::get_desc(RGSemaphoreID semaphore) const {
  auto it = m_semaphore_text_descs.find(semaphore);
  if (it != m_semaphore_text_descs.end()) {
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
  auto get_buffer_def = [&](const BufferAccess &buffer_access) {
    return this->get_buffer_def(buffer_access.buffer);
  };
  auto get_buffer_kill = [&](const BufferAccess &buffer_access) {
    return this->get_buffer_kill(buffer_access.buffer);
  };

  SmallVector<RGNodeID> dependents;
  auto get_dependants = [&](const RGNode &node) -> const auto & {
    // Reads must happen before writes
    dependents.assign(concat(node.read_textures | filter_map(get_texture_kill),
                             node.read_buffers | filter_map(get_buffer_kill)));
    return dependents;
  };

  SmallVector<RGNodeID> dependencies;
  auto get_dependencies = [&](const RGNode &node) -> const auto & {
    auto is_not_create = [&](RGNodeID def) { return def != getNodeID(node); };
    dependencies.assign(concat(
        // Reads must happen after creation
        node.read_textures | map(get_texture_def),
        node.read_buffers | map(get_buffer_def),
        // Writes must happen after creation
        node.write_textures | map(get_texture_def) | filter(is_not_create),
        node.write_buffers | map(get_buffer_def) | filter(is_not_create)));
    return dependencies;
  };

  SmallVector<RGNodeID> outputs;
  auto get_outputs = [&](const RGNode &node) -> const auto & {
    outputs.assign(concat(node.write_textures | map(get_texture_def),
                          node.write_buffers | map(get_buffer_def)));
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
auto get_texture_usage_flags(VkAccessFlags2 accesses) -> VkImageUsageFlags {
  VkImageUsageFlags flags = 0;
  if (accesses & VK_ACCESS_2_TRANSFER_READ_BIT) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (accesses & VK_ACCESS_2_TRANSFER_WRITE_BIT) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if (accesses & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (accesses & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (accesses & VK_ACCESS_2_SHADER_STORAGE_READ_BIT or
      accesses & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  return flags;
}

auto get_buffer_usage_flags(VkAccessFlags2 accesses) -> VkBufferUsageFlags {
  VkBufferUsageFlags flags = 0;
  if (accesses & VK_ACCESS_2_TRANSFER_READ_BIT) {
    flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if (accesses & VK_ACCESS_2_TRANSFER_WRITE_BIT) {
    flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  if (accesses & VK_ACCESS_2_UNIFORM_READ_BIT) {
    flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (accesses & VK_ACCESS_2_SHADER_STORAGE_READ_BIT or
      accesses & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (accesses & VK_ACCESS_2_INDEX_READ_BIT) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (accesses & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT) {
    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  }
  return flags;
}
} // namespace

auto RenderGraph::Builder::derive_resource_usage_flags(
    std::span<const RGNode> scheduled_passes)
    -> std::pair<Vector<VkImageUsageFlags>, Vector<VkBufferUsageFlags>> {
  Vector<VkImageUsageFlags> texture_usage(getPhysTextureCount());
  Vector<VkBufferUsageFlags> buffer_usage(get_physical_buffer_count());
  for (const auto &pass : scheduled_passes) {
    for (const auto &texture_access :
         concat(pass.read_textures, pass.write_textures)) {
      auto texture = m_phys_textures[texture_access.texture];
      texture_usage[texture] |=
          get_texture_usage_flags(texture_access.accesses);
    }
    for (const auto &buffer_access :
         concat(pass.read_buffers, pass.write_buffers)) {
      auto buffer = m_physical_buffers[buffer_access.buffer];
      buffer_usage[buffer] |= get_buffer_usage_flags(buffer_access.accesses);
    }
  }
  return {std::move(texture_usage), std::move(buffer_usage)};
}

Vector<Texture> RenderGraph::Builder::createTextures(
    std::span<const VkImageUsageFlags> texture_usage_flags) {
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
          .array_layers = desc.array_layers,
          .mip_levels = desc.mip_levels,
      });
    }
  }
  return textures;
}

auto RenderGraph::Builder::create_buffers(
    std::span<const VkBufferUsageFlags> buffer_usage_flags) -> Vector<Buffer> {
  Vector<Buffer> buffers(get_physical_buffer_count());
  for (auto buffer : range<unsigned>(buffers.size())) {
    const auto &desc = m_buffer_descs[buffer];
    buffers[buffer] = m_device->create_buffer({
        .usage = buffer_usage_flags[buffer],
        .heap = desc.heap,
        .size = desc.size,
    });
  }
  return buffers;
}

void RenderGraph::Builder::generateBarriers(
    std::span<RGNode> scheduled_passes) {
  struct ResourceAccess {
    VkAccessFlags2 accesses;
    VkPipelineStageFlags2 stages;
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
    if (!pass.wait_semaphores.empty()) {
      begin_new_batch = true;
    }
    if (begin_new_batch) {
      auto &batch = batches.emplace_back();
      batch.wait_semaphores = std::move(pass.wait_semaphores);
      begin_new_batch = false;
    }
    auto &batch = batches.back();
    batch.barrier_cbs.emplace_back(std::move(pass.barrier_cb));
    batch.pass_cbs.emplace_back(std::move(pass.pass_cb));
    if (!pass.signal_semaphores.empty()) {
      batch.signal_semaphores = std::move(pass.signal_semaphores);
      begin_new_batch = true;
    }
  }

  return batches;
}

auto RenderGraph::Builder::build() -> std::unique_ptr<RenderGraph> {
  addPresentNodes();
  auto scheduled_passes = schedulePasses();
  auto [texture_usage_flags, buffer_usage_flags] =
      derive_resource_usage_flags(scheduled_passes);
  auto textures = createTextures(texture_usage_flags);
  auto buffers = create_buffers(buffer_usage_flags);
  generateBarriers(scheduled_passes);
  auto batches = batchPasses(std::move(scheduled_passes));
  return create_render_graph({
      .swapchain = m_swapchain,
      .batches = std::move(batches),
      .textures = std::move(textures),
      .phys_textures = std::move(m_phys_textures),
      .buffers = std::move(buffers),
      .physical_buffers = std::move(m_physical_buffers),
      .num_semaphores = m_num_semaphores,
  });
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

void RenderGraph::set_buffer(RGBufferID vbuffer, Buffer buffer) {
  auto pbuffer = m_physical_buffers[vbuffer];
  assert(!m_buffers[pbuffer].get() && "Buffer already defined");
  m_buffers[pbuffer] = std::move(buffer);
}

auto RenderGraph::get_buffer(RGBufferID buffer) const -> const Buffer & {
  auto it = m_physical_buffers.find(buffer);
  assert(it != m_physical_buffers.end() && "Undefined buffer");
  return m_buffers[it->second];
}

void RenderGraph::set_semaphore(RGSemaphoreID id, VkSemaphore semaphore) {
  m_semaphores[id] = semaphore;
}

auto RenderGraph::get_semaphore(RGSemaphoreID semaphore) const -> VkSemaphore {
  return m_semaphores[semaphore];
}

} // namespace ren
