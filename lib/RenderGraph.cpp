#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSetAllocator.hpp"
#include "Device.hpp"
#include "Formats.inl"
#include "ResourceArena.hpp"
#include "Support/FlatSet.hpp"
#include "Support/Log.hpp"
#include "Support/PriorityQueue.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

#include <range/v3/action.hpp>
#include <range/v3/algorithm.hpp>

static auto get_texture_usage_flags(VkAccessFlags2 accesses)
    -> VkImageUsageFlags {
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

static auto get_buffer_usage_flags(VkAccessFlags2 accesses)
    -> VkBufferUsageFlags {
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

namespace ren {

auto RenderGraph::Builder::init_new_pass(std::string name) -> RGPassID {
  auto pass = m_passes.size();
  m_passes.emplace_back();
  m_pass_names.push_back(std::move(name));
  return static_cast<RGPassID>(pass);
}

auto RenderGraph::Builder::getPassID(const RGPass &pass) const -> RGPassID {
  return static_cast<RGPassID>(&pass - m_passes.data());
}

void RenderGraph::Builder::wait_semaphore(RGPassID pass,
                                          Handle<Semaphore> semaphore,
                                          u64 value,
                                          VkPipelineStageFlags2 stages) {
  m_passes[pass].wait_semaphores.push_back({
      .semaphore = semaphore,
      .value = value,
      .stages = stages,
  });
}

void RenderGraph::Builder::signal_semaphore(RGPassID pass,
                                            Handle<Semaphore> semaphore,
                                            u64 value,
                                            VkPipelineStageFlags2 stages) {
  m_passes[pass].signal_semaphores.push_back({
      .semaphore = semaphore,
      .value = value,
      .stages = stages,
  });
}

auto RenderGraph::Builder::create_pass(std::string name) -> PassBuilder {
  auto pass = init_new_pass(std::move(name));
  return {pass, this};
}

auto RenderGraph::Builder::init_new_texture(Optional<RGPassID> pass,
                                            Optional<RGTextureID> from_texture,
                                            std::string name) -> RGTextureID {
  auto texture = static_cast<RGTextureID>(m_textures.size());
  m_textures.emplace_back();
  m_texture_names.push_back(std::move(name));
  m_texture_states.emplace_back();
  pass.map([&](RGPassID pass) {
    m_texture_defs[texture] = pass;
    from_texture.map([&](RGTextureID from_texture) {
      m_texture_parents[texture] = from_texture;
      m_texture_kills[from_texture] = pass;
    });
  });
  from_texture.map_or_else(
      [&](RGTextureID from_texture) {
        m_physical_textures.push_back(from_texture);
      },
      [&] { m_physical_textures.push_back(texture); });
  m_texture_usage_flags.emplace_back();
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

void RenderGraph::Builder::read_texture(RGPassID pass, RGTextureID texture,
                                        VkAccessFlags2 accesses,
                                        VkPipelineStageFlags2 stages,
                                        VkImageLayout layout) {
  m_passes[pass].read_textures.push_back({
      .texture = texture,
      .accesses = accesses,
      .stages = stages,
      .layout = layout,
  });
  m_texture_usage_flags[m_physical_textures[texture]] |=
      get_texture_usage_flags(accesses);
}

auto RenderGraph::Builder::write_texture(RGPassID pass, RGTextureID texture,
                                         std::string name,
                                         VkAccessFlags2 accesses,
                                         VkPipelineStageFlags2 stages,
                                         VkImageLayout layout) -> RGTextureID {
  auto new_texture = init_new_texture(pass, texture, std::move(name));
  m_passes[pass].write_textures.push_back({
      .texture = new_texture,
      .accesses = accesses,
      .stages = stages,
      .layout = layout,
  });
  m_texture_usage_flags[m_physical_textures[new_texture]] |=
      get_texture_usage_flags(accesses);
  return new_texture;
}

auto RenderGraph::Builder::create_texture(RGPassID pass,
                                          RGTextureCreateInfo &&create_info,
                                          std::string name,
                                          VkAccessFlags2 accesses,
                                          VkPipelineStageFlags2 stages,
                                          VkImageLayout layout) -> RGTextureID {
  auto new_texture = init_new_texture(pass, None, std::move(name));
  m_texture_create_infos[new_texture] = std::move(create_info);
  m_passes[pass].write_textures.push_back({
      .texture = new_texture,
      .accesses = accesses,
      .stages = stages,
      .layout = layout,
  });
  m_texture_usage_flags[m_physical_textures[new_texture]] |=
      get_texture_usage_flags(accesses);
  return new_texture;
}

auto RenderGraph::Builder::import_texture(const TextureView &texture,
                                          std::string name,
                                          VkAccessFlags2 accesses,
                                          VkPipelineStageFlags2 stages,
                                          VkImageLayout layout) -> RGTextureID {
  auto new_texture = init_new_texture(None, None, std::move(name));
  m_textures[new_texture] = texture;
  m_texture_states[new_texture] = {
      .accesses = accesses,
      .stages = stages,
      .layout = layout,
  };
  return new_texture;
}

auto RenderGraph::Builder::init_new_buffer(Optional<RGPassID> pass,
                                           Optional<RGBufferID> from_buffer,
                                           std::string name) -> RGBufferID {
  auto buffer = static_cast<RGBufferID>(m_buffers.size());
  m_buffers.emplace_back();
  m_buffer_names.push_back(std::move(name));
  m_buffer_states.emplace_back();
  pass.map([&](RGPassID pass) {
    m_buffer_defs[buffer] = pass;
    from_buffer.map([&](RGBufferID from_buffer) {
      m_buffer_parents[buffer] = from_buffer;
      m_buffer_kills[from_buffer] = pass;
    });
  });
  from_buffer.map_or_else(
      [&](RGBufferID from_buffer) {
        m_physical_buffers.push_back(from_buffer);
      },
      [&] { m_physical_buffers.push_back(buffer); });
  m_buffer_usage_flags.emplace_back();
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

void RenderGraph::Builder::read_buffer(RGPassID pass, RGBufferID buffer,
                                       VkAccessFlags2 accesses,
                                       VkPipelineStageFlags2 stages) {
  m_passes[pass].read_buffers.push_back({
      .buffer = buffer,
      .accesses = accesses,
      .stages = stages,
  });
  m_buffer_usage_flags[m_physical_buffers[buffer]] |=
      get_buffer_usage_flags(accesses);
}

auto RenderGraph::Builder::write_buffer(RGPassID pass, RGBufferID buffer,
                                        std::string name,
                                        VkAccessFlags2 accesses,
                                        VkPipelineStageFlags2 stages)
    -> RGBufferID {
  auto new_buffer = init_new_buffer(pass, buffer, std::move(name));
  m_passes[pass].write_buffers.push_back({
      .buffer = new_buffer,
      .accesses = accesses,
      .stages = stages,
  });
  m_buffer_usage_flags[m_physical_buffers[new_buffer]] |=
      get_buffer_usage_flags(accesses);
  return new_buffer;
}

auto RenderGraph::Builder::create_buffer(
    RGPassID pass, RGBufferCreateInfo &&create_info, std::string name,
    VkAccessFlags2 accesses, VkPipelineStageFlags2 stages) -> RGBufferID {
  assert(create_info.size > 0);
  auto new_buffer = init_new_buffer(pass, None, std::move(name));
  auto [_, inserted] =
      m_buffer_create_infos.emplace(new_buffer, std::move(create_info));
  assert(inserted);
  m_passes[pass].write_buffers.push_back({
      .buffer = new_buffer,
      .accesses = accesses,
      .stages = stages,
  });
  m_buffer_usage_flags[m_physical_buffers[new_buffer]] |=
      get_buffer_usage_flags(accesses);
  return new_buffer;
}

auto RenderGraph::Builder::import_buffer(const BufferView &buffer,
                                         std::string name,
                                         VkAccessFlags2 accesses,
                                         VkPipelineStageFlags2 stages)
    -> RGBufferID {
  auto new_buffer = init_new_buffer(None, None, std::move(name));
  m_buffers[new_buffer] = buffer;
  m_buffer_states[new_buffer] = {
      .accesses = accesses,
      .stages = stages,
  };
  return new_buffer;
}

void RenderGraph::Builder::set_callback(RGPassID pass, RGCallback cb) {
  m_passes[pass].pass_cb = std::move(cb);
}

void RenderGraph::Builder::present(Swapchain &swapchain, RGTextureID texture,
                                   Handle<Semaphore> acquire_semaphore,
                                   Handle<Semaphore> present_semaphore) {
  m_swapchain = &swapchain;
  m_present_semaphore = present_semaphore;

  swapchain.acquireImage(acquire_semaphore);

  auto swapchain_image = import_texture(
      m_swapchain->getTexture(), "Swapchain texture", VK_ACCESS_2_NONE,
      VK_PIPELINE_STAGE_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED);

  auto blit = create_pass("Blit to swapchain");

  blit.read_texture(texture, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_BLIT_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  auto blitted_swapchain_image = blit.write_texture(
      swapchain_image, "Swapchain texture after blit",
      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  blit.wait_semaphore(acquire_semaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
  blit.set_callback([=, src = texture, dst = swapchain_image](
                        Device &device, RenderGraph &rg, CommandBuffer &cmd) {
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

  auto present = create_pass("Present");

  present.read_texture(blitted_swapchain_image, 0, 0,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  present.signal_semaphore(present_semaphore, VK_PIPELINE_STAGE_2_NONE);
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
  auto get_dependants = [&](const RGPass &pass) -> const auto & {
    // Reads must happen before writes
    dependents.assign(concat(pass.read_textures | filter_map(get_texture_kill),
                             pass.read_buffers | filter_map(get_buffer_kill)));
    return dependents;
  };

  SmallVector<RGPassID> dependencies;
  auto get_dependencies = [&](const RGPass &pass) -> const auto & {
    auto is_not_create = [&](RGPassID def) { return def != getPassID(pass); };
    dependencies.assign(concat(
        // Reads must happen after creation
        pass.read_textures | filter_map(get_texture_def),
        pass.read_buffers | filter_map(get_buffer_def),
        // Writes must happen after creation
        pass.write_textures | filter_map(get_texture_def) |
            filter(is_not_create),
        pass.write_buffers | filter_map(get_buffer_def) |
            filter(is_not_create)));
    return dependencies;
  };

  SmallVector<RGPassID> outputs;
  auto get_outputs = [&](const RGPass &pass) -> const auto & {
    outputs.assign(concat(pass.write_textures | filter_map(get_texture_def),
                          pass.write_buffers | filter_map(get_buffer_def)));
    return outputs;
  };

  // Schedule passes whose dependencies were scheduled the longest time ago
  // first
  MinQueue<std::tuple<int, RGPassID>> unscheduled_passes;

  // Build DAG
  for (const auto &pass : m_passes | ranges::views::drop(1)) {
    const auto &predecessors = get_dependencies(pass);

    auto id = getPassID(pass);
    for (auto p : predecessors) {
      add_edge(p, id);
    }
    for (auto s : get_dependants(pass)) {
      add_edge(id, s);
    }

    if (predecessors.empty()) {
      // This is a pass with no dependencies and it can be scheduled right away
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
        int max_dependency_time = ranges::max(concat(
            once(-1), get_dependencies(m_passes[s]) | map([&](RGPassID d) {
                        return pass_schedule_times[d];
                      })));
        unscheduled_passes.push({max_dependency_time, s});
      }
    }
  }

  return scheduled_passes;
}

void RenderGraph::Builder::print_resources() const {
  auto buffers = range<unsigned>(1, m_buffers.size());
  if (not buffers.empty()) {
    rendergraphDebug("Buffers:");
    for (auto buffer : buffers) {
      rendergraphDebug("  * Buffer {} ({})", buffer, m_buffer_names[buffer]);
    }
    rendergraphDebug("");
  }

  auto textures = range<unsigned>(1, m_textures.size());
  if (not textures.empty()) {
    rendergraphDebug("Textures:");
    for (auto texture : textures) {
      rendergraphDebug("  * Texture {} ({})", texture,
                       m_texture_names[texture]);
    }
    rendergraphDebug("");
  }
}

void RenderGraph::Builder::print_passes(
    std::span<const RGPassID> passes) const {
  rendergraphDebug("Scheduled passes:");
  for (auto passid : passes) {
    const auto &pass = m_passes[passid];
    std::string_view name = m_pass_names[passid];
    rendergraphDebug("  * {} pass", name);

    auto is_buffer_create = [&](const RGBufferAccess &access) {
      return !m_buffer_parents.contains(access.buffer);
    };

    auto create_buffers = pass.write_buffers | filter(is_buffer_create);

    auto write_buffers =
        pass.write_buffers | ranges::views::remove_if(is_buffer_create);

    if (!create_buffers.empty()) {
      rendergraphDebug("    Creates buffers:");
      for (const auto &access : create_buffers) {
        auto buffer = access.buffer;
        rendergraphDebug("      - Buffer {} ({})", buffer,
                         m_buffer_names[buffer]);
      }
    }

    if (!pass.read_buffers.empty()) {
      rendergraphDebug("    Reads buffers:");
      for (const auto &read_buffer : pass.read_buffers) {
        auto buffer = read_buffer.buffer;
        rendergraphDebug("      - Buffer {} ({})", buffer,
                         m_buffer_names[buffer]);
      }
    }

    if (!write_buffers.empty()) {
      rendergraphDebug("    Writes buffers:");
      for (const auto &access : write_buffers) {
        auto buffer = access.buffer;
        auto it = m_buffer_parents.find(buffer);
        assert(it != m_buffer_parents.end());
        auto parent = it->second;
        rendergraphDebug("      - Buffer {} ({}) -> Buffer {} ({})", parent,
                         m_buffer_names[parent], buffer,
                         m_buffer_names[buffer]);
      }
    }

    auto is_texture_create = [&](const RGTextureAccess &access) {
      return !m_texture_parents.contains(access.texture);
    };

    auto create_textures = pass.write_textures | filter(is_texture_create);

    auto write_textures =
        pass.write_textures | ranges::views::remove_if(is_texture_create);

    if (!create_textures.empty()) {
      rendergraphDebug("    Creates textures:");
      for (const auto &access : create_textures) {
        auto texture = access.texture;
        rendergraphDebug("      - Texture {} ({})", texture,
                         m_texture_names[texture]);
      }
    }

    if (!pass.read_textures.empty()) {
      rendergraphDebug("    Reads textures:");
      for (const auto &read_texture : pass.read_textures) {
        auto texture = read_texture.texture;
        rendergraphDebug("      - Texture {} ({})", texture,
                         m_texture_names[texture]);
      }
    }

    if (!write_textures.empty()) {
      rendergraphDebug("    Writes textures:");
      for (const auto &access : write_textures) {
        auto texture = access.texture;
        auto it = m_texture_parents.find(texture);
        assert(it != m_texture_parents.end());
        auto parent = it->second;
        rendergraphDebug("      - Texture {} ({}) -> Texture {} ({})", parent,
                         m_texture_names[parent], texture,
                         m_texture_names[texture]);
      }
    }

    rendergraphDebug("");
  }
}

void RenderGraph::Builder::create_textures(const Device &device,
                                           ResourceArena &arena) {
  for (const auto &[texture, create_info] : m_texture_create_infos) {
    m_textures[texture] = device.get_texture_view(arena.create_texture({
        REN_SET_DEBUG_NAME(std::move(create_info.debug_name)),
        .type = create_info.type,
        .format = create_info.format,
        .usage = m_texture_usage_flags[texture],
        .width = create_info.width,
        .height = create_info.height,
        .array_layers = create_info.array_layers,
        .mip_levels = create_info.mip_levels,
    }));
  }
  for (auto [texture, physical_texture] : enumerate(m_physical_textures)) {
    m_textures[texture] = m_textures[physical_texture];
  }
}

void RenderGraph::Builder::create_buffers(const Device &device,
                                          ResourceArena &arena) {
  for (const auto &[buffer, create_info] : m_buffer_create_infos) {
    m_buffers[buffer] = device.get_buffer_view(arena.create_buffer({
        REN_SET_DEBUG_NAME(std::move(create_info.debug_name)),
        .heap = create_info.heap,
        .usage = m_buffer_usage_flags[buffer],
        .size = create_info.size,
    }));
  }
  for (auto [buffer, physical_buffer] : enumerate(m_physical_buffers)) {
    m_buffers[buffer] = m_buffers[physical_buffer];
  }
}

void RenderGraph::Builder::insert_barriers(Device &device) {
  for (auto &pass : m_passes) {
    auto memory_barriers = concat(pass.read_buffers, pass.write_buffers) |
                           filter_map([&](const RGBufferAccess &buffer_access)
                                          -> Optional<VkMemoryBarrier2> {
                             auto physical_buffer =
                                 m_physical_buffers[buffer_access.buffer];
                             auto &state = m_buffer_states[physical_buffer];

                             if (state.accesses == VK_ACCESS_2_NONE or
                                 buffer_access.accesses == VK_ACCESS_2_NONE) {
                               return None;
                             }

                             VkMemoryBarrier2 barrier = {
                                 .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                 .srcStageMask = state.stages,
                                 .srcAccessMask = state.accesses,
                                 .dstStageMask = buffer_access.stages,
                                 .dstAccessMask = buffer_access.accesses,
                             };

                             state = {
                                 .accesses = buffer_access.accesses,
                                 .stages = buffer_access.stages,
                             };

                             return barrier;
                           }) |
                           ranges::to<Vector>();

    auto image_barriers =
        concat(pass.read_textures, pass.write_textures) |
        map([&](const RGTextureAccess &texture_access) {
          auto physical_texture = m_physical_textures[texture_access.texture];
          auto &state = m_texture_states[physical_texture];
          const auto &view = m_textures[physical_texture];

          VkImageMemoryBarrier2 barrier = {
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
              .srcStageMask = state.stages,
              .srcAccessMask = state.accesses,
              .dstStageMask = texture_access.stages,
              .dstAccessMask = texture_access.accesses,
              .oldLayout = state.layout,
              .newLayout = texture_access.layout,
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

          state = {
              .accesses = texture_access.accesses,
              .stages = texture_access.stages,
              .layout = texture_access.layout,
          };

          return barrier;
        }) |
        ranges::to<Vector>();

    pass.barrier_cb = [memory_barriers = std::move(memory_barriers),
                       image_barriers = std::move(image_barriers)](
                          Device &device, RenderGraph &rg, CommandBuffer &cmd) {
      cmd.pipeline_barrier(memory_barriers, image_barriers);
    };
  }
}

auto RenderGraph::Builder::batch_passes(std::span<const RGPassID> schedule)
    -> Vector<RGBatch> {
  Vector<RGBatch> batches;
  bool begin_new_batch = true;
  for (auto passid : schedule) {
    auto &pass = m_passes[passid];
    auto &name = m_pass_names[passid];
    if (!pass.wait_semaphores.empty()) {
      begin_new_batch = true;
    }
    if (begin_new_batch) {
      auto &batch = batches.emplace_back();
      batch.wait_semaphores = std::move(pass.wait_semaphores);
      begin_new_batch = false;
    }
    auto &batch = batches.back();
    batch.barrier_cbs.push_back(std::move(pass.barrier_cb));
    batch.pass_cbs.push_back(std::move(pass.pass_cb));
    batch.pass_names.push_back(std::move(name));
    if (!pass.signal_semaphores.empty()) {
      batch.signal_semaphores = std::move(pass.signal_semaphores);
      begin_new_batch = true;
    }
  }
  return batches;
}

auto RenderGraph::Builder::build(Device &device, ResourceArena &arena)
    -> RenderGraph {
  rendergraphDebug("### Build RenderGraph ###");
  rendergraphDebug("");

  rendergraphDebug("Create buffers");
  rendergraphDebug("");
  create_buffers(device, arena);
  rendergraphDebug("Create textures");
  rendergraphDebug("");
  create_textures(device, arena);
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
  auto batches = batch_passes(schedule);

  rendergraphDebug("### Build done ###");
  rendergraphDebug("");
  return {{
      .batches = std::move(batches),
      .textures = std::move(m_textures),
      .buffers = std::move(m_buffers),
      .swapchain = m_swapchain,
      .present_semaphore = m_present_semaphore,
  }};
}

auto RenderGraph::get_texture(RGTextureID texture) const -> TextureView {
  assert(texture);
  return m_textures[texture];
}

auto RenderGraph::get_buffer(RGBufferID buffer) const -> BufferView {
  assert(buffer);
  return m_buffers[buffer];
}

void RenderGraph::execute(Device &device, CommandAllocator &cmd_allocator) {
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffers;
  SmallVector<VkSemaphoreSubmitInfo> wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> signal_semaphores;

  for (auto &batch : m_batches) {
    cmd_buffers.clear();

    for (auto &&[barrier_cb, pass_cb, pass_name] :
         zip(batch.barrier_cbs, batch.pass_cbs, batch.pass_names)) {
      auto cmd = cmd_allocator.allocate();
      cmd.begin();
      if (barrier_cb) {
        barrier_cb(device, *this, cmd);
      }
      if (pass_cb) {
        cmd.begin_debug_region(pass_name.c_str());
        pass_cb(device, *this, cmd);
        cmd.end_debug_region();
      }
      cmd.end();
      cmd_buffers.push_back(
          {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
           .commandBuffer = cmd.get()});
    }

    auto get_semaphore_submit_info = [&](const RGSemaphoreSignal &signal) {
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
}

} // namespace ren
