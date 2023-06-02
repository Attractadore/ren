#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
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

namespace ren {
namespace {

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

} // namespace

auto RenderGraph::Builder::init_new_pass(RGDebugName name) -> RGPassID {
  auto pass = m_passes.size();
  m_passes.emplace_back();
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_pass_names.push_back(std::move(name));
#endif
  return static_cast<RGPassID>(pass);
}

auto RenderGraph::Builder::getPassID(const RGPass &pass) const -> RGPassID {
  return static_cast<RGPassID>(&pass - m_passes.data());
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
  auto pass = init_new_pass(std::move(create_info.name));
  return {pass, this};
}

auto RenderGraph::Builder::init_new_texture(Optional<RGPassID> pass,
                                            Optional<RGTextureID> from_texture,
                                            RGDebugName name) -> RGTextureID {
  auto texture = static_cast<RGTextureID>(m_textures.size());
  from_texture.map_or_else(
      [&](RGTextureID from_texture) {
        m_textures.push_back(m_textures[from_texture]);
      },
      [&] {
        m_textures.push_back(m_physical_textures.size());
        m_physical_textures.emplace_back();
        m_texture_states.emplace_back();
      });
  pass.map([&](RGPassID pass) {
    m_texture_defs[texture] = pass;
    from_texture.map([&](RGTextureID from_texture) {
      m_texture_kills[from_texture] = pass;
    });
  });
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_texture_names.push_back(std::move(name));
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

void RenderGraph::Builder::read_texture(RGPassID pass,
                                        RGTextureReadInfo &&read_info) {
  m_passes[pass].read_textures.push_back({
      .texture = read_info.texture,
      .state = read_info.state,
  });
  auto it = m_texture_create_infos.find(m_textures[read_info.texture]);
  if (it != m_texture_create_infos.end()) {
    it->second.usage |= get_texture_usage_flags(read_info.state.accesses);
  }
}

auto RenderGraph::Builder::write_texture(RGPassID pass,
                                         RGTextureWriteInfo &&write_info)
    -> RGTextureID {
  auto new_texture =
      init_new_texture(pass, write_info.texture, std::move(write_info.name));
  m_passes[pass].write_textures.push_back({
      .texture = write_info.texture,
      .state = write_info.state,
  });
  auto it = m_texture_create_infos.find(m_textures[write_info.texture]);
  if (it != m_texture_create_infos.end()) {
    it->second.usage |= get_texture_usage_flags(write_info.state.accesses);
  }
  return new_texture;
}

auto RenderGraph::Builder::create_texture(RGPassID pass,
                                          RGTextureCreateInfo &&create_info)
    -> RGTextureID {
  auto new_texture = init_new_texture(pass, None, std::move(create_info.name));
  auto physical_texture = m_textures[new_texture];
  if (create_info.preserve) {
    m_preserved_textures.insert(physical_texture);
  }
  m_texture_create_infos[physical_texture] = {
      REN_SET_DEBUG_NAME(
          fmt::format("RenderGraph Texture {}", physical_texture)),
      .type = create_info.type,
      .format = create_info.format,
      .usage = get_texture_usage_flags(create_info.state.accesses),
      .size = create_info.size,
      .num_mip_levels = create_info.num_mip_levels,
  };
  m_passes[pass].write_textures.push_back({
      .texture = new_texture,
      .state = create_info.state,
  });
  return new_texture;
}

auto RenderGraph::Builder::import_texture(RGTextureImportInfo &&import_info)
    -> RGTextureID {
  auto new_texture = init_new_texture(None, None, std::move(import_info.name));
  auto physical_texture = m_textures[new_texture];
  m_physical_textures[physical_texture] = import_info.texture;
  m_texture_states[physical_texture] = import_info.state;
  return new_texture;
}

auto RenderGraph::Builder::init_new_buffer(Optional<RGPassID> pass,
                                           Optional<RGBufferID> from_buffer,
                                           RGDebugName name) -> RGBufferID {
  auto buffer = static_cast<RGBufferID>(m_buffers.size());
  from_buffer.map_or_else(
      [&](RGBufferID from_buffer) {
        m_buffers.push_back(m_buffers[from_buffer]);
      },
      [&] {
        m_buffers.push_back(m_physical_buffers.size());
        m_physical_buffers.emplace_back();
        m_buffer_states.emplace_back();
      });
  pass.map([&](RGPassID pass) {
    m_buffer_defs[buffer] = pass;
    from_buffer.map(
        [&](RGBufferID from_buffer) { m_buffer_kills[from_buffer] = pass; });
  });
#if REN_RENDER_GRAPH_DEBUG_NAMES
  m_buffer_names.push_back(std::move(name));
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

void RenderGraph::Builder::read_buffer(RGPassID pass,
                                       RGBufferReadInfo &&read_info) {
  m_passes[pass].read_buffers.push_back({
      .buffer = read_info.buffer,
      .state = read_info.state,
  });
  auto it = m_buffer_create_infos.find(m_buffers[read_info.buffer]);
  if (it != m_buffer_create_infos.end()) {
    it->second.usage |= get_buffer_usage_flags(read_info.state.accesses);
  }
}

auto RenderGraph::Builder::write_buffer(RGPassID pass,
                                        RGBufferWriteInfo &&write_info)
    -> RGBufferID {
  auto new_buffer =
      init_new_buffer(pass, write_info.buffer, std::move(write_info.name));
  m_passes[pass].write_buffers.push_back({
      .buffer = write_info.buffer,
      .state = write_info.state,
  });
  auto it = m_buffer_create_infos.find(m_buffers[write_info.buffer]);
  if (it != m_buffer_create_infos.end()) {
    it->second.usage |= get_buffer_usage_flags(write_info.state.accesses);
  }
  return new_buffer;
}

auto RenderGraph::Builder::create_buffer(RGPassID pass,
                                         RGBufferCreateInfo &&create_info)
    -> RGBufferID {
  assert(create_info.size > 0);
  auto new_buffer = init_new_buffer(pass, None, std::move(create_info.name));
  auto physical_buffer = m_buffers[new_buffer];
  if (create_info.preserve) {
    m_preserved_buffers.insert(physical_buffer);
  }
  m_buffer_create_infos[physical_buffer] = {
      REN_SET_DEBUG_NAME(fmt::format("RenderGraph Buffer {}", physical_buffer)),
      .heap = create_info.heap,
      .usage = get_buffer_usage_flags(create_info.state.accesses),
      .size = create_info.size,
  };
  m_passes[pass].write_buffers.push_back({
      .buffer = new_buffer,
      .state = create_info.state,
  });
  return new_buffer;
}

auto RenderGraph::Builder::import_buffer(RGBufferImportInfo &&import_info)
    -> RGBufferID {
  auto new_buffer = init_new_buffer(None, None, std::move(import_info.name));
  auto physical_buffer = m_buffers[new_buffer];
  m_physical_buffers[physical_buffer] = import_info.buffer;
  m_buffer_states[physical_buffer] = import_info.state;
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

  auto swapchain_image = import_texture(RGTextureImportInfo{
      .name = "Swapchain image",
      .texture = m_swapchain->getTexture(),
  });

  auto blit = create_pass({
      .name = "Blit to swapchain",
  });

  blit.read_texture({
      .texture = texture,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_BLIT_BIT,
              .accesses = VK_ACCESS_2_TRANSFER_READ_BIT,
              .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          },
  });

  auto blitted_swapchain_image = blit.write_texture({
      .name = "Swapchain image after blit",
      .texture = swapchain_image,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_BLIT_BIT,
              .accesses = VK_ACCESS_2_TRANSFER_WRITE_BIT,
              .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          },
  });

  blit.wait_semaphore({
      .semaphore = acquire_semaphore,
      .stages = VK_PIPELINE_STAGE_2_BLIT_BIT,
  });
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

  auto present = create_pass({
      .name = "Present",
  });

  present.read_texture({
      .texture = blitted_swapchain_image,
      .state =
          {
              .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          },
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
  auto get_dependants = [&](const RGPass &pass) -> std::span<const RGPassID> {
    // Reads must happen before writes
    dependents.assign(concat(pass.read_textures | filter_map(get_texture_kill),
                             pass.read_buffers | filter_map(get_buffer_kill)));
    return dependents;
  };

  SmallVector<RGPassID> dependencies;
  auto get_dependencies = [&](const RGPass &pass) -> std::span<const RGPassID> {
    dependencies.assign(concat(
        // Reads must happen after creation
        pass.read_textures | filter_map(get_texture_def),
        pass.read_buffers | filter_map(get_buffer_def),
        // Writes must happen after creation
        pass.write_textures | filter_map(get_texture_def),
        pass.write_buffers | filter_map(get_buffer_def)));
    ranges::actions::unstable_remove_if(
        dependencies, [&](RGPassID dep) { return dep == getPassID(pass); });
    return dependencies;
  };

  // Schedule passes whose dependencies were scheduled the longest time ago
  // first
  MinQueue<std::tuple<int, RGPassID>> unscheduled_passes;

  // Build DAG
  for (const auto &pass : m_passes | ranges::views::drop(1)) {
    auto id = getPassID(pass);
    auto predecessors = get_dependencies(pass);
    auto successors = get_dependants(pass);

    for (auto p : predecessors) {
      add_edge(p, id);
    }

    for (auto s : successors) {
      add_edge(id, s);
    }

    if (predecessors.empty()) {
      // This is a pass with no dependencies and it can be scheduled right
      // away
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
#if REN_RENDER_GRAPH_DEBUG_NAMES
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
#endif
}

void RenderGraph::Builder::print_passes(
    std::span<const RGPassID> passes) const {
#if REN_RENDER_GRAPH_DEBUG_NAMES
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
        auto new_buffer =
            ranges::find_if(m_buffer_defs,
                            [&](const std::pair<RGBufferID, RGPassID> &kv) {
                              auto [new_buffer, def] = kv;
                              return m_buffers[new_buffer] ==
                                         m_buffers[buffer] and
                                     def == passid;
                            })
                ->first;
        rendergraphDebug("      - Buffer {} ({}) -> Buffer {} ({})", buffer,
                         m_buffer_names[buffer], new_buffer,
                         m_buffer_names[new_buffer]);
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
        auto new_texture =
            ranges::find_if(m_texture_defs,
                            [&](const std::pair<RGTextureID, RGPassID> &kv) {
                              auto [new_texture, def] = kv;
                              return m_textures[new_texture] ==
                                         m_textures[texture] and
                                     def == passid;
                            })
                ->first;
        rendergraphDebug("      - Texture {} ({}) -> Texture {} ({})", texture,
                         m_texture_names[texture], new_texture,
                         m_texture_names[new_texture]);
      }
    }

    rendergraphDebug("");
  }
#endif
}

void RenderGraph::Builder::create_buffers(const Device &device,
                                          ResourceArena &frame_arena,
                                          ResourceArena &next_frame_arena) {
  for (const auto &[buffer, create_info] : m_buffer_create_infos) {
    auto &arena =
        m_preserved_buffers.contains(buffer) ? next_frame_arena : frame_arena;
    m_physical_buffers[buffer] =
        device.get_buffer_view(arena.create_buffer(std::move(create_info)));
  }
}

void RenderGraph::Builder::create_textures(const Device &device,
                                           ResourceArena &frame_arena,
                                           ResourceArena &next_frame_arena) {
  for (const auto &[texture, create_info] : m_texture_create_infos) {
    auto &arena =
        m_preserved_textures.contains(texture) ? next_frame_arena : frame_arena;
    m_physical_textures[texture] = device.get_texture_view(
        frame_arena.create_texture(std::move(create_info)));
  }
}

void RenderGraph::Builder::insert_barriers(Device &device) {
  for (auto &pass : m_passes) {
    auto memory_barriers =
        concat(pass.read_buffers, pass.write_buffers) |
        filter_map([&](const RGBufferAccess &buffer_access)
                       -> Optional<VkMemoryBarrier2> {
          auto physical_buffer = m_buffers[buffer_access.buffer];
          auto &state = m_buffer_states[physical_buffer];

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
          auto physical_texture = m_textures[texture_access.texture];
          auto &state = m_texture_states[physical_texture];
          const auto &view = m_physical_textures[physical_texture];

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

auto RenderGraph::Builder::build(Device &device, ResourceArena &frame_arena,
                                 ResourceArena &next_frame_arena)
    -> RenderGraph {
  rendergraphDebug("### Build RenderGraph ###");
  rendergraphDebug("");

  rendergraphDebug("Create buffers");
  rendergraphDebug("");
  create_buffers(device, frame_arena, next_frame_arena);
  rendergraphDebug("Create textures");
  rendergraphDebug("");
  create_textures(device, frame_arena, next_frame_arena);
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
      .buffers = std::move(m_buffers),
      .physical_buffers = std::move(m_physical_buffers),
      .buffer_states = std::move(m_buffer_states),
      .textures = std::move(m_textures),
      .physical_textures = std::move(m_physical_textures),
      .texture_states = std::move(m_texture_states),
      .swapchain = m_swapchain,
      .present_semaphore = m_present_semaphore,
  }};
}

auto RenderGraph::get_buffer(RGBufferID buffer) const -> const BufferView & {
  assert(buffer);
  return m_physical_buffers[m_buffers[buffer]];
}

auto RenderGraph::get_texture(RGTextureID texture) const
    -> const TextureView & {
  assert(texture);
  return m_physical_textures[m_textures[texture]];
}

auto RenderGraph::export_buffer(RGBufferID buffer) const -> RGBufferExportInfo {
  assert(buffer);
  auto physical_buffer = m_buffers[buffer];
  return {
      .buffer = m_physical_buffers[physical_buffer],
      .state = m_buffer_states[physical_buffer],
  };
};

auto RenderGraph::export_texture(RGTextureID texture) const
    -> RGTextureExportInfo {
  assert(texture);
  auto physical_texture = m_textures[texture];
  return {
      .texture = m_physical_textures[physical_texture],
      .state = m_texture_states[physical_texture],
  };
};

void RenderGraph::execute(Device &device, CommandAllocator &cmd_allocator) {
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffers;
  SmallVector<VkSemaphoreSubmitInfo> wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> signal_semaphores;

  for (auto &batch : m_batches) {
    cmd_buffers.clear();

    for (auto index : range(batch.pass_cbs.size())) {
      const auto &barrier_cb = batch.barrier_cbs[index];
      const auto &pass_cb = batch.pass_cbs[index];

      auto cmd = cmd_allocator.allocate();
      cmd.begin();
      if (barrier_cb) {
        barrier_cb(device, *this, cmd);
      }
      if (pass_cb) {
#if REN_RENDER_GRAPH_DEBUG_NAMES
        auto pass_name = batch.pass_names[index].c_str();
        cmd.begin_debug_region(pass_name);
#endif
        pass_cb(device, *this, cmd);
#if REN_RENDER_GRAPH_DEBUG_NAMES
        cmd.end_debug_region();
#endif
      }
      cmd.end();

      cmd_buffers.push_back(
          {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
           .commandBuffer = cmd.get()});
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
}

} // namespace ren
