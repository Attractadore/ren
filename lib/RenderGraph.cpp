#include "RenderGraph.hpp"
#include "Support/Views.hpp"

namespace ren {

namespace {

auto get_pass_stages(RgPassType type) -> VkPipelineStageFlags2 {
  switch (type) {
  case RgPassType::Host:
    return VK_PIPELINE_STAGE_2_NONE;
  case RgPassType::Graphics:
    return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
           VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
           VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
           VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  case RgPassType::Compute:
    return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  case RgPassType::Transfer:
    return VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
           VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
  }
  unreachable("Unknown pass type {}", static_cast<int>(type));
}

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

auto get_buffer_access(VkPipelineStageFlags2 pass_stage_mask,
                       VkPipelineStageFlags2 stage_mask,
                       VkAccessFlags2 access_mask) -> RgBufferAccess {
  if (!stage_mask) {
    stage_mask = pass_stage_mask;
  }
  ren_assert(
      (stage_mask & pass_stage_mask) == stage_mask,
      "Buffer access pipeline stage mask must be a subset of pass stage mask");
  return {
      .stage_mask = stage_mask,
      .access_mask = access_mask,
  };
}

auto get_texture_layout(VkAccessFlags2 accesses) -> VkImageLayout {
  auto general_accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
  auto transfer_src_accesses = VK_ACCESS_2_TRANSFER_READ_BIT;
  auto transfer_dst_accesses = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  auto read_only_accesses = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  auto attachment_accesses = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  if ((accesses & general_accesses) == accesses) {
    return VK_IMAGE_LAYOUT_GENERAL;
  } else if ((accesses & transfer_src_accesses) == accesses) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  } else if ((accesses & transfer_dst_accesses) == accesses) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  } else if ((accesses & read_only_accesses) == accesses) {
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  } else {
    assert((accesses & attachment_accesses) == accesses);
    return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  }
}

auto get_texture_access(VkPipelineStageFlags2 pass_stage_mask,
                        VkPipelineStageFlags2 stage_mask,
                        VkAccessFlags2 access_mask, VkImageLayout layout)
    -> RgTextureAccess {
  if (!stage_mask) {
    stage_mask = pass_stage_mask;
  }
  ren_assert(
      (stage_mask & pass_stage_mask) == stage_mask,
      "Texture access pipeline stage mask must be a subset of pass stage mask");
  if (!layout) {
    layout = get_texture_layout(access_mask);
  }
  return {
      .stage_mask = stage_mask,
      .access_mask = access_mask,
      .layout = layout,
  };
}

} // namespace

RenderGraph::RenderGraph(Device &device, Swapchain &swapchain,
                         TextureIDAllocator &tex_alloc)
    : m_arena(device) {
  m_device = &device;

  m_swapchain = &swapchain;

  for (auto &&[index, semaphore] : enumerate(m_acquire_semaphores)) {
    semaphore = m_arena.create_semaphore({
        .name =
            fmt::format("Render graph swapchain acquire semaphore {}", index),
    });
  }

  for (auto &&[index, semaphore] : enumerate(m_present_semaphores)) {
    semaphore = m_arena.create_semaphore({
        .name =
            fmt::format("Render graph swapchain present semaphore {}", index),
    });
  }

  m_tex_alloc = &tex_alloc;
}

#if 0

RenderGraph::Builder::Builder(RenderGraph &rg) : m_rg(&rg) {}

auto RenderGraph::Builder::init_new_pass(RGPassType type, RGDebugName name)
    -> RGPassID {
  auto passid = m_passes.size();
  auto &pass = m_passes.emplace_back();
  pass.stages = get_pass_stages(type);
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_pass_names.push_back(std::move(name));
#endif
  return static_cast<RGPassID>(passid);
}

void RenderGraph::Builder::wait_semaphore(RGPassID pass,
                                          RGSemaphoreSignalInfo &&signal_info) {
  m_passes[pass].wait_semaphores.push_back(std::move(signal_info));
}

void RenderGraph::Builder::signal_semaphore(
    RGPassID pass, RGSemaphoreSignalInfo &&signal_info) {
  m_passes[pass].signal_semaphores.push_back(std::move(signal_info));
}

auto RenderGraph::Builder::create_pass(RGPassCreateInfo &&create_info)
    -> PassBuilder {
  auto pass = init_new_pass(create_info.type, std::move(create_info.name));
  return {pass, *this};
}

auto RenderGraph::Builder::init_new_texture(Optional<RGPassID> pass,
                                            Optional<RGTextureID> from_texture,
                                            RGDebugName name) -> RGTextureID {
  auto &textures = m_rg->m_runtime.m_textures;
  auto &physical_textures = m_rg->m_runtime.m_physical_textures;
  auto &texture_states = m_rg->m_texture_states;
  auto texture = from_texture.map_or_else(
      [&](RGTextureID from_texture) {
        return textures.insert(textures[from_texture]);
      },
      [&] {
        auto texture = textures.insert(physical_textures.size());
        physical_textures.emplace_back();
        texture_states.emplace_back();
        return texture;
      });
  pass.map([&](RGPassID pass) {
    m_texture_defs[texture] = pass;
    from_texture.map([&](RGTextureID from_texture) {
      m_texture_kills[from_texture] = pass;
    });
  });
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_rg->m_texture_names.insert(texture, std::move(name));
#endif
  return texture;
}

auto RenderGraph::Builder::get_texture_def(RGTextureID texture) const
    -> Optional<RGPassID> {
  auto it = m_texture_defs.find(texture);
  if (it != m_texture_defs.end()) {
    return it->second;
  }
  return None;
}

auto RenderGraph::Builder::get_texture_kill(RGTextureID texture) const
    -> Optional<RGPassID> {
  auto it = m_texture_kills.find(texture);
  if (it != m_texture_kills.end()) {
    return it->second;
  }
  return None;
}

void RenderGraph::Builder::read_texture(RGPassID passid,
                                        RGTextureReadInfo &&read_info) {
  auto &textures = m_rg->m_runtime.m_textures;
  auto &pass = m_passes[passid];
  pass.read_textures.push_back({
      .texture = read_info.texture,
      .state = get_texture_state(pass.stages, read_info.stages,
                                 read_info.accesses, read_info.layout),
  });
  auto it = m_texture_create_infos.find(textures[read_info.texture]);
  if (it != m_texture_create_infos.end()) {
    it->second.usage |= get_texture_usage_flags(read_info.accesses);
  }
}

auto RenderGraph::Builder::write_texture(RGPassID passid,
                                         RGTextureWriteInfo &&write_info)
    -> RGTextureID {
  const auto &textures = m_rg->m_runtime.m_textures;
  auto new_texture =
      init_new_texture(passid, write_info.texture, std::move(write_info.name));
  auto physical_texture = textures[new_texture];
  // Temporal textures should not be overwritten
  assert(ranges::none_of(m_rg->m_temporal_textures, [&](RGTextureID texture) {
    return textures[texture] == physical_texture;
  }));
  if (write_info.temporal) {
    // External textures can't be marked as temporal
    assert(not m_rg->m_external_textures.contains(physical_texture));
    m_rg->m_temporal_textures.insert(new_texture);
  }
  auto &pass = m_passes[passid];
  pass.write_textures.push_back({
      .texture = write_info.texture,
      .state = get_texture_state(pass.stages, write_info.stages,
                                 write_info.accesses, write_info.layout),
  });
  auto it = m_texture_create_infos.find(textures[write_info.texture]);
  if (it != m_texture_create_infos.end()) {
    it->second.usage |= get_texture_usage_flags(write_info.accesses);
  }
  return new_texture;
}

auto RenderGraph::Builder::create_texture(RGPassID passid,
                                          RGTextureCreateInfo &&create_info)
    -> RGTextureID {
  assert(glm::all(glm::greaterThan(create_info.size, glm::uvec3(0))));
  const auto &textures = m_rg->m_runtime.m_textures;
  auto new_texture =
      init_new_texture(passid, None, std::move(create_info.name));
  if (create_info.temporal) {
    m_rg->m_temporal_textures.insert(new_texture);
  }
  auto physical_texture = textures[new_texture];
  m_texture_create_infos[physical_texture] = {
      // FIXME: name will be incorrect during next frame for temporal textures
      .name = fmt::format("RenderGraph Texture {}", physical_texture),
      .type = create_info.type,
      .format = create_info.format,
      .usage = get_texture_usage_flags(create_info.accesses),
      .size = create_info.size,
      .num_mip_levels = create_info.num_mip_levels,
  };
  auto &pass = m_passes[passid];
  pass.write_textures.push_back({
      .texture = new_texture,
      .state = get_texture_state(pass.stages, create_info.stages,
                                 create_info.accesses, create_info.layout),
  });
  return new_texture;
}

auto RenderGraph::Builder::import_texture(RGTextureImportInfo &&import_info)
    -> RGTextureID {
  auto &textures = m_rg->m_runtime.m_textures;
  auto &physical_textures = m_rg->m_runtime.m_physical_textures;
  auto new_texture = init_new_texture(None, None, std::move(import_info.name));
  auto physical_texture = textures[new_texture];
  m_rg->m_external_textures.insert(physical_texture);
  physical_textures[physical_texture] = import_info.texture;
  m_rg->m_texture_states[physical_texture] = import_info.state;
  return new_texture;
}

auto RenderGraph::Builder::init_new_buffer(Optional<RGPassID> pass,
                                           Optional<RGBufferID> from_buffer,
                                           RGDebugName name) -> RGBufferID {
  auto &buffers = m_rg->m_runtime.m_buffers;
  auto &physical_buffers = m_rg->m_runtime.m_physical_buffers;
  auto buffer = from_buffer.map_or_else(
      [&](RGBufferID from_buffer) {
        return buffers.insert(buffers[from_buffer]);
      },
      [&] {
        auto buffer = buffers.insert(physical_buffers.size());
        physical_buffers.emplace_back();
        m_rg->m_buffer_states.emplace_back();
        return buffer;
      });
  pass.map([&](RGPassID pass) {
    m_buffer_defs[buffer] = pass;
    from_buffer.map(
        [&](RGBufferID from_buffer) { m_buffer_kills[from_buffer] = pass; });
  });
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_rg->m_buffer_names.insert(buffer, std::move(name));
#endif
  return buffer;
}

auto RenderGraph::Builder::get_buffer_def(RGBufferID buffer) const
    -> Optional<RGPassID> {
  auto it = m_buffer_defs.find(buffer);
  if (it != m_buffer_defs.end()) {
    return it->second;
  }
  return None;
}

auto RenderGraph::Builder::get_buffer_kill(RGBufferID buffer) const
    -> Optional<RGPassID> {
  auto it = m_buffer_kills.find(buffer);
  if (it != m_buffer_kills.end()) {
    return it->second;
  }
  return None;
}

void RenderGraph::Builder::read_buffer(RGPassID passid,
                                       RGBufferReadInfo &&read_info) {
  const auto &buffers = m_rg->m_runtime.m_buffers;
  auto &pass = m_passes[passid];
  pass.read_buffers.push_back({
      .buffer = read_info.buffer,
      .state =
          get_buffer_state(pass.stages, read_info.stages, read_info.accesses),
  });
  auto it = m_buffer_create_infos.find(buffers[read_info.buffer]);
  if (it != m_buffer_create_infos.end()) {
    it->second.usage |= get_buffer_usage_flags(read_info.accesses);
  }
}

auto RenderGraph::Builder::write_buffer(RGPassID passid,
                                        RGBufferWriteInfo &&write_info)
    -> RGBufferID {
  const auto &buffers = m_rg->m_runtime.m_buffers;
  auto new_buffer =
      init_new_buffer(passid, write_info.buffer, std::move(write_info.name));
  auto physical_buffer = buffers[new_buffer];
  // Buffers that have been marked as temporal can't be written
  assert(ranges::none_of(m_rg->m_temporal_buffers, [&](RGBufferID buffer) {
    return buffers[buffer] == physical_buffer;
  }));
  if (write_info.temporal) {
    // External buffers can't be marked as temporal
    assert(not m_rg->m_external_buffers.contains(physical_buffer));
    m_rg->m_temporal_buffers.insert(new_buffer);
  }
  auto &pass = m_passes[passid];
  pass.write_buffers.push_back({
      .buffer = write_info.buffer,
      .state =
          get_buffer_state(pass.stages, write_info.stages, write_info.accesses),
  });
  auto it = m_buffer_create_infos.find(buffers[write_info.buffer]);
  if (it != m_buffer_create_infos.end()) {
    it->second.usage |= get_buffer_usage_flags(write_info.accesses);
  }
  return new_buffer;
}

auto RenderGraph::Builder::create_buffer(RGPassID passid,
                                         RGBufferCreateInfo &&create_info)
    -> RGBufferID {
  assert(create_info.size > 0);
  const auto &buffers = m_rg->m_runtime.m_buffers;
  auto new_buffer = init_new_buffer(passid, None, std::move(create_info.name));
  if (create_info.temporal) {
    m_rg->m_temporal_buffers.insert(new_buffer);
  }
  auto physical_buffer = buffers[new_buffer];
  m_buffer_create_infos[physical_buffer] = {
      // FIXME: name will be incorrect during next frame for temporal buffers
      .name = fmt::format("RenderGraph Buffer {}", physical_buffer),
      .heap = create_info.heap,
      .usage = get_buffer_usage_flags(create_info.accesses),
      .size = create_info.size,
  };
  auto &pass = m_passes[passid];
  pass.write_buffers.push_back({
      .buffer = new_buffer,
      .state = get_buffer_state(pass.stages, create_info.stages,
                                create_info.accesses),
  });
  return new_buffer;
}

auto RenderGraph::Builder::import_buffer(RGBufferImportInfo &&import_info)
    -> RGBufferID {
  const auto &buffers = m_rg->m_runtime.m_buffers;
  auto &physical_buffers = m_rg->m_runtime.m_physical_buffers;
  auto new_buffer = init_new_buffer(None, None, std::move(import_info.name));
  auto physical_buffer = buffers[new_buffer];
  physical_buffers[physical_buffer] = import_info.buffer;
  m_rg->m_buffer_states[physical_buffer] = import_info.state;
  return new_buffer;
}

void RenderGraph::Builder::set_callback(RGPassID pass, RGPassCallback cb) {
  m_passes[pass].cb = std::move(cb);
}

void RenderGraph::Builder::present(Swapchain &swapchain, RGTextureID texture,
                                   Handle<Semaphore> acquire_semaphore,
                                   Handle<Semaphore> present_semaphore) {
  m_rg->m_swapchain = &swapchain;
  m_rg->m_present_semaphore = present_semaphore;

  swapchain.acquireImage(acquire_semaphore);

  auto swapchain_image = import_texture(RGTextureImportInfo{
      .name = "Swapchain image",
      .texture = swapchain.getTexture(),
  });

  auto blit = create_pass({
      .name = "Blit to swapchain",
      .type = RGPassType::Transfer,
  });

  blit.read_transfer_texture({.texture = texture});

  auto blitted_swapchain_image = blit.write_transfer_texture({
      .name = "Swapchain image after blit",
      .texture = swapchain_image,
  });

  blit.wait_semaphore({.semaphore = acquire_semaphore});
  blit.set_transfer_callback(
      [=, src = texture, dst = swapchain_image](Device &device, RGRuntime &rg,
                                                CommandRecorder &cmd) {
        auto src_texture = rg.get_texture(src);
        auto swapchain_texture = rg.get_texture(dst);
        VkImageBlit region = {
            .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .mipLevel = src_texture.first_mip_level,
                               .baseArrayLayer = src_texture.first_array_layer,
                               .layerCount = 1},
            .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .mipLevel = swapchain_texture.first_mip_level,
                               .baseArrayLayer =
                                   swapchain_texture.first_array_layer,
                               .layerCount = 1},
        };
        auto src_size = device.get_texture_view_size(src_texture);
        std::memcpy(&region.srcOffsets[1], &src_size, sizeof(src_size));
        auto dst_size = device.get_texture_view_size(swapchain_texture);
        std::memcpy(&region.dstOffsets[1], &dst_size, sizeof(dst_size));
        cmd.blit(src_texture.texture, swapchain_texture.texture, region,
                 VK_FILTER_LINEAR);
      });

  auto present = create_pass({.name = "Present"});
  present.read_texture({
      .texture = blitted_swapchain_image,
      .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  });
  present.signal_semaphore({.semaphore = present_semaphore});
}

auto RenderGraph::Builder::schedule_passes() -> Vector<RGPassID> {
  Vector<SmallFlatSet<RGPassID>> successors(m_passes.size());
  Vector<int> predecessor_counts(m_passes.size());

  auto add_edge = [&](RGPassID from, RGPassID to) {
    auto [it, inserted] = successors[from].insert(to);
    if (inserted) {
      ++predecessor_counts[to];
    }
  };

  auto get_texture_def = [&](const RGTextureAccess &tex_access) {
    return this->get_texture_def(tex_access.texture);
  };
  auto get_texture_kill = [&](const RGTextureAccess &tex_access) {
    return this->get_texture_kill(tex_access.texture);
  };
  auto get_buffer_def = [&](const RGBufferAccess &buffer_access) {
    return this->get_buffer_def(buffer_access.buffer);
  };
  auto get_buffer_kill = [&](const RGBufferAccess &buffer_access) {
    return this->get_buffer_kill(buffer_access.buffer);
  };

  SmallVector<RGPassID> dependents;
  auto get_dependants = [&](RGPassID id) -> std::span<const RGPassID> {
    const auto &pass = m_passes[id];
    // Reads must happen before writes
    dependents.assign(concat(pass.read_textures | filter_map(get_texture_kill),
                             pass.read_buffers | filter_map(get_buffer_kill)));
    return dependents;
  };

  SmallVector<RGPassID> dependencies;
  auto get_dependencies = [&](RGPassID id) -> std::span<const RGPassID> {
    const auto &pass = m_passes[id];
    dependencies.assign(concat(
        // Reads must happen after creation
        pass.read_textures | filter_map(get_texture_def),
        pass.read_buffers | filter_map(get_buffer_def),
        // Writes must happen after creation
        pass.write_textures | filter_map(get_texture_def),
        pass.write_buffers | filter_map(get_buffer_def)));
    ranges::actions::unstable_remove_if(
        dependencies, [&](RGPassID dep) { return dep == id; });
    return dependencies;
  };

  // Schedule passes whose dependencies were scheduled the longest time ago
  // first
  MinQueue<std::tuple<int, RGPassID>> unscheduled_passes;

  // Build DAG
  for (auto idx : range<size_t>(1, m_passes.size())) {
    RGPassID id(idx);
    auto predecessors = get_dependencies(id);
    auto successors = get_dependants(id);

    for (auto p : predecessors) {
      add_edge(p, id);
    }

    for (auto s : successors) {
      add_edge(id, s);
    }

    if (predecessors.empty()) {
      // This is a pass with no dependencies and can be scheduled right away
      unscheduled_passes.push({-1, id});
    }
  }

  Vector<RGPassID> scheduled_passes;
  scheduled_passes.reserve(m_passes.size());
  auto &pass_schedule_times = predecessor_counts;

  while (not unscheduled_passes.empty()) {
    auto [dependency_time, pass] = unscheduled_passes.top();
    unscheduled_passes.pop();

    int time = scheduled_passes.size();
    assert(dependency_time < time);
    scheduled_passes.push_back(pass);
    pass_schedule_times[pass] = time;

    for (auto s : successors[pass]) {
      if (--predecessor_counts[s] == 0) {
        int max_dependency_time = ranges::max(
            concat(once(-1), get_dependencies(s) | map([&](RGPassID d) {
                               return pass_schedule_times[d];
                             })));
        unscheduled_passes.push({max_dependency_time, s});
      }
    }
  }

  return scheduled_passes;
}

void RenderGraph::Builder::print_resources() const {
#if REN_RENDER_GRAPH_DEBUG_NAMES
  const auto &buffers = m_rg->m_runtime.m_buffers;
  if (not buffers.empty()) {
    rendergraphDebug("Buffers:");
    for (auto buffer : buffers.keys()) {
      rendergraphDebug("  * Buffer {:#x} ({})", std::bit_cast<u32>(buffer),
                       m_rg->m_buffer_names[buffer]);
    }
    rendergraphDebug("");
  }

  const auto &textures = m_rg->m_runtime.m_textures;
  if (not textures.empty()) {
    rendergraphDebug("Textures:");
    for (auto texture : textures.keys()) {
      rendergraphDebug("  * Texture {:#x} ({})", std::bit_cast<u32>(texture),
                       m_rg->m_texture_names[texture]);
    }
    rendergraphDebug("");
  }
#endif
}

void RenderGraph::Builder::print_passes(
    std::span<const RGPassID> passes) const {
#if REN_RENDER_GRAPH_DEBUG_NAMES
  const auto &buffers = m_rg->m_runtime.m_buffers;
  const auto &textures = m_rg->m_runtime.m_textures;

  rendergraphDebug("Scheduled passes:");
  for (auto passid : passes) {
    const auto &pass = m_passes[passid];
    std::string_view name = m_pass_names[passid];
    rendergraphDebug("  * {} pass", name);

    auto is_buffer_create = [&](const RGBufferAccess &access) {
      return get_buffer_def(access.buffer) == passid;
    };

    auto create_buffers = pass.write_buffers | filter(is_buffer_create);

    auto write_buffers =
        pass.write_buffers | ranges::views::remove_if(is_buffer_create);

    if (!create_buffers.empty()) {
      rendergraphDebug("    Creates buffers:");
      for (const auto &access : create_buffers) {
        auto buffer = access.buffer;
        rendergraphDebug("      - Buffer {:#x} ({})",
                         std::bit_cast<u32>(buffer),
                         m_rg->m_buffer_names[buffer]);
      }
    }

    if (!pass.read_buffers.empty()) {
      rendergraphDebug("    Reads buffers:");
      for (const auto &read_buffer : pass.read_buffers) {
        auto buffer = read_buffer.buffer;
        rendergraphDebug("      - Buffer {:#x} ({})",
                         std::bit_cast<u32>(buffer),
                         m_rg->m_buffer_names[buffer]);
      }
    }

    if (!write_buffers.empty()) {
      rendergraphDebug("    Writes buffers:");
      for (const auto &access : write_buffers) {
        auto buffer = access.buffer;
        auto new_buffer =
            ranges::find_if(m_buffer_defs,
                            [&](const std::pair<RGBufferID, RGPassID> &kv) {
                              auto [new_buffer, def] = kv;
                              return buffers[new_buffer] == buffers[buffer] and
                                     def == passid;
                            })
                ->first;
        rendergraphDebug(
            "      - Buffer {:#x} ({}) -> Buffer {:#x} ({})",
            std::bit_cast<u32>(buffer), m_rg->m_buffer_names[buffer],
            std::bit_cast<u32>(new_buffer), m_rg->m_buffer_names[new_buffer]);
      }
    }

    auto is_texture_create = [&](const RGTextureAccess &access) {
      return get_texture_def(access.texture) == passid;
    };

    auto create_textures = pass.write_textures | filter(is_texture_create);

    auto write_textures =
        pass.write_textures | ranges::views::remove_if(is_texture_create);

    if (!create_textures.empty()) {
      rendergraphDebug("    Creates textures:");
      for (const auto &access : create_textures) {
        auto texture = access.texture;
        rendergraphDebug("      - Texture {:#x} ({})",
                         std::bit_cast<u32>(texture),
                         m_rg->m_texture_names[texture]);
      }
    }

    if (!pass.read_textures.empty()) {
      rendergraphDebug("    Reads textures:");
      for (const auto &read_texture : pass.read_textures) {
        auto texture = read_texture.texture;
        rendergraphDebug("      - Texture {:#x} ({})",
                         std::bit_cast<u32>(texture),
                         m_rg->m_texture_names[texture]);
      }
    }

    if (!write_textures.empty()) {
      rendergraphDebug("    Writes textures:");
      for (const auto &access : write_textures) {
        auto texture = access.texture;
        auto new_texture =
            ranges::find_if(m_texture_defs,
                            [&](const std::pair<RGTextureID, RGPassID> &kv) {
                              auto [new_texture, def] = kv;
                              return textures[new_texture] ==
                                         textures[texture] and
                                     def == passid;
                            })
                ->first;
        rendergraphDebug("      - Texture {:#x} ({}) -> Texture {:#x} ({})",
                         std::bit_cast<u32>(texture),
                         m_rg->m_texture_names[texture],
                         std::bit_cast<u32>(new_texture),
                         m_rg->m_texture_names[new_texture]);
      }
    }

    rendergraphDebug("");
  }
#endif
}

void RenderGraph::Builder::create_buffers(const Device &device) {
  auto &physical_buffers = m_rg->m_runtime.m_physical_buffers;
  for (const auto &[buffer, create_info] : m_buffer_create_infos) {
    physical_buffers[buffer] = device.get_buffer_view(
        m_rg->m_arena.create_buffer(std::move(create_info)));
  }
}

void RenderGraph::Builder::create_textures(const Device &device) {
  auto &physical_textures = m_rg->m_runtime.m_physical_textures;
  for (const auto &[texture, create_info] : m_texture_create_infos) {
    physical_textures[texture] = device.get_texture_view(
        m_rg->m_arena.create_texture(std::move(create_info)));
  }
}

void RenderGraph::Builder::insert_barriers(Device &device) {
  const auto &buffers = m_rg->m_runtime.m_buffers;
  const auto &textures = m_rg->m_runtime.m_textures;
  const auto &physical_textures = m_rg->m_runtime.m_physical_textures;

  for (auto &pass : m_passes) {
    auto memory_barriers =
        concat(pass.read_buffers, pass.write_buffers) |
        filter_map([&](const RGBufferAccess &buffer_access)
                       -> Optional<VkMemoryBarrier2> {
          auto physical_buffer = buffers[buffer_access.buffer];
          auto &state = m_rg->m_buffer_states[physical_buffer];

          // FIXME: this looks wrong
          if (state.accesses == VK_ACCESS_2_NONE or
              buffer_access.state.accesses == VK_ACCESS_2_NONE) {
            return None;
          }

          VkMemoryBarrier2 barrier = {
              .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
              .srcStageMask = state.stages,
              .srcAccessMask = state.accesses,
              .dstStageMask = buffer_access.state.stages,
              .dstAccessMask = buffer_access.state.accesses,
          };

          state = buffer_access.state;

          return barrier;
        }) |
        ranges::to<Vector>();

    auto image_barriers =
        concat(pass.read_textures, pass.write_textures) |
        map([&](const RGTextureAccess &texture_access) {
          auto physical_texture = textures[texture_access.texture];
          auto &state = m_rg->m_texture_states[physical_texture];
          const auto &view = physical_textures[physical_texture];

          VkImageMemoryBarrier2 barrier = {
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
              .srcStageMask = state.stages,
              .srcAccessMask = state.accesses,
              .dstStageMask = texture_access.state.stages,
              .dstAccessMask = texture_access.state.accesses,
              .oldLayout = state.layout,
              .newLayout = texture_access.state.layout,
              .image = device.get_texture(view.texture).image,
              .subresourceRange =
                  {
                      .aspectMask = getVkImageAspectFlags(view.format),
                      .baseMipLevel = view.first_mip_level,
                      .levelCount = view.num_mip_levels,
                      .baseArrayLayer = view.first_array_layer,
                      .layerCount = view.num_array_layers,
                  },
          };

          state = texture_access.state;

          return barrier;
        }) |
        ranges::to<Vector>();

    pass.cb = [memory_barriers = std::move(memory_barriers),
               image_barriers = std::move(image_barriers),
               cb = std::move(pass.cb)](Device &device, RGRuntime &rg,
                                        CommandRecorder &cmd) {
      cmd.pipeline_barrier(memory_barriers, image_barriers);
      if (cb) {
        cb(device, rg, cmd);
      }
    };
  }
}

auto RenderGraph::Builder::batch_passes(std::span<const RGPassID> schedule)
    -> Vector<RGBatch> {
  Vector<RGBatch> batches;
  bool begin_new_batch = true;
  for (auto passid : schedule) {
    auto &pass = m_passes[passid];
    if (!pass.wait_semaphores.empty()) {
      begin_new_batch = true;
    }
    if (begin_new_batch) {
      auto &batch = batches.emplace_back();
      batch.wait_semaphores = std::move(pass.wait_semaphores);
      begin_new_batch = false;
    }
    auto &batch = batches.back();
    batch.pass_cbs.push_back(std::move(pass.cb));
#if REN_RENDER_GRAPH_DEBUG_NAMES
    auto &name = m_pass_names[passid];
    batch.pass_names.push_back(std::move(name));
#endif
    if (!pass.signal_semaphores.empty()) {
      batch.signal_semaphores = std::move(pass.signal_semaphores);
      begin_new_batch = true;
    }
  }
  return batches;
}

void RenderGraph::Builder::build(Device &device) {
  rendergraphDebug("### Build RenderGraph ###");
  rendergraphDebug("");

  rendergraphDebug("Create buffers");
  rendergraphDebug("");
  create_buffers(device);
  rendergraphDebug("Create textures");
  rendergraphDebug("");
  create_textures(device);
  print_resources();

  rendergraphDebug("Schedule passes");
  rendergraphDebug("");
  auto schedule = schedule_passes();
  print_passes(schedule);

  rendergraphDebug("Insert barriers");
  rendergraphDebug("");
  insert_barriers(device);

  rendergraphDebug("Batch passes");
  rendergraphDebug("");
  m_rg->m_batches = batch_passes(schedule);

  rendergraphDebug("### Build done ###");
  rendergraphDebug("");
}

auto RGRuntime::get_buffer(RGBufferID buffer) const -> const BufferView & {
  assert(buffer);
  return m_physical_buffers[m_buffers[buffer]];
}

auto RGRuntime::get_texture(RGTextureID texture) const -> const TextureView & {
  assert(texture);
  return m_physical_textures[m_textures[texture]];
}

void RenderGraph::execute(Device &device, CommandAllocator &cmd_allocator) {
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffers;
  SmallVector<VkSemaphoreSubmitInfo> wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> signal_semaphores;

  for (auto &batch : m_batches) {
    cmd_buffers.clear();

    for (auto index : range(batch.pass_cbs.size())) {
      const auto &pass_cb = batch.pass_cbs[index];

      auto cmd_buffer = cmd_allocator.allocate();
      {
        CommandRecorder cmd(device, cmd_buffer);
        if (pass_cb) {
#if REN_RENDER_GRAPH_DEBUG_NAMES
          const auto &pass_name = batch.pass_names[index];
          auto _ = cmd.debug_region(pass_name.c_str());
#endif
          pass_cb(device, m_runtime, cmd);
        }
      }

      cmd_buffers.push_back({
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = cmd_buffer,
      });
    }

    auto get_semaphore_submit_info = [&](const RGSemaphoreSignalInfo &signal) {
      return VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = device.get_semaphore(signal.semaphore).handle,
          .value = signal.value,
          .stageMask = signal.stages,
      };
    };

    wait_semaphores.assign(batch.wait_semaphores |
                           map(get_semaphore_submit_info));

    signal_semaphores.assign(batch.signal_semaphores |
                             map(get_semaphore_submit_info));

    device.graphicsQueueSubmit(cmd_buffers, wait_semaphores, signal_semaphores);
  }

  m_swapchain->presentImage(m_present_semaphore);

  retain_temporal_resources();
}

void RenderGraph::retain_temporal_resources() {
  m_batches.clear();

  m_swapchain = nullptr;
  m_present_semaphore = {};

  auto &buffers = m_runtime.m_buffers;
  buffers.erase_if([&](RGBufferID buffer, unsigned) {
    return not m_temporal_buffers.contains(buffer);
  });

  auto &physical_buffers = m_runtime.m_physical_buffers;
  auto old_physical_buffers = std::move(physical_buffers);
  auto old_buffer_states = std::move(m_buffer_states);
  physical_buffers.resize(m_temporal_buffers.size());
  m_buffer_states.resize(m_temporal_buffers.size());
  for (auto &&[idx, b] : buffers | enumerate) {
    auto &[buffer, physical_buffer] = b;
    auto &view = old_physical_buffers[physical_buffer];
    physical_buffers[idx] = std::exchange(view, BufferView());
    m_buffer_states[idx] = old_buffer_states[physical_buffer];
    physical_buffer = idx;
  }

  for (const auto &[physical_buffer, view] : old_physical_buffers | enumerate) {
    if (not m_external_buffers.contains(physical_buffer)) {
      m_arena.destroy_buffer(view.buffer);
    }
  }

  m_temporal_buffers.clear();
  m_external_buffers.clear();

  auto &textures = m_runtime.m_textures;
  textures.erase_if([&](RGTextureID texture, unsigned) {
    return not m_temporal_textures.contains(texture);
  });

  auto &physical_textures = m_runtime.m_physical_textures;
  auto old_physical_textures = std::move(physical_textures);
  auto old_texture_states = std::move(m_texture_states);
  physical_textures.resize(m_temporal_textures.size());
  m_texture_states.resize(m_temporal_textures.size());
  for (auto &&[idx, b] : textures | enumerate) {
    auto &[texture, physical_texture] = b;
    auto &view = old_physical_textures[physical_texture];
    physical_textures[idx] = std::exchange(view, TextureView());
    m_texture_states[idx] = old_texture_states[physical_texture];
    physical_texture = idx;
  }

  for (const auto &[physical_texture, view] :
       old_physical_textures | enumerate) {
    if (not m_external_textures.contains(physical_texture)) {
      m_arena.destroy_texture(view.texture);
    }
  }

  m_temporal_textures.clear();
  m_external_textures.clear();

#if REN_RENDER_GRAPH_DEBUG_NAMES
  for (auto &[_, name] : m_buffer_names) {
    name = fmt::format("{} (from previous frame)", name);
  }
  for (auto &[_, name] : m_texture_names) {
    name = fmt::format("{} (from previous frame)", name);
  }
#endif
}

#endif

RgPassBuilder::PassBuilder(RgPass pass, Builder &builder)
    : m_pass(pass), m_builder(&builder) {}

auto RgPassBuilder::create_buffer(RgBufferCreateInfo &&create_info,
                                  const RgBufferAccess &access)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return m_builder->create_buffer(m_pass, std::move(create_info), access);
}

auto RgPassBuilder::read_buffer(RgBufferReadInfo &&read_info,
                                const RgBufferAccess &access) -> RgRtBuffer {
  return m_builder->read_buffer(m_pass, std::move(read_info), access);
}

auto RgPassBuilder::write_buffer(RgBufferWriteInfo &&write_info,
                                 const RgBufferAccess &access)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return m_builder->write_buffer(m_pass, std::move(write_info), access);
}

auto RgPassBuilder::read_indirect_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                         .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                     });
}

auto RgPassBuilder::read_index_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                         .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
                     });
}

auto RgPassBuilder::read_vertex_shader_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::read_fragment_shader_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::create_uniform_buffer(RgBufferCreateInfo &&create_info)
    -> RgRtBuffer {
  create_info.heap = BufferHeap::Upload;
  return std::get<1>(
      create_buffer(std::move(create_info),
                    {
                        .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                    }));
}

auto RgPassBuilder::read_compute_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                     });
}

auto RgPassBuilder::write_compute_buffer(RgBufferWriteInfo &&write_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return write_buffer(std::move(write_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      });
}

auto RgPassBuilder::create_compute_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Device;
  return create_buffer(std::move(create_info),
                       {
                           .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                       });
}

auto RgPassBuilder::read_transfer_buffer(RgBufferReadInfo &&read_info)
    -> RgRtBuffer {
  return read_buffer(std::move(read_info),
                     {
                         .stage_mask = 0,
                         .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                     });
}

auto RgPassBuilder::write_transfer_buffer(RgBufferWriteInfo &&write_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  return write_buffer(std::move(write_info),
                      {
                          .stage_mask = 0,
                          .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      });
}

auto RgPassBuilder::create_transfer_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Device;
  return create_buffer(std::move(create_info),
                       {
                           .stage_mask = 0,
                           .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       });
}

auto RgPassBuilder::create_upload_buffer(RgBufferCreateInfo &&create_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.heap = BufferHeap::Upload;
  return create_buffer(std::move(create_info), {
                                                   .stage_mask = 0,
                                                   .access_mask = 0,
                                               });
}

#if 0
void RgPassBuilder::set_color_attachment(RGColorAttachment attachment,
                                         u32 index) {
  auto &attachments = m_render_pass.color_attachments;
  if (attachments.size() <= index) {
    attachments.resize(index + 1);
  }
  attachments[index] = std::move(attachment);
}

auto RgPassBuilder::write_color_attachment(
    RGTextureWriteInfo &&write_info,
    RGColorAttachmentWriteInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  write_info.stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  write_info.access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  write_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = write_texture(std::move(write_info));
  set_color_attachment(
      {
          .texture = texture,
          .ops =
              {
                  .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                  .store = VK_ATTACHMENT_STORE_OP_STORE,
              },
      },
      attachment_info.index);
  return texture;
}

auto RgPassBuilder::create_color_attachment(
    RGTextureCreateInfo &&create_info,
    RGColorAttachmentCreateInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  create_info.access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  create_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = create_texture(std::move(create_info));
  auto clear_color = attachment_info.ops.get<RGAttachmentClearColor>().map(
      [](const RGAttachmentClearColor &clear) { return clear.color; });
  set_color_attachment(
      {
          .texture = texture,
          .ops =
              {
                  .load = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                      : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .store = VK_ATTACHMENT_STORE_OP_STORE,
                  .clear_color = clear_color.value_or(glm::vec4(0.0f)),
              },
      },
      attachment_info.index);
  return texture;
}

void RgPassBuilder::read_depth_attachment(
    RGTextureReadInfo &&read_info,
    RGDepthAttachmentReadInfo &&attachment_info) {
  read_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  read_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  read_info.layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  read_texture(std::move(read_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = read_info.texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_LOAD,
              .store = VK_ATTACHMENT_STORE_OP_NONE,
          },
  };
}

auto RgPassBuilder::write_depth_attachment(
    RGTextureWriteInfo &&write_info,
    RGDepthAttachmentWriteInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  write_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  write_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  write_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = write_texture(std::move(write_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_LOAD,
              .store = VK_ATTACHMENT_STORE_OP_STORE,
          },
  };
  return texture;
}

auto RgPassBuilder::create_depth_attachment(
    RGTextureCreateInfo &&create_info,
    RGDepthAttachmentCreateInfo &&attachment_info)
    -> std::tuple<RgBuffer, RgRtBuffer> {
  create_info.type = VK_IMAGE_TYPE_2D;
  create_info.stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  create_info.access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  create_info.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  auto texture = create_texture(std::move(create_info));
  m_render_pass.depth_attachment = RGDepthAttachment{
      .texture = texture,
      .depth_ops =
          DepthAttachmentOperations{
              .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store = VK_ATTACHMENT_STORE_OP_STORE,
              .clear_depth = attachment_info.depth_ops.depth,
          },
  };
  return texture;
}
#endif

auto RgPassBuilder::read_storage_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_GENERAL,
                      });
}

auto RgPassBuilder::write_storage_texture(RgTextureWriteInfo &&write_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return write_texture(std::move(write_info),
                       {
                           .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_GENERAL,
                       });
}

auto RgPassBuilder::create_storage_texture(RgTextureCreateInfo &&create_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return create_texture(
      std::move(create_info),
      {
          .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .layout = VK_IMAGE_LAYOUT_GENERAL,
      });
}

auto RgPassBuilder::read_sampled_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                      });
}

auto RgPassBuilder::read_transfer_texture(RgTextureReadInfo &&read_info)
    -> RgRtTexture {
  return read_texture(std::move(read_info),
                      {
                          .stage_mask = 0,
                          .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                          .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      });
}

auto RgPassBuilder::write_transfer_texture(RgTextureWriteInfo &&write_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return write_texture(std::move(write_info),
                       {
                           .stage_mask = 0,
                           .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       });
}

auto RgPassBuilder::create_transfer_texture(RgTextureCreateInfo &&create_info)
    -> std::tuple<RgTexture, RgRtTexture> {
  return create_texture(std::move(create_info),
                        {
                            .stage_mask = 0,
                            .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        });
}

#if 0
void RgPassBuilder::set_host_callback(RGHostPassCallback cb) {
  assert(cb);
  m_builder->set_callback(
      m_pass, [cb = std::move(cb)](Device &device, RGRuntime &rg,
                                   CommandRecorder &) { cb(device, rg); });
}

void RgPassBuilder::set_graphics_callback(RGGraphicsPassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass, [cb = std::move(cb),
                                   begin_info = std::move(m_render_pass)](
                                      Device &device, RGRuntime &rg,
                                      CommandRecorder &cmd) {
    auto color_attachments =
        begin_info.color_attachments |
        map([&](const Optional<RGColorAttachment> &attachment)
                -> Optional<ColorAttachment> {
          return attachment.map(
              [&](const RGColorAttachment &attachment) -> ColorAttachment {
                return {
                    .texture = rg.get_texture(attachment.texture),
                    .ops = attachment.ops,
                };
              });
        }) |
        ranges::to<
            StaticVector<Optional<ColorAttachment>, MAX_COLOR_ATTACHMENTS>>;
    auto render_pass = cmd.render_pass({
        .color_attachments = color_attachments,
        .depth_stencil_attachment = begin_info.depth_attachment.map(
            [&](const RGDepthAttachment &attachment) -> DepthStencilAttachment {
              return {
                  .texture = rg.get_texture(attachment.texture),
                  .depth_ops = attachment.depth_ops,
              };
            }),
    });
    cb(device, rg, render_pass);
  });
}

void RgPassBuilder::set_compute_callback(RGComputePassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass,
                          [cb = std::move(cb)](Device &device, RGRuntime &rg,
                                               CommandRecorder &cmd) {
                            auto pass = cmd.compute_pass();
                            cb(device, rg, pass);
                          });
}

void RgPassBuilder::set_transfer_callback(RGTransferPassCallback cb) {
  assert(cb);
  m_builder->set_callback(m_pass, std::move(cb));
}
#endif

} // namespace ren
