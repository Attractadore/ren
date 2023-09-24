#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Support/FlatSet.hpp"
#include "Support/Math.hpp"
#include "Support/PriorityQueue.hpp"
#include "Support/Views.hpp"

#include <range/v3/action.hpp>

namespace ren {

namespace {

constexpr VkAccessFlags2 READ_ONLY_ACCESS_MASK =
    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
    VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT |
    VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
    VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

constexpr VkAccessFlags2 WRITE_ONLY_ACCESS_MASK =
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

auto get_buffer_usage_flags(VkAccessFlags2 accesses) -> VkBufferUsageFlags {
  assert((accesses & VK_ACCESS_2_MEMORY_READ_BIT) == 0);
  assert((accesses & VK_ACCESS_2_MEMORY_WRITE_BIT) == 0);
  assert((accesses & VK_ACCESS_2_SHADER_READ_BIT) == 0);
  assert((accesses & VK_ACCESS_2_SHADER_WRITE_BIT) == 0);

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
  if (accesses & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
    flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }
  if (accesses & VK_ACCESS_2_INDEX_READ_BIT) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (accesses & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT) {
    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  }

  return flags;
}

auto get_texture_usage_flags(VkAccessFlags2 accesses) -> VkImageUsageFlags {
  assert((accesses & VK_ACCESS_2_MEMORY_READ_BIT) == 0);
  assert((accesses & VK_ACCESS_2_MEMORY_WRITE_BIT) == 0);
  assert((accesses & VK_ACCESS_2_SHADER_READ_BIT) == 0);
  assert((accesses & VK_ACCESS_2_SHADER_WRITE_BIT) == 0);

  VkImageUsageFlags flags = 0;
  if (accesses & VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT) {
    flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  }
  if (accesses & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (accesses & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (accesses & (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (accesses & (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (accesses & VK_ACCESS_2_TRANSFER_READ_BIT) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (accesses & VK_ACCESS_2_TRANSFER_WRITE_BIT) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  return flags;
}

} // namespace

RgBuilder::RgBuilder(RenderGraph &rg) {
  ren_rg_time_region("builder-init");
  m_rg = &rg;
  m_rg->m_pass_ids.clear();
  m_rg->m_pass_datas = {{}};
#if REN_RG_DEBUG
  m_rg->m_pass_names = {{}};
#endif
  m_rg->m_pass_update_callbacks = {{}};
  m_rg->m_buffer_ids.clear();
  m_rg->m_buffer_parents = {{}};
  m_rg->m_texture_ids.clear();
  m_rg->m_texture_parents = {{}};
}

auto RgBuilder::create_pass(String name) -> RgPassBuilder {
  ren_rg_inc_time_counter(m_create_pass_counter);
  RgPassId id(m_passes.size());
  m_passes.emplace_back();
  m_rg->m_pass_datas.emplace_back();
#if REN_RG_DEBUG
  m_rg->m_pass_names.push_back(name);
#endif
  m_rg->m_pass_update_callbacks.emplace_back();
  m_rg->m_pass_ids.insert(std::move(name), id);
  return RgPassBuilder(id, *this);
}

auto RgBuilder::add_buffer_use(RgBufferId buffer, const RgBufferUsage &usage)
    -> RgBufferUseId {
  RgBufferUseId id(m_buffer_uses.size());
  m_buffer_uses.push_back({
      .buffer = buffer,
      .usage = usage,
  });
  return id;
};

auto RgBuilder::get_buffer_def(RgBufferId buffer) const -> RgPassId {
  return m_buffer_defs[buffer];
}

auto RgBuilder::get_buffer_kill(RgBufferId buffer) const -> RgPassId {
  return m_buffer_kills[buffer];
}

auto RgBuilder::get_or_alloc_buffer(StringView name) -> RgBufferId {
  auto it = m_rg->m_buffer_ids.find(name);
  if (it == m_rg->m_buffer_ids.end()) {
    RgBufferId id(m_rg->m_buffer_parents.size());
    m_rg->m_buffer_parents.emplace_back();
    m_buffer_defs.emplace_back();
    m_buffer_kills.emplace_back();
#if REN_RG_DEBUG
    m_buffer_children.emplace_back();
#endif
    it = m_rg->m_buffer_ids.insert(it, String(name), id);
  }
  RgBufferId id = it->second;
#if REN_RG_DEBUG
  {
    auto it = m_buffer_names.find(id);
    if (it == m_buffer_names.end()) {
      m_buffer_names.insert(it, id, String(name));
    }
  }
#endif
  return id;
}

void RgBuilder::create_buffer(RgBufferCreateInfo &&create_info) {
  ren_rg_inc_time_counter(m_create_buffer_counter);
  RgBufferId id = get_or_alloc_buffer(std::move(create_info.name));
  m_rg->m_buffer_parents[id] = id;
  m_buffer_descs.insert(RgPhysicalBufferId(id), {
                                                    .heap = create_info.heap,
                                                    .size = create_info.size,
                                                });
}

auto RgBuilder::read_buffer(RgPassId pass, StringView buffer,
                            const RgBufferUsage &usage) -> RgBufferId {
  ren_rg_inc_time_counter(m_read_buffer_counter);
  RgBufferId id = get_or_alloc_buffer(buffer);
  m_passes[pass].read_buffers.push_back(add_buffer_use(id, usage));
  return id;
}

auto RgBuilder::write_buffer(RgPassId pass, StringView dst_buffer,
                             StringView src_buffer, const RgBufferUsage &usage)
    -> RgBufferId {
  ren_rg_inc_time_counter(m_write_buffer_counter);
  RgBufferId src_id = get_or_alloc_buffer(src_buffer);
  RgBufferId dst_id = get_or_alloc_buffer(dst_buffer);
  m_buffer_kills[src_id] = pass;
#if REN_RG_DEBUG
  m_buffer_children[src_id] = dst_id;
#endif
  m_buffer_defs[dst_id] = pass;
  ren_assert(!m_rg->m_buffer_parents[dst_id],
             "Render graph buffers must be written only once");
  m_rg->m_buffer_parents[dst_id] = src_id;
  m_passes[pass].write_buffers.push_back(add_buffer_use(src_id, usage));
  return dst_id;
}

auto RgBuilder::is_buffer_valid(StringView buffer) const -> bool {
  return m_rg->is_buffer_valid(buffer);
}

auto RgBuilder::add_texture_use(RgTextureId texture,
                                const RgTextureUsage &usage) -> RgTextureUseId {
  RgTextureUseId id(m_texture_uses.size());
  m_texture_uses.push_back({
      .texture = texture,
      .usage = usage,
  });
  return id;
}

auto RgBuilder::get_texture_def(RgTextureId texture) const -> RgPassId {
  return m_texture_defs[texture];
}

auto RgBuilder::get_texture_kill(RgTextureId texture) const -> RgPassId {
  return m_texture_kills[texture];
}

auto RgBuilder::get_or_alloc_texture(StringView name, u32 temporal_layer)
    -> RgTextureId {
  RgTextureId id;
  auto it = m_rg->m_texture_ids.find(name);
  if (it != m_rg->m_texture_ids.end()) {
    id = RgTextureId(it->second + temporal_layer);
  } else {
    id = RgTextureId(m_rg->m_texture_parents.size());
    usize num_textures = id + RG_MAX_TEMPORAL_LAYERS;
    m_rg->m_texture_parents.resize(num_textures);
    m_rg->m_texture_ids.insert(it, String(name), id);
    m_texture_defs.resize(num_textures);
    m_texture_kills.resize(num_textures);
#if REN_RG_DEBUG
    m_texture_children.resize(num_textures);
#endif
    id = RgTextureId(id + temporal_layer);
  }
#if REN_RG_DEBUG
  {
    auto it = m_texture_names.find(id);
    if (it == m_texture_names.end()) {
      if (temporal_layer > 0) {
        m_texture_names.insert(it, id,
                               fmt::format("{}#{}", name, temporal_layer));
      } else {
        m_texture_names.insert(it, id, String(name));
      }
    }
  }
#endif
  return id;
}

void RgBuilder::create_texture(RgTextureCreateInfo &&create_info) {
  ren_rg_inc_time_counter(m_create_texture_counter);
  assert(create_info.num_temporal_layers > 0);
  RgTextureId id = get_or_alloc_texture(std::move(create_info.name));
  for (int i : range(create_info.num_temporal_layers)) {
    m_rg->m_texture_parents[id + i] = RgTextureId(id + i);
  }
  m_texture_descs.insert(
      RgPhysicalTextureId(id),
      {
          .type = create_info.type,
          .format = create_info.format,
          .width = create_info.width,
          .height = create_info.height,
          .depth = create_info.depth,
          .num_mip_levels = create_info.num_mip_levels,
          .num_array_layers = create_info.num_array_layers,
          .num_temporal_layers = create_info.num_temporal_layers,
          .clear = create_info.clear,
      });
}

auto RgBuilder::read_texture(RgPassId pass, StringView texture,
                             const RgTextureUsage &usage, u32 temporal_layer)
    -> RgTextureId {
  ren_rg_inc_time_counter(m_read_texture_counter);
  RgTextureId id = get_or_alloc_texture(texture, temporal_layer);
  m_passes[pass].read_textures.push_back(add_texture_use(id, usage));
  return id;
}

auto RgBuilder::write_texture(RgPassId pass, StringView dst_texture,
                              StringView src_texture,
                              const RgTextureUsage &usage) -> RgTextureId {
  ren_rg_inc_time_counter(m_write_texture_counter);
  RgTextureId src_id = get_or_alloc_texture(src_texture);
  RgTextureId dst_id = get_or_alloc_texture(dst_texture);
  m_texture_kills[src_id] = pass;
#if REN_RG_DEBUG
  m_texture_children[src_id] = dst_id;
#endif
  m_texture_defs[dst_id] = pass;
  for (int i : range(RG_MAX_TEMPORAL_LAYERS)) {
    ren_assert(!m_rg->m_texture_parents[dst_id + i],
               "Render graph textures must be written only once");
    m_rg->m_texture_parents[dst_id + i] = RgTextureId(src_id + i);
  }
  m_passes[pass].write_textures.push_back(add_texture_use(src_id, usage));
  return dst_id;
}

auto RgBuilder::is_texture_valid(StringView texture) const -> bool {
  return m_rg->is_texture_valid(texture);
}

auto RgBuilder::add_semaphore_signal(RgSemaphoreId semaphore,
                                     VkPipelineStageFlags2 stage_mask,
                                     u64 value) -> RgSemaphoreSignalId {
  RgSemaphoreSignalId id(m_semaphore_signals.size());
  m_semaphore_signals.push_back({
      .semaphore = semaphore,
      .stage_mask = stage_mask,
      .value = value,
  });
  return id;
}

auto RgBuilder::alloc_semaphore(StringView name) -> RgSemaphoreId {
  RgSemaphoreId id(m_semaphore_ids.size());
  m_semaphore_ids.insert(String(name), id);
#if REN_RG_DEBUG
  m_semaphore_names.emplace_back(name);
#endif
  return id;
}

void RgBuilder::wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                               VkPipelineStageFlags2 stage_mask, u64 value) {
  ren_rg_inc_time_counter(m_wait_semaphore_counter);
  m_passes[pass].wait_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
}

void RgBuilder::signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                                 VkPipelineStageFlags2 stage_mask, u64 value) {
  ren_rg_inc_time_counter(m_signal_semaphore_counter);
  m_passes[pass].signal_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
}

void RgBuilder::present(StringView texture_name) {
  ren_rg_inc_time_counter(m_present_counter);

  m_rg->m_acquire_semaphore = alloc_semaphore("rg-acquire-semaphore");
  m_rg->m_present_semaphore = alloc_semaphore("rg-present-semaphore");

  auto blit = create_pass("rg-blit-to-swapchain");
  blit.wait_semaphore(m_rg->m_acquire_semaphore);
  RgTextureId texture =
      blit.read_texture(texture_name, RG_TRANSFER_SRC_TEXTURE);
  RgTextureId backbuffer = blit.write_texture(
      "rg-blitted-backbuffer", "rg-backbuffer", RG_TRANSFER_DST_TEXTURE);

  blit.set_transfer_callback(ren_rg_transfer_callback(RgNoPassData) {
    Handle<Texture> src = rg.get_texture(texture);
    Handle<Texture> dst = rg.get_texture(backbuffer);
    VkImageBlit region = {
        .srcSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .dstSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
    };
    glm::uvec3 src_size = device.get_texture(src).size;
    std::memcpy(&region.srcOffsets[1], &src_size, sizeof(src_size));
    glm::uvec3 dst_size = device.get_texture(dst).size;
    std::memcpy(&region.dstOffsets[1], &dst_size, sizeof(dst_size));
    cmd.blit(src, dst, {region}, VK_FILTER_LINEAR);
  });

  auto present = create_pass("rg-present");
  auto _ = present.write_texture("rg-final-backbuffer", "rg-blitted-backbuffer",
                                 RG_PRESENT_TEXTURE);
  present.set_host_callback(ren_rg_host_callback(RgNoPassData){});
  present.signal_semaphore(m_rg->m_present_semaphore);
}

auto RgBuilder::get_buffer_parent(RgBufferId buffer) -> RgBufferId {
  RgBufferId parent = m_rg->m_buffer_parents[buffer];
  if (m_rg->m_buffer_parents[parent] == buffer) {
    return parent;
  }
  parent = get_buffer_parent(parent);
  m_rg->m_buffer_parents[buffer] = parent;
  return parent;
}

void RgBuilder::build_buffer_disjoint_set() {
  for (RgBufferId &parent : m_rg->m_buffer_parents) {
    parent = get_buffer_parent(parent);
  }
}

auto RgBuilder::get_texture_parent(RgTextureId texture) -> RgTextureId {
  RgTextureId parent = m_rg->m_texture_parents[texture];
  if (m_rg->m_texture_parents[parent] == texture) {
    return parent;
  }
  parent = get_texture_parent(parent);
  m_rg->m_texture_parents[texture] = parent;
  return parent;
}

void RgBuilder::build_texture_disjoint_set() {
  for (RgTextureId &parent : m_rg->m_texture_parents) {
    parent = get_texture_parent(parent);
  }
}

auto RgBuilder::build_pass_schedule() -> Vector<RgPassId> {
  Vector<SmallFlatSet<RgPassId>> successors(m_passes.size());
  Vector<int> predecessor_counts(m_passes.size());

  auto add_edge = [&](RgPassId from, RgPassId to) {
    auto [it, inserted] = successors[from].insert(to);
    if (inserted) {
      ++predecessor_counts[to];
    }
  };

  auto get_buffer_def = [&](RgBufferUseId use) {
    return this->get_buffer_def(m_buffer_uses[use].buffer);
  };

  auto get_buffer_kill = [&](RgBufferUseId use) {
    return this->get_buffer_kill(m_buffer_uses[use].buffer);
  };

  auto get_texture_def = [&](RgTextureUseId use) {
    return this->get_texture_def(m_texture_uses[use].texture);
  };

  auto get_texture_kill = [&](RgTextureUseId use) {
    return this->get_texture_kill(m_texture_uses[use].texture);
  };

  auto is_null_pass = [](RgPassId pass) -> bool { return !pass; };

  SmallVector<RgPassId> dependents;
  auto get_dependants = [&](RgPassId pass_id) -> Span<const RgPassId> {
    const RgPassInfo &pass = m_passes[pass_id];
    dependents.clear();
    // Reads must happen before writes
    dependents.append(pass.read_buffers | map(get_buffer_kill));
    dependents.append(pass.read_textures | map(get_texture_kill));
    ranges::actions::unstable_remove_if(dependents, is_null_pass);
    return dependents;
  };

  SmallVector<RgPassId> dependencies;
  auto get_dependencies = [&](RgPassId pass_id) -> Span<const RgPassId> {
    const RgPassInfo &pass = m_passes[pass_id];
    dependencies.clear();
    // Reads must happen after creation
    dependencies.append(pass.read_buffers | map(get_buffer_def));
    dependencies.append(pass.read_textures | map(get_texture_def));
    // Writes must happen after creation
    dependencies.append(pass.write_buffers | map(get_buffer_def));
    dependencies.append(pass.write_textures | map(get_texture_def));
    ranges::actions::unstable_remove_if(dependencies, is_null_pass);
    return dependencies;
  };

  // Schedule passes whose dependencies were scheduled the longest time ago
  // first
  MinQueue<std::tuple<int, RgPassId>> unscheduled_passes;

  // Build DAG
  for (int idx : range<int>(1, m_passes.size())) {
    RgPassId pass(idx);

    auto predecessors = get_dependencies(pass);
    auto successors = get_dependants(pass);

    for (RgPassId p : predecessors) {
      add_edge(p, pass);
    }

    for (RgPassId s : successors) {
      add_edge(pass, s);
    }

    if (predecessors.empty()) {
      // This is a pass with no dependencies and can be scheduled right away
      unscheduled_passes.push({-1, pass});
    }
  }

  Vector<RgPassId> schedule;
  schedule.reserve(m_passes.size());
  auto &pass_schedule_times = predecessor_counts;

  while (not unscheduled_passes.empty()) {
    auto [dependency_time, pass] = unscheduled_passes.top();
    unscheduled_passes.pop();

    int time = schedule.size();
    assert(dependency_time < time);
    schedule.push_back(pass);
    pass_schedule_times[pass] = time;

    for (RgPassId s : successors[pass]) {
      if (--predecessor_counts[s] == 0) {
        int max_dependency_time = -1;
        Span<const RgPassId> dependencies = get_dependencies(s);
        if (not dependencies.empty()) {
          max_dependency_time = ranges::max(dependencies | map([&](RgPassId d) {
                                              return pass_schedule_times[d];
                                            }));
        }
        unscheduled_passes.push({max_dependency_time, s});
      }
    }
  }

  return schedule;
}

void RgBuilder::dump_pass_schedule(Span<const RgPassId> schedule) const {
#if REN_RG_DEBUG
  fmt::println(stderr, "Scheduled passes:");
  for (RgPassId pass_id : schedule) {
    const RgPassInfo &pass = m_passes[pass_id];
    fmt::println(stderr, "  * {}", m_rg->m_pass_names[pass_id]);
    if (not pass.read_buffers.empty()) {
      fmt::println(stderr, "    Reads buffers:");
      for (RgBufferUseId use : pass.read_buffers) {
        fmt::println(stderr, "      - {}",
                     m_buffer_names[m_buffer_uses[use].buffer]);
      }
    }
    if (not pass.write_buffers.empty()) {
      fmt::println(stderr, "    Writes buffers:");
      for (RgBufferUseId use : pass.write_buffers) {
        RgBufferId src_buffer = m_buffer_uses[use].buffer;
        RgBufferId dst_buffer = m_buffer_children[src_buffer];
        fmt::println(stderr, "      - {} -> {}", m_buffer_names[src_buffer],
                     m_buffer_names[dst_buffer]);
      }
    }
    if (not pass.read_textures.empty()) {
      fmt::println(stderr, "    Reads textures:");
      for (RgTextureUseId use : pass.read_textures) {
        fmt::println(stderr, "      - {}",
                     m_texture_names[m_texture_uses[use].texture]);
      }
    }
    if (not pass.write_textures.empty()) {
      fmt::println(stderr, "    Writes textures:");
      for (RgTextureUseId use : pass.write_textures) {
        RgTextureId src_texture = m_texture_uses[use].texture;
        RgTextureId dst_texture = m_texture_children[src_texture];
        fmt::println(stderr, "      - {} -> {}", m_texture_names[src_texture],
                     m_texture_names[dst_texture]);
      }
    }
    fmt::println(stderr, "");
  }
#endif
}

auto RgBuilder::passes() const {
  return range<int>(1, m_passes.size()) |
         map([](int idx) { return RgPassId(idx); });
}

void RgBuilder::create_resources(Span<const RgPassId> schedule) {
  {
    ren_rg_time_region("cr-clear-arena");
    m_rg->m_arena.clear();
  }

  std::array<VkBufferUsageFlags, NUM_BUFFER_HEAPS> heap_usage_flags = {};

  RenRgTimeCounter(update_buffer_heap_usage_flags_counter,
                   "cr-update-buffer-heap-usage-flags");
  RenRgTimeCounter(update_texture_usage_flags_counter,
                   "cr-update-texture-usage-flags");

  for (RgPassId pass_id : schedule) {
    const RgPassInfo &pass = m_passes[pass_id];

    auto update_buffer_heap_usage_flags = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      RgPhysicalBufferId physical_buffer_id(m_rg->m_buffer_parents[use.buffer]);
      const RgBufferDesc &desc = m_buffer_descs[physical_buffer_id];
      auto heap = i32(desc.heap);
      heap_usage_flags[heap] |= get_buffer_usage_flags(use.usage.access_mask);
    };

    {
      ren_rg_inc_time_counter(update_buffer_heap_usage_flags_counter);
      ranges::for_each(pass.read_buffers, update_buffer_heap_usage_flags);
      ranges::for_each(pass.write_buffers, update_buffer_heap_usage_flags);
    }

    auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_texture_uses[use_id];
      RgPhysicalTextureId physical_texture_id(
          m_rg->m_texture_parents[use.texture]);
      m_texture_descs.get(physical_texture_id).map([&](RgTextureDesc &desc) {
        desc.usage |= get_texture_usage_flags(use.usage.access_mask);
      });
    };

    {
      ren_rg_inc_time_counter(update_texture_usage_flags_counter);
      ranges::for_each(pass.read_textures, update_texture_usage_flags);
      ranges::for_each(pass.write_textures, update_texture_usage_flags);
    }
  }

  {
    ren_rg_dump_time_counter(update_buffer_heap_usage_flags_counter);
    ren_rg_time_region("cr-update-buffer-descs");
    m_rg->m_buffers.resize(m_rg->m_buffer_parents.size());
    m_rg->m_buffer_descs.clear();
    for (const auto &[physical_buffer_id, desc] : m_buffer_descs) {
      m_rg->m_buffer_descs.insert(physical_buffer_id,
                                  {.heap = desc.heap, .size = desc.size});
    }
    m_rg->m_heap_buffer_usage_flags = heap_usage_flags;
    m_rg->m_heap_buffers = {};
  }

  {
    ren_rg_dump_time_counter(update_texture_usage_flags_counter);
    ren_rg_time_region("cr-create-textures");
    m_rg->m_textures.resize(m_rg->m_texture_parents.size());
    m_rg->m_tex_alloc.clear();
    m_rg->m_storage_texture_descriptors.resize(m_rg->m_textures.size());
    m_rg->m_texture_instance_counts.clear();
    for (const auto &[base_texture_id, desc] : m_texture_descs) {
      VkImageUsageFlags usage = desc.usage;
      if (desc.num_temporal_layers > 1 and desc.clear) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      }
      u32 num_instances = PIPELINE_DEPTH + desc.num_temporal_layers - 1;
      for (i32 i = 0; i < num_instances; ++i) {
        RgPhysicalTextureId texture_id(base_texture_id + i);
        Handle<Texture> htexture = m_rg->m_arena.create_texture({
            .name = fmt::format("Render graph texture {}", i32(texture_id)),
            .type = desc.type,
            .format = desc.format,
            .usage = usage,
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
            .num_mip_levels = desc.num_mip_levels,
            .num_array_layers = desc.num_array_layers,
        });
        m_rg->m_textures[texture_id] = htexture;
        StorageTextureId storage_descriptor;
        if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
          storage_descriptor = m_rg->m_tex_alloc.allocate_storage_texture(
              g_device->get_texture_view(htexture));
        }
        m_rg->m_storage_texture_descriptors[texture_id] = storage_descriptor;
      }
      m_rg->m_texture_instance_counts.insert(base_texture_id, num_instances);
    }
  }

  {
    ren_rg_time_region("cr-create-semaphores");
    m_rg->m_semaphores.resize(m_semaphore_ids.size());
    for (auto &&[index, semaphore] : m_rg->m_acquire_semaphores | enumerate) {
      semaphore = m_rg->m_arena.create_semaphore({
          .name =
              fmt::format("Render graph swapchain acquire semaphore {}", index),
      });
    }
    for (auto &&[index, semaphore] : m_rg->m_present_semaphores | enumerate) {
      semaphore = m_rg->m_arena.create_semaphore({
          .name =
              fmt::format("Render graph swapchain present semaphore {}", index),
      });
    }
  }
}

void RgBuilder::fill_pass_runtime_info(Span<const RgPassId> schedule) {
  m_rg->m_passes.resize(schedule.size());
  m_rg->m_color_attachments.clear();
  m_rg->m_depth_stencil_attachments.clear();
  for (auto [idx, pass_id] : schedule | enumerate) {
    m_rg->m_passes[idx].pass = pass_id;
    m_passes[pass_id].type.visit(OverloadSet{
        [&](Monostate) {
#if REN_RG_DEBUG
          unreachable("Callback for pass {} has not been set!",
                      m_rg->m_pass_names[pass_id]);
#else
          unreachable("Callback for pass {} has not been set!", u32(pass_id));
#endif
        },
        [&](RgHostPassInfo &host_pass) {
          m_rg->m_passes[idx].type = RgHostPass{.cb = std::move(host_pass.cb)};
        },
        [&](RgGraphicsPassInfo &graphics_pass) {
          m_rg->m_passes[idx].type = RgGraphicsPass{
              .num_color_attachments =
                  u32(graphics_pass.color_attachments.size()),
              .has_depth_attachment =
                  graphics_pass.depth_stencil_attachment.has_value(),
              .cb = std::move(graphics_pass.cb),
          };
          m_rg->m_color_attachments.append(graphics_pass.color_attachments);
          graphics_pass.depth_stencil_attachment.map(
              [&](const RgDepthStencilAttachment &att) {
                m_rg->m_depth_stencil_attachments.push_back(att);
              });
        },
        [&](RgComputePassInfo &compute_pass) {
          m_rg->m_passes[idx].type =
              RgComputePass{.cb = std::move(compute_pass.cb)};
        },
        [&](RgTransferPassInfo &transfer_pass) {
          m_rg->m_passes[idx].type =
              RgTransferPass{.cb = std::move(transfer_pass.cb)};
        },
    });
  }
}

void RgBuilder::place_barriers_and_semaphores(
    Span<const RgPassId> schedule, Vector<RgClearTexture> &clear_textures) {
  HashMap<RgPhysicalBufferId, RgBufferUsage>
      buffer_after_write_hazard_src_states;
  HashMap<RgPhysicalBufferId, VkPipelineStageFlags2>
      buffer_after_read_hazard_src_states;

  struct TextureUsageWithoutLayout {
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  };

  HashMap<RgPhysicalTextureId, TextureUsageWithoutLayout>
      texture_after_write_hazard_src_states;
  HashMap<RgPhysicalTextureId, VkPipelineStageFlags2>
      texture_after_read_hazard_src_states;
  HashMap<RgPhysicalTextureId, VkImageLayout> texture_layouts;
  HashMap<RgPhysicalTextureId, usize> texture_deferred_barriers;

  m_rg->m_memory_barriers.clear();
  m_rg->m_texture_barriers.clear();
  m_rg->m_wait_semaphores.clear();
  m_rg->m_signal_semaphores.clear();

  for (auto [idx, pass_id] : schedule | enumerate) {
    const RgPassInfo &pass = m_passes[pass_id];

    usize old_memory_barrier_count = m_rg->m_memory_barriers.size();
    usize old_texture_barrier_count = m_rg->m_texture_barriers.size();

    // TODO: merge separate barriers together if it doesn't change how
    // synchronization happens
    auto maybe_place_barrier_for_buffer = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      RgPhysicalBufferId physical_buffer(m_rg->m_buffer_parents[use.buffer]);

      VkPipelineStageFlags2 dst_stage_mask = use.usage.stage_mask;
      VkAccessFlags2 dst_access_mask = use.usage.access_mask;

      // Don't need a barrier for host-only accesses
      bool is_host_only_access = dst_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_host_only_access) {
        assert(dst_access_mask == VK_ACCESS_2_NONE);
        return;
      }

      VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
      VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

      if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
        RgBufferUsage &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        // Reset the source stage mask that the next WAR hazard will use
        src_stage_mask =
            std::exchange(buffer_after_read_hazard_src_states[physical_buffer],
                          VK_PIPELINE_STAGE_2_NONE);
        // If this is a WAR hazard, need to wait for all previous
        // reads to finish. The previous write's memory has already been made
        // available by previous RAW barriers, so it only needs to be made
        // visible
        // FIXME: According to the Vulkan spec, WAR hazards require only an
        // execution barrier
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // No reads were performed between this write and the previous one so
          // this is a WAW hazard. Need to wait for the previous write to finish
          // and make its memory available and visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // hazards will use
        after_write_state.stage_mask = dst_stage_mask;
        // Read accesses are redundant for source access mask
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;
      } else {
        // This is a RAW hazard. Need to wait for the previous write to finish
        // and make it's memory available and visible
        // TODO/FIXME: all RAW barriers should be merged, since if they are
        // issued separately they might cause the cache to be flushed multiple
        // times
        const RgBufferUsage &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        src_stage_mask = after_write_state.stage_mask;
        src_access_mask = after_write_state.access_mask;
        // Update the source stage mask that the next WAR hazard will use
        buffer_after_read_hazard_src_states[physical_buffer] |= dst_stage_mask;
      }

      // First barrier isn't required and can be skipped
      bool is_first_access = src_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_first_access) {
        assert(src_access_mask == VK_PIPELINE_STAGE_2_NONE);
        return;
      }

      m_rg->m_memory_barriers.push_back({
          .src_stage_mask = src_stage_mask,
          .src_access_mask = src_access_mask,
          .dst_stage_mask = dst_stage_mask,
          .dst_access_mask = dst_access_mask,
      });
    };

    ranges::for_each(pass.read_buffers, maybe_place_barrier_for_buffer);
    ranges::for_each(pass.write_buffers, maybe_place_barrier_for_buffer);

    auto maybe_place_barrier_for_texture = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_texture_uses[use_id];
      RgPhysicalTextureId physical_texture(
          m_rg->m_texture_parents[use.texture]);

      VkPipelineStageFlags2 dst_stage_mask = use.usage.stage_mask;
      VkAccessFlags2 dst_access_mask = use.usage.access_mask;
      VkImageLayout dst_layout = use.usage.layout;
      assert(dst_layout);
      if (dst_layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        assert(dst_stage_mask);
        assert(dst_access_mask);
      }

      VkImageLayout &src_layout = texture_layouts[physical_texture];

      if (dst_layout == src_layout) {
        // Only a memory barrier is required if layout doesn't change
        // NOTE: this code is copy-pasted from above

        VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

        if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
          TextureUsageWithoutLayout &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = std::exchange(
              texture_after_read_hazard_src_states[physical_texture],
              VK_PIPELINE_STAGE_2_NONE);
          if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          after_write_state.stage_mask = dst_stage_mask;
          after_write_state.access_mask =
              dst_access_mask & WRITE_ONLY_ACCESS_MASK;
        } else {
          const TextureUsageWithoutLayout &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
          texture_after_read_hazard_src_states[physical_texture] |=
              dst_stage_mask;
        }

        assert(src_stage_mask != VK_PIPELINE_STAGE_2_NONE);

        m_rg->m_memory_barriers.push_back({
            .src_stage_mask = src_stage_mask,
            .src_access_mask = src_access_mask,
            .dst_stage_mask = dst_stage_mask,
            .dst_access_mask = dst_access_mask,
        });
      } else {
        // Need an image barrier to change layout.
        // Layout transitions are read-write operations, so only to take care of
        // WAR and WAW hazards in this case

        TextureUsageWithoutLayout &after_write_state =
            texture_after_write_hazard_src_states[physical_texture];
        // If this is a WAR hazard, must wait for all previous reads to finish
        // and make the layout transition's memory available.
        // Also reset the source stage mask that the next WAR barrier will use
        VkPipelineStageFlags2 src_stage_mask = std::exchange(
            texture_after_read_hazard_src_states[physical_texture],
            VK_PIPELINE_STAGE_2_NONE);
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // If there were no reads between this write and the previous one,
          // need to wait for the previous write to finish and make it's memory
          // available and the layout transition's memory visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // barriers will use
        after_write_state.stage_mask = dst_stage_mask;
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;

        // If this is the first access to this texture, need to patch stage and
        // access masks after all barriers have been placed to account for
        // resource reuse and/or temporal resources
        bool is_first_access = src_layout == VK_IMAGE_LAYOUT_UNDEFINED;
        if (is_first_access) {
          assert(src_stage_mask == VK_PIPELINE_STAGE_2_NONE);
          assert(src_access_mask == VK_ACCESS_2_NONE);
          texture_deferred_barriers.insert(physical_texture,
                                           m_rg->m_texture_barriers.size());
        }

        m_rg->m_texture_barriers.push_back({
            .texture = physical_texture,
            .src_stage_mask = src_stage_mask,
            .src_access_mask = src_access_mask,
            .src_layout = src_layout,
            .dst_stage_mask = dst_stage_mask,
            .dst_access_mask = dst_access_mask,
            .dst_layout = dst_layout,
        });

        // Update current layout
        src_layout = dst_layout;
      }
    };

    ranges::for_each(pass.read_textures, maybe_place_barrier_for_texture);
    ranges::for_each(pass.write_textures, maybe_place_barrier_for_texture);

    usize new_memory_barrier_count = m_rg->m_memory_barriers.size();
    usize new_texture_barrier_count = m_rg->m_texture_barriers.size();

    m_rg->m_passes[idx].num_memory_barriers =
        new_memory_barrier_count - old_memory_barrier_count;
    m_rg->m_passes[idx].num_texture_barriers =
        new_texture_barrier_count - old_texture_barrier_count;

    auto get_semaphore_signal = [&](RgSemaphoreSignalId signal_id) {
      return m_semaphore_signals[signal_id];
    };

    m_rg->m_wait_semaphores.append(pass.wait_semaphores |
                                   map(get_semaphore_signal));
    m_rg->m_passes[idx].num_wait_semaphores = pass.wait_semaphores.size();

    m_rg->m_signal_semaphores.append(pass.signal_semaphores |
                                     map(get_semaphore_signal));
    m_rg->m_passes[idx].num_signal_semaphores = pass.signal_semaphores.size();
  }

  for (auto [base_physical_texture, num_instances] :
       m_rg->m_texture_instance_counts) {
    const RgTextureDesc &desc = m_texture_descs[base_physical_texture];
    u32 num_temporal_layers = desc.num_temporal_layers;
    {
      usize barrier_idx = texture_deferred_barriers[base_physical_texture];
      RgTextureBarrier &barrier = m_rg->m_texture_barriers[barrier_idx];
      // Don't need to make memory visible since it will be overwritten anyway
      // TODO: Need to check Vulkan spec to see if this is true
      barrier.dst_access_mask = VK_ACCESS_2_NONE;
      // Patch source stage and access masks of newest temporal layer if texture
      // instance count is less than the pipeline depth.
      // If that's the case, the oldest texture might still be in use by the
      // device when this frame is submitted, so it can't used without an
      // execution an memory barrier.
      // If there are as many textures as there are frames in flight (or more),
      // the oldest texture can be reused without a barrier, as its frame will
      // be done (its semaphore waited on) before this frame is submitted
      // TODO: Need to check Vulkan spec that memory is made available after
      // waiting on a timeline semaphore for the host
      if (num_instances < PIPELINE_DEPTH + num_temporal_layers - 1) {
        RgPhysicalTextureId prev_physical_texture(base_physical_texture +
                                                  num_temporal_layers - 1);
        assert(texture_after_read_hazard_src_states.contains(
            prev_physical_texture));
        VkPipelineStageFlags2 src_stage_mask =
            texture_after_read_hazard_src_states[prev_physical_texture];
        VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          assert(texture_after_write_hazard_src_states.contains(
              prev_physical_texture));
          const TextureUsageWithoutLayout &prev_after_write_state =
              texture_after_write_hazard_src_states[prev_physical_texture];
          src_stage_mask = prev_after_write_state.stage_mask;
          src_access_mask = prev_after_write_state.access_mask;
        }
        barrier.src_stage_mask = src_stage_mask;
        barrier.src_access_mask = src_access_mask;
      }
    }
    for (i32 i = 1; i < num_temporal_layers; ++i) {
      RgPhysicalTextureId physical_texture(base_physical_texture + i);
      // Patch first barrier with transition from temporal slice that is 1 step
      // into the future
      RgPhysicalTextureId prev_physical_texture(base_physical_texture + i - 1);
      // If this is a WAR hazard, only need to wait for previous reads to finish
      // and make the layout transition's memory visible
      assert(
          texture_after_read_hazard_src_states.contains(prev_physical_texture));
      VkPipelineStageFlags2 src_stage_mask =
          texture_after_read_hazard_src_states[prev_physical_texture];
      VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
      VkImageLayout src_layout = texture_layouts[prev_physical_texture];
      if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
        // If this is a WAW hazard, need to wait for previous write to finish,
        // make its memory available and the layout transition's memory visible
        assert(texture_after_write_hazard_src_states.contains(
            prev_physical_texture));
        const TextureUsageWithoutLayout &prev_after_write_state =
            texture_after_write_hazard_src_states[prev_physical_texture];
        src_stage_mask = prev_after_write_state.stage_mask;
        src_access_mask = prev_after_write_state.access_mask;
      }
      usize barrier_idx = texture_deferred_barriers[physical_texture];
      RgTextureBarrier &barrier = m_rg->m_texture_barriers[barrier_idx];
      barrier.src_stage_mask = src_stage_mask;
      barrier.src_access_mask = src_access_mask;
      assert(texture_layouts.contains(prev_physical_texture));
      barrier.src_layout = src_layout;

      RgClearTexture clear_texture = {
          .texture = m_rg->m_textures[physical_texture],
          .dst_stage_mask = src_stage_mask,
          .dst_layout = src_layout,
      };

      desc.clear.visit(OverloadSet{
          [](Monostate) {},
          [&](const glm::vec4 &clear_color) {
            clear_texture.clear.color = clear_color;
            clear_textures.push_back(clear_texture);
          },
          [&](const VkClearDepthStencilValue &clear_depth_stencil) {
            clear_texture.clear.depth_stencil = clear_depth_stencil;
            clear_textures.push_back(clear_texture);
          },
      });
    }
  }
}

void RgBuilder::clear_temporal_textures(
    CommandAllocator &cmd_alloc,
    Span<const RgClearTexture> clear_textures) const {
  if (clear_textures.empty()) {
    return;
  }

  VkCommandBuffer cmd_buffer = [&] {
    ren_rg_time_region("ctt-alloc-command-buffer");
    return cmd_alloc.allocate();
  }();
  {
    CommandRecorder rec(cmd_buffer);

    Vector<VkImageMemoryBarrier2> barriers;
    {
      ren_rg_time_region("ctt-gen-barriers-1");
      barriers.reserve(clear_textures.size());
      for (const RgClearTexture &clear_texture : clear_textures) {
        const Texture &texture = g_device->get_texture(clear_texture.texture);
        barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = texture.image,
            .subresourceRange =
                {
                    .aspectMask = getVkImageAspectFlags(texture.format),
                    .levelCount = texture.num_mip_levels,
                    .layerCount = texture.num_array_layers,
                },
        });
      }
    }
    {
      ren_rg_time_region("ctt-record-barriers-1");
      rec.pipeline_barrier({}, barriers);
    }

    {
      ren_rg_time_region("ctt-record-clears");
      for (const RgClearTexture &clear_texture : clear_textures) {
        const Texture &texture = g_device->get_texture(clear_texture.texture);
        VkImageAspectFlags aspect_mask = getVkImageAspectFlags(texture.format);
        if (aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) {
          rec.clear_texture(clear_texture.texture, clear_texture.clear.color);
        } else {
          assert(aspect_mask &
                 (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
          rec.clear_texture(clear_texture.texture,
                            clear_texture.clear.depth_stencil);
        }
      }
    }

    {
      ren_rg_time_region("ctt-gen-barriers-2");
      barriers.clear();
      for (const RgClearTexture &clear_texture : clear_textures) {
        const Texture &texture = g_device->get_texture(clear_texture.texture);
        barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = clear_texture.dst_stage_mask,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = clear_texture.dst_layout,
            .image = texture.image,
            .subresourceRange =
                {
                    .aspectMask = getVkImageAspectFlags(texture.format),
                    .levelCount = texture.num_mip_levels,
                    .layerCount = texture.num_array_layers,
                },
        });
      }
    }
    {
      ren_rg_time_region("ctt-record-barriers-2");
      rec.pipeline_barrier({}, barriers);
    }
  }

  {
    ren_rg_time_region("ctt-submit");
    g_device->graphicsQueueSubmit({{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd_buffer,
    }});
  }
}

void RgBuilder::build(CommandAllocator &cmd_alloc) {
  ren_rg_dump_time_counter(m_create_pass_counter);
  ren_rg_dump_time_counter(m_host_callback_counter);
  ren_rg_dump_time_counter(m_graphics_callback_counter);
  ren_rg_dump_time_counter(m_compute_callback_counter);
  ren_rg_dump_time_counter(m_transfer_callback_counter);
  ren_rg_dump_time_counter(m_update_callback_counter);

  ren_rg_dump_time_counter(m_create_buffer_counter);
  ren_rg_dump_time_counter(m_read_buffer_counter);
  ren_rg_dump_time_counter(m_write_buffer_counter);

  ren_rg_dump_time_counter(m_create_texture_counter);
  ren_rg_dump_time_counter(m_read_texture_counter);
  ren_rg_dump_time_counter(m_write_texture_counter);
  ren_rg_dump_time_counter(m_color_attachment_counter);
  ren_rg_dump_time_counter(m_depth_attachment_counter);

  ren_rg_dump_time_counter(m_wait_semaphore_counter);
  ren_rg_dump_time_counter(m_signal_semaphore_counter);

  ren_rg_dump_time_counter(m_present_counter);

  {
    ren_rg_time_region("build-buffer-disjoint-set");
    build_buffer_disjoint_set();
  }

  {
    ren_rg_time_region("setup-backbuffer");
    RgTextureId backbuffer = m_rg->m_texture_ids["rg-backbuffer"];
    assert(backbuffer);
    m_rg->m_texture_parents[backbuffer] = backbuffer;
    m_rg->m_backbuffer = RgPhysicalTextureId(backbuffer);
  }

  {
    ren_rg_time_region("build-texture-disjoint-set");
    build_texture_disjoint_set();
  }

  Vector<RgPassId> schedule = [&] {
    ren_rg_time_region("schedule-passes");
    return build_pass_schedule();
  }();
  dump_pass_schedule(schedule);

  {
    ren_rg_time_region("create-resources");
    create_resources(schedule);
  }

  {
    ren_rg_time_region("fill-pass-runtime-info");
    fill_pass_runtime_info(schedule);
  }

  Vector<RgClearTexture> clear_textures;
  {
    ren_rg_time_region("place-barriers");
    place_barriers_and_semaphores(schedule, clear_textures);
  }
  {
    ren_rg_time_region("clear-temporal-textures");
    clear_temporal_textures(cmd_alloc, clear_textures);
  }
}

} // namespace ren
