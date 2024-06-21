#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "RenderGraph.hpp"
#include "Support/FlatSet.hpp"
#include "Support/Math.hpp"
#include "Support/PriorityQueue.hpp"
#include "Support/Views.hpp"
#include "glsl/DevicePtr.h"

namespace ren {

namespace {

auto get_buffer_usage_flags(VkAccessFlags2 accesses) -> VkBufferUsageFlags {
  ren_assert((accesses & VK_ACCESS_2_MEMORY_READ_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_MEMORY_WRITE_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_SHADER_READ_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_SHADER_WRITE_BIT) == 0);

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
  ren_assert((accesses & VK_ACCESS_2_MEMORY_READ_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_MEMORY_WRITE_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_SHADER_READ_BIT) == 0);
  ren_assert((accesses & VK_ACCESS_2_SHADER_WRITE_BIT) == 0);

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
  m_rg = &rg;

#if REN_RG_DEBUG
  m_rg->m_pass_names = {{}};
#endif

  m_rg->m_color_attachments.clear();
  m_rg->m_depth_stencil_attachments.clear();

  m_rg->m_buffer_uses.clear();
  m_rg->m_texture_uses.clear();
  m_rg->m_semaphore_signals.clear();

  m_rg->m_physical_variables = {{}};
  m_rg->m_variables.clear();

  m_rg->m_physical_buffers = {{}};
  m_rg->m_buffers.clear();

  m_rg->m_physical_textures = {{}};
  m_rg->m_textures.clear();
  m_rg->m_external_textures.clear();
  m_rg->m_texture_temporal_layer_count.clear();
  m_rg->m_texture_usages.clear();
  m_rg->m_tex_alloc.clear();
  m_rg->m_storage_texture_descriptors.clear();

  m_rg->m_semaphores = {{}};

  m_rg->m_arena.clear();
}

auto RgBuilder::create_pass(RgPassCreateInfo &&create_info) -> RgPassBuilder {
  RgPassId id(m_passes.size());
  m_passes.emplace_back();
#if REN_RG_DEBUG
  m_rg->m_pass_names.push_back(std::move(create_info.name));
#endif
  return RgPassBuilder(id, *this);
}

auto RgBuilder::get_variable_def(RgGenericVariableId variable) const
    -> RgPassId {
  return m_variable_defs[variable];
}

auto RgBuilder::get_variable_kill(RgGenericVariableId variable) const
    -> RgPassId {
  return m_variable_kills[variable];
}

auto RgBuilder::create_virtual_variable(RgPassId pass, RgDebugName name,
                                        RgGenericVariableId parent)
    -> RgGenericVariableId {
  RgPhysicalVariableId physical_variable;
  if (parent) {
    physical_variable = m_rg->m_physical_variables[parent];
  } else {
    ren_assert(!pass);
    physical_variable = RgPhysicalVariableId(m_rg->m_variables.size());
    m_rg->m_variables.emplace_back();
  }

  RgGenericVariableId variable(m_rg->m_physical_variables.size());

  m_rg->m_physical_variables.push_back(physical_variable);
  m_variable_defs.push_back(pass);
  m_variable_kills.emplace_back();

  if (parent) {
    ren_assert(pass);
    m_variable_kills[parent] = pass;
  }

#if REN_RG_DEBUG
  if (not name.empty()) {
    m_variable_names[variable] = std::move(name);
  }
  m_variable_children.emplace_back();
  if (parent) {
    ren_assert_msg(!m_variable_children[parent],
                   "Render graph variables can only be written once");
    m_variable_children[parent] = variable;
  }
#endif

  return variable;
}

auto RgBuilder::read_variable(RgPassId pass, RgGenericVariableId variable)
    -> RgGenericVariableToken {
  ren_assert(variable);
  m_passes[pass].read_variables.push_back(variable);
  return RgGenericVariableToken(variable);
}

auto RgBuilder::write_variable(RgPassId pass, RgDebugName name,
                               RgGenericVariableId src)
    -> std::tuple<RgGenericVariableId, RgGenericVariableToken> {
  ren_assert(src);
  RgGenericVariableId dst = create_virtual_variable(pass, std::move(name), src);
  m_passes[pass].write_variables.push_back(src);
  return {dst, RgGenericVariableToken(src)};
}

auto RgBuilder::add_buffer_use(RgBufferId buffer,
                               const RgBufferUsage &usage) -> RgBufferUseId {
  ren_assert(buffer);
  RgBufferUseId id(m_rg->m_buffer_uses.size());
  m_rg->m_buffer_uses.push_back({
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

auto RgBuilder::create_virtual_buffer(RgPassId pass, RgDebugName name,
                                      RgBufferId parent) -> RgBufferId {
  RgPhysicalBufferId physical_buffer;
  if (parent) {
    physical_buffer = m_rg->m_physical_buffers[parent];
  } else {
    ren_assert(!pass);
    physical_buffer = RgPhysicalBufferId(m_rg->m_buffers.size());
    for (auto i : range<usize>(PIPELINE_DEPTH)) {
      m_rg->m_buffers.emplace_back();
    }
  }

  RgBufferId buffer(m_rg->m_physical_buffers.size());

  m_rg->m_physical_buffers.push_back(physical_buffer);
  m_buffer_defs.push_back(pass);
  m_buffer_kills.emplace_back();

  if (parent) {
    ren_assert(pass);
    m_buffer_kills[parent] = pass;
  }

#if REN_RG_DEBUG
  if (not name.empty()) {
    m_buffer_names[buffer] = std::move(name);
  }
  m_buffer_children.emplace_back();
  if (parent) {
    ren_assert_msg(!m_buffer_children[parent],
                   "Render graph buffers can only be written once");
    m_buffer_children[parent] = buffer;
  }
#endif

  return buffer;
}

auto RgBuilder::create_buffer(RgBufferCreateInfo &&create_info) -> RgBufferId {

  RgBufferId buffer = create_virtual_buffer(
      RgPassId(), std::move(create_info.name), RgBufferId());
  m_buffer_descs[m_rg->m_physical_buffers[buffer]] = {
      .heap = create_info.heap,
      .size = create_info.size,
  };
  return buffer;
}

auto RgBuilder::read_buffer(RgPassId pass, RgBufferId buffer,
                            const RgBufferUsage &usage) -> RgBufferToken {
  ren_assert(buffer);
  m_passes[pass].read_buffers.push_back(add_buffer_use(buffer, usage));
  return RgBufferToken(buffer);
}

auto RgBuilder::write_buffer(RgPassId pass, RgDebugName name, RgBufferId src,
                             const RgBufferUsage &usage)
    -> std::tuple<RgBufferId, RgBufferToken> {
  ren_assert(src);
  RgBufferId dst = create_virtual_buffer(pass, std::move(name), src);
  m_passes[pass].write_buffers.push_back(add_buffer_use(src, usage));
  return {dst, RgBufferToken(src)};
}

auto RgBuilder::add_texture_use(RgTextureId texture,
                                const RgTextureUsage &usage) -> RgTextureUseId {
  ren_assert(texture);
  RgTextureUseId id(m_rg->m_texture_uses.size());
  m_rg->m_texture_uses.push_back({
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

auto RgBuilder::create_virtual_texture(RgPassId pass, RgDebugName name,
                                       RgTextureId parent,
                                       u32 num_temporal_layers) -> RgTextureId {
  ren_assert(num_temporal_layers > 0);
  RgPhysicalTextureId physical_texture;
  if (parent) {
    ren_assert(num_temporal_layers == 1);
    physical_texture = m_rg->m_physical_textures[parent];
  } else {
    ren_assert(!pass);
    physical_texture = RgPhysicalTextureId(m_rg->m_textures.size());
    usize num_textures = m_rg->m_textures.size() + num_temporal_layers;
    m_rg->m_textures.resize(num_textures);
    m_rg->m_texture_usages.resize(num_textures);
    m_rg->m_external_textures.resize(num_textures);
    m_rg->m_storage_texture_descriptors.resize(num_textures);
    m_rg->m_texture_temporal_layer_count.resize(num_textures);
    m_rg->m_texture_temporal_layer_count[physical_texture] =
        num_temporal_layers;
  }

  RgTextureId texture(m_rg->m_physical_textures.size());
  for (auto i : range<usize>(num_temporal_layers)) {
    m_rg->m_physical_textures.push_back(
        RgPhysicalTextureId(physical_texture + i));
    m_texture_defs.push_back(pass);
    m_texture_kills.emplace_back();
  }

  if (parent) {
    ren_assert(pass);
    m_texture_kills[parent] = pass;
  }

#if REN_RG_DEBUG
  if (not name.empty()) {
    for (auto i : range<usize>(1, num_temporal_layers)) {
      m_texture_names[RgTextureId(texture + i)] = fmt::format("{}#{}", name, i);
    }
    m_texture_names[texture] = std::move(name);
  }
  for (auto i : range<usize>(num_temporal_layers)) {
    m_texture_children.emplace_back();
    m_texture_parents.push_back(parent);
  }
  if (parent) {
    ren_assert_msg(!m_texture_children[parent],
                   "Render graph textures can only be written once");
    m_texture_children[parent] = texture;
  }
#endif

  return texture;
}

auto RgBuilder::create_texture(RgTextureCreateInfo &&create_info)
    -> RgTextureId {
  RgTextureId texture =
      create_virtual_texture(RgPassId(), std::move(create_info.name),
                             RgTextureId(), create_info.num_temporal_layers);
  RgPhysicalTextureId physical_texture(m_rg->m_physical_textures[texture]);
  m_texture_descs[physical_texture] = {
      .type = create_info.type,
      .format = create_info.format,
      .usage = get_texture_usage_flags(create_info.init_usage.access_mask),
      .width = create_info.width,
      .height = create_info.height,
      .depth = create_info.depth,
      .num_mip_levels = create_info.num_mip_levels,
      .num_array_layers = create_info.num_array_layers,
  };
  if (create_info.init_cb) {
    ren_assert(create_info.num_temporal_layers > 1);
    m_texture_init_callbacks[physical_texture] = std::move(create_info.init_cb);
    for (auto l : range<usize>(1, create_info.num_temporal_layers)) {
      m_rg->m_texture_usages[physical_texture + l] = create_info.init_usage;
    }
  }
  return texture;
}

auto RgBuilder::create_external_texture(
    RgExternalTextureCreateInfo &&create_info) -> RgTextureId {
  RgTextureId texture = create_virtual_texture(
      RgPassId(), std::move(create_info.name), RgTextureId());
  RgPhysicalTextureId physical_texture(m_rg->m_physical_textures[texture]);
  m_rg->m_external_textures[physical_texture] = true;
  return texture;
}

auto RgBuilder::read_texture(RgPassId pass, RgTextureId texture,
                             const RgTextureUsage &usage,
                             u32 temporal_layer) -> RgTextureToken {
  ren_assert(texture);
#if REN_RG_DEBUG
  ren_assert_msg(
      temporal_layer == 0 or !m_texture_parents[texture],
      "Only the first declaration of a temporal texture can be used to "
      "read a previous temporal layer");
#endif
  RgPhysicalTextureId physical_texture = m_rg->m_physical_textures[texture];
  ren_assert_msg(temporal_layer <
                     m_rg->m_texture_temporal_layer_count[physical_texture],
                 "Temporal layer index out of range");
  m_passes[pass].read_textures.push_back(
      add_texture_use(RgTextureId(texture + temporal_layer), usage));
  return RgTextureToken(texture + temporal_layer);
}

auto RgBuilder::write_texture(RgPassId pass, RgDebugName name, RgTextureId src,
                              const RgTextureUsage &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  ren_assert(src);
  RgTextureId dst = create_virtual_texture(pass, std::move(name), src);
  m_passes[pass].write_textures.push_back(add_texture_use(src, usage));
  return {dst, RgTextureToken(src)};
}

auto RgBuilder::create_external_semaphore(RgSemaphoreCreateInfo &&create_info)
    -> RgSemaphoreId {
  ren_assert(create_info.type == VK_SEMAPHORE_TYPE_BINARY);
  RgSemaphoreId semaphore(m_rg->m_semaphores.size());
  m_rg->m_semaphores.emplace_back();
#if REN_RG_DEBUG
  m_semaphore_names.push_back(std::move(create_info.name));
#endif
  return semaphore;
}

auto RgBuilder::add_semaphore_signal(RgSemaphoreId semaphore,
                                     VkPipelineStageFlags2 stage_mask,
                                     u64 value) -> RgSemaphoreSignalId {
  RgSemaphoreSignalId id(m_rg->m_semaphore_signals.size());
  m_rg->m_semaphore_signals.push_back({
      .semaphore = semaphore,
      .stage_mask = stage_mask,
      .value = value,
  });
  return id;
}

void RgBuilder::wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                               VkPipelineStageFlags2 stage_mask, u64 value) {
  m_passes[pass].wait_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
}

void RgBuilder::signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                                 VkPipelineStageFlags2 stage_mask, u64 value) {
  m_passes[pass].signal_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
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

  auto get_variable_def = [&](RgGenericVariableId variable) {
    return this->get_variable_def(variable);
  };

  auto get_variable_kill = [&](RgGenericVariableId variable) {
    return this->get_variable_kill(variable);
  };

  auto get_buffer_def = [&](RgBufferUseId use) {
    return this->get_buffer_def(m_rg->m_buffer_uses[use].buffer);
  };

  auto get_buffer_kill = [&](RgBufferUseId use) {
    return this->get_buffer_kill(m_rg->m_buffer_uses[use].buffer);
  };

  auto get_texture_def = [&](RgTextureUseId use) {
    return this->get_texture_def(m_rg->m_texture_uses[use].texture);
  };

  auto get_texture_kill = [&](RgTextureUseId use) {
    return this->get_texture_kill(m_rg->m_texture_uses[use].texture);
  };

  auto is_null_pass = [](RgPassId pass) -> bool { return !pass; };

  SmallVector<RgPassId> dependents;
  auto get_dependants = [&](RgPassId pass_id) -> Span<const RgPassId> {
    const RgPassInfo &pass = m_passes[pass_id];
    dependents.clear();
    // Reads must happen before writes
    dependents.append(pass.read_variables | map(get_variable_kill));
    dependents.append(pass.read_buffers | map(get_buffer_kill));
    dependents.append(pass.read_textures | map(get_texture_kill));
    dependents.unstable_erase_if(is_null_pass);
    return dependents;
  };

  SmallVector<RgPassId> dependencies;
  auto get_dependencies = [&](RgPassId pass_id) -> Span<const RgPassId> {
    const RgPassInfo &pass = m_passes[pass_id];
    dependencies.clear();
    // Reads must happen after creation
    dependencies.append(pass.read_variables | map(get_variable_def));
    dependencies.append(pass.read_buffers | map(get_buffer_def));
    dependencies.append(pass.read_textures | map(get_texture_def));
    // Writes must happen after creation
    dependencies.append(pass.write_variables | map(get_variable_def));
    dependencies.append(pass.write_buffers | map(get_buffer_def));
    dependencies.append(pass.write_textures | map(get_texture_def));
    dependencies.unstable_erase_if(is_null_pass);
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
    ren_assert(dependency_time < time);
    schedule.push_back(pass);
    pass_schedule_times[pass] = time;

    for (RgPassId s : successors[pass]) {
      if (--predecessor_counts[s] == 0) {
        int max_dependency_time = -1;
        Span<const RgPassId> dependencies = get_dependencies(s);
        if (not dependencies.empty()) {
          max_dependency_time =
              std::ranges::max(dependencies | map([&](RgPassId d) {
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

  SmallVector<RgGenericVariableId> create_variables;
  SmallVector<RgGenericVariableId> write_variables;
  SmallVector<RgBufferId> create_buffers;
  SmallVector<RgBufferId> write_buffers;
  SmallVector<RgTextureId> create_textures;
  SmallVector<RgTextureId> write_textures;

  for (RgPassId pass_id : schedule) {
    const RgPassInfo &pass = m_passes[pass_id];

    create_variables.clear();
    write_variables.clear();
    for (RgGenericVariableId variable : pass.write_variables) {
      if (not m_variable_names.contains(variable)) {
        create_variables.push_back(m_variable_children[variable]);
      } else {
        write_variables.push_back(variable);
      }
    }

    fmt::println(stderr, "  * {}", m_rg->m_pass_names[pass_id]);
    if (not create_variables.empty()) {
      fmt::println(stderr, "    Creates variables:");
      for (RgVariableId variable : create_variables) {
        fmt::println(stderr, "      - {}", m_variable_names[variable]);
      }
    }
    if (not pass.read_variables.empty()) {
      fmt::println(stderr, "    Reads variables:");
      for (RgVariableId variable : pass.read_variables) {
        fmt::println(stderr, "      - {}", m_variable_names[variable]);
      }
    }
    if (not write_variables.empty()) {
      fmt::println(stderr, "    Writes variables:");
      for (RgVariableId src : write_variables) {
        RgVariableId dst = m_variable_children[src];
        fmt::println(stderr, "      - {} -> {}", m_variable_names[src],
                     m_variable_names[dst]);
      }
    }

    create_buffers.clear();
    write_buffers.clear();
    for (RgBufferUseId use : pass.write_buffers) {
      RgBufferId buffer = m_rg->m_buffer_uses[use].buffer;
      if (not m_buffer_names.contains(buffer)) {
        create_buffers.push_back(m_buffer_children[buffer]);
      } else {
        write_buffers.push_back(buffer);
      }
    }

    if (not create_buffers.empty()) {
      fmt::println(stderr, "    Creates buffers:");
      for (RgBufferId buffer : create_buffers) {
        fmt::println(stderr, "      - {}", m_buffer_names[buffer]);
      }
    }
    if (not pass.read_buffers.empty()) {
      fmt::println(stderr, "    Reads buffers:");
      for (RgBufferUseId use : pass.read_buffers) {
        fmt::println(stderr, "      - {}",
                     m_buffer_names[m_rg->m_buffer_uses[use].buffer]);
      }
    }
    if (not write_buffers.empty()) {
      fmt::println(stderr, "    Writes buffers:");
      for (RgBufferId src : write_buffers) {
        RgBufferId dst = m_buffer_children[src];
        fmt::println(stderr, "      - {} -> {}", m_buffer_names[src],
                     m_buffer_names[dst]);
      }
    }

    create_textures.clear();
    write_textures.clear();
    for (RgTextureUseId use : pass.write_textures) {
      RgTextureId texture = m_rg->m_texture_uses[use].texture;
      if (not m_texture_names.contains(texture)) {
        create_textures.push_back(m_texture_children[texture]);
      } else {
        write_textures.push_back(texture);
      }
    }

    if (not create_textures.empty()) {
      fmt::println(stderr, "    Creates textures:");
      for (RgTextureId texture : create_textures) {
        fmt::println(stderr, "      - {}", m_texture_names[texture]);
      }
    }
    if (not pass.read_textures.empty()) {
      fmt::println(stderr, "    Reads textures:");
      for (RgTextureUseId use : pass.read_textures) {
        fmt::println(stderr, "      - {}",
                     m_texture_names[m_rg->m_texture_uses[use].texture]);
      }
    }
    if (not write_textures.empty()) {
      fmt::println(stderr, "    Writes textures:");
      for (RgTextureId src : write_textures) {
        RgTextureId dst = m_texture_children[src];
        fmt::println(stderr, "      - {} -> {}", m_texture_names[src],
                     m_texture_names[dst]);
      }
    }

    if (not pass.wait_semaphores.empty()) {
      fmt::println(stderr, "    Waits for semaphores:");
      for (RgSemaphoreSignalId signal : pass.wait_semaphores) {
        RgSemaphoreId semaphore = m_rg->m_semaphore_signals[signal].semaphore;
        fmt::println(stderr, "      - {}", m_semaphore_names[semaphore]);
      }
    }

    if (not pass.signal_semaphores.empty()) {
      fmt::println(stderr, "    Signals semaphores:");
      for (RgSemaphoreSignalId signal : pass.signal_semaphores) {
        RgSemaphoreId semaphore = m_rg->m_semaphore_signals[signal].semaphore;
        fmt::println(stderr, "      - {}", m_semaphore_names[semaphore]);
      }
    }

    fmt::println(stderr, "");
  }
#endif
}

void RgBuilder::create_resources(Span<const RgPassId> schedule) {
  std::array<VkBufferUsageFlags, NUM_BUFFER_HEAPS> heap_usage_flags = {};

  for (RgPassId pass_id : schedule) {
    const RgPassInfo &pass = m_passes[pass_id];

    auto update_buffer_heap_usage_flags = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_rg->m_buffer_uses[use_id];
      RgPhysicalBufferId physical_buffer_id =
          m_rg->m_physical_buffers[use.buffer];
      const RgBufferDesc &desc = m_buffer_descs[physical_buffer_id];
      auto heap = i32(desc.heap);
      heap_usage_flags[heap] |= get_buffer_usage_flags(use.usage.access_mask);
    };

    std::ranges::for_each(pass.read_buffers, update_buffer_heap_usage_flags);
    std::ranges::for_each(pass.write_buffers, update_buffer_heap_usage_flags);

    auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_rg->m_texture_uses[use_id];
      RgPhysicalTextureId physical_texture_id =
          m_rg->m_physical_textures[use.texture];
      m_texture_descs.get(physical_texture_id).map([&](RgTextureDesc &desc) {
        desc.usage |= get_texture_usage_flags(use.usage.access_mask);
      });
    };

    std::ranges::for_each(pass.read_textures, update_texture_usage_flags);
    std::ranges::for_each(pass.write_textures, update_texture_usage_flags);
  }

  // Calculate required size for each buffer heap
  std::array<usize, NUM_BUFFER_HEAPS> required_heap_sizes = {};
  for (const auto &[_, desc] : m_buffer_descs) {
    auto heap = int(desc.heap);
    required_heap_sizes[heap] += pad(desc.size, glsl::DEVICE_CACHE_LINE_SIZE);
  }
  for (usize &size : required_heap_sizes) {
    size *= PIPELINE_DEPTH;
  }

  m_rg->m_heap_buffers = {};
  for (int heap = 0; heap < m_rg->m_heap_buffers.size(); ++heap) {
    Handle<Buffer> &heap_buffer = m_rg->m_heap_buffers[heap];
    usize size = required_heap_sizes[heap];
    if (size > 0) {
      heap_buffer =
          m_rg->m_arena
              .create_buffer({
                  .name = fmt::format("Render graph buffer for heap {}", heap),
                  .heap = BufferHeap(heap),
                  .usage = heap_usage_flags[heap],
                  .size = size,
              })
              .buffer;
    }
  }

  std::array<usize, NUM_BUFFER_HEAPS> heap_tops = {};
  for (const auto &[base_buffer_id, desc] : m_buffer_descs) {
    auto heap = int(desc.heap);
    Handle<Buffer> buffer = m_rg->m_heap_buffers[heap];
    usize offset = heap_tops[heap];
    usize size = desc.size;
    for (int i = 0; i < PIPELINE_DEPTH; ++i) {
      ren_assert(offset + size <= required_heap_sizes[heap]);
      m_rg->m_buffers[base_buffer_id + i] = {
          .buffer = buffer,
          .offset = offset,
          .size = size,
      };
      offset += pad(size, glsl::DEVICE_CACHE_LINE_SIZE);
    }
    heap_tops[heap] = offset;
  }

  for (const auto &[base_texture_id, desc] : m_texture_descs) {
    VkImageUsageFlags usage = desc.usage;
    for (auto i :
         range<usize>(m_rg->m_texture_temporal_layer_count[base_texture_id])) {
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
      StorageTextureId storage_descriptor;
      if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        storage_descriptor = m_rg->m_tex_alloc.allocate_storage_texture(
            *m_rg->m_renderer, m_rg->m_renderer->get_texture_view(htexture));
      }
      m_rg->m_textures[texture_id] = htexture;
      m_rg->m_storage_texture_descriptors[texture_id] = storage_descriptor;
    }
  }
}

void RgBuilder::init_temporal_textures(CommandAllocator &cmd_alloc) const {
  if (m_texture_init_callbacks.empty()) {
    return;
  }

  VkCommandBuffer cmd_buffer = cmd_alloc.allocate();
  {
    CommandRecorder rec(*m_rg->m_renderer, cmd_buffer);

    Vector<VkImageMemoryBarrier2> barriers;
    barriers.reserve(m_texture_init_callbacks.size());
    for (const auto &[texture_id, _] : m_texture_init_callbacks) {
      for (auto l :
           range<usize>(1, m_rg->m_texture_temporal_layer_count[texture_id])) {
        const Texture &texture =
            m_rg->m_renderer->get_texture(m_rg->m_textures[texture_id + l]);
        const RgTextureUsage &usage = m_rg->m_texture_usages[texture_id + l];
        barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .dstStageMask = usage.stage_mask,
            .dstAccessMask = usage.access_mask,
            .newLayout = usage.layout,
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
    rec.pipeline_barrier({}, barriers);

    for (const auto &[texture_id, cb] : m_texture_init_callbacks) {
      for (auto l :
           range<usize>(1, m_rg->m_texture_temporal_layer_count[texture_id])) {
        Handle<Texture> texture = m_rg->m_textures[texture_id + l];
        cb(texture, *m_rg->m_renderer, rec);
      }
    }
  }

  m_rg->m_renderer->graphicsQueueSubmit({{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd_buffer,
  }});
}

void RgBuilder::fill_pass_runtime_info(Span<const RgPassId> schedule) {
  m_rg->m_passes.resize(schedule.size());
  for (auto [idx, pass_id] : schedule | std::views::enumerate) {
    RgPassInfo &pass_info = m_passes[pass_id];
    m_rg->m_passes[idx] = {
        .pass = pass_id,
        .read_variables = std::move(pass_info.read_variables),
        .write_variables = std::move(pass_info.write_variables),
        .read_buffers = std::move(pass_info.read_buffers),
        .write_buffers = std::move(pass_info.write_buffers),
        .read_textures = std::move(pass_info.read_textures),
        .write_textures = std::move(pass_info.write_textures),
        .wait_semaphores = std::move(pass_info.wait_semaphores),
        .signal_semaphores = std::move(pass_info.signal_semaphores),
    };
    pass_info.data.visit(OverloadSet{
        [&](Monostate) {
#if REN_RG_DEBUG
          unreachable("Callback for pass {} has not been set!",
                      m_rg->m_pass_names[pass_id]);
#else
          unreachable("Callback for pass {} has not been set!", u32(pass_id));
#endif
        },
        [&](RgHostPassInfo &host_pass) {
          m_rg->m_passes[idx].data = RgHostPass{.cb = std::move(host_pass.cb)};
        },
        [&](RgGraphicsPassInfo &graphics_pass) {
          m_rg->m_passes[idx].data = RgGraphicsPass{
              .base_color_attachment = u32(m_rg->m_color_attachments.size()),
              .num_color_attachments =
                  u32(graphics_pass.color_attachments.size()),
              .depth_attachment = graphics_pass.depth_stencil_attachment.map(
                  [&](const RgDepthStencilAttachment &) -> u32 {
                    return m_rg->m_depth_stencil_attachments.size();
                  }),
              .cb = std::move(graphics_pass.cb),
          };
          m_rg->m_color_attachments.append(graphics_pass.color_attachments);
          graphics_pass.depth_stencil_attachment.map(
              [&](const RgDepthStencilAttachment &att) {
                m_rg->m_depth_stencil_attachments.push_back(att);
              });
        },
        [&](RgComputePassInfo &compute_pass) {
          m_rg->m_passes[idx].data =
              RgComputePass{.cb = std::move(compute_pass.cb)};
        },
        [&](RgGenericPassInfo &pass) {
          m_rg->m_passes[idx].data = RgGenericPass{.cb = std::move(pass.cb)};
        },
    });
  }
}

void RgBuilder::build(CommandAllocator &cmd_alloc) {
  Vector<RgPassId> schedule = build_pass_schedule();
  dump_pass_schedule(schedule);

  create_resources(schedule);
  init_temporal_textures(cmd_alloc);

  fill_pass_runtime_info(schedule);
}

} // namespace ren
