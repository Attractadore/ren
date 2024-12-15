#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "Profiler.hpp"
#include "Swapchain.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <fmt/format.h>

namespace ren {

RgPersistent::RgPersistent(Renderer &renderer) : m_texture_arena(renderer) {}

auto RgPersistent::create_texture(RgTextureCreateInfo &&create_info)
    -> RgTextureId {
#if REN_RG_DEBUG
  ren_assert(not create_info.name.empty());
#endif

  RgPhysicalTextureId physical_texture_id(m_physical_textures.size());
  m_physical_textures.resize(m_physical_textures.size() +
                             RG_MAX_TEMPORAL_LAYERS);
  m_persistent_textures.resize(m_physical_textures.size());
  m_external_textures.resize(m_physical_textures.size());

  VkImageUsageFlags usage = 0;
  u32 num_temporal_layers = 1;
  bool is_external = false;

  create_info.ext.visit(OverloadSet{
      [](Monostate) {},
      [&](const RgTextureExternalInfo &ext) {
        ren_assert(ext.usage);
        usage = ext.usage;
        is_external = true;
      },
      [&](const RgTextureTemporalInfo &ext) {
        ren_assert(ext.num_temporal_layers > 1);
        usage =
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        num_temporal_layers = ext.num_temporal_layers;
        m_texture_init_info[physical_texture_id] = {
            .usage = ext.usage,
            .cb = std::move(ext.cb),
        };
      },
  });

  for (usize i : range(num_temporal_layers)) {
    RgPhysicalTextureId id(physical_texture_id + i);

#if REN_RG_DEBUG
    String handle_name, init_name, name;
    if (is_external) {
      handle_name = create_info.name;
      name = std::move(create_info.name);
    } else {
      if (num_temporal_layers == 1) {
        handle_name = std::move(create_info.name);
      } else {
        handle_name = fmt::format("{}#{}", create_info.name, i);
      }
      if (i == 0) {
        name = fmt::format("rg#{}", handle_name);
      } else {
        init_name = fmt::format("rg#{}", handle_name);
        name = handle_name;
      }
    }
#endif

    RgTextureId init_id;
    if (i > 0) {
      init_id = m_textures.insert({
#if REN_RG_DEBUG
          .name = std::move(init_name),
#endif
          .parent = id,
      });
    }

    m_physical_textures[id] = {
#if REN_RG_DEBUG
        .name = std::move(handle_name),
#endif
        .type = create_info.type,
        .format = create_info.format,
        .usage = usage,
        .size = {create_info.width, create_info.height, create_info.depth},
        .num_mip_levels = create_info.num_mip_levels,
        .num_array_layers = create_info.num_array_layers,
        .init_id = init_id,
        .id = m_textures.insert({
#if REN_RG_DEBUG
            .name = std::move(name),
#endif
            .parent = id,
        }),
    };

    m_persistent_textures[id] = i > 0;
    m_external_textures[id] = is_external;
  }

  return m_physical_textures[physical_texture_id].id;
}

auto RgPersistent::create_external_semaphore(RgDebugName name)
    -> RgSemaphoreId {
  RgSemaphoreId semaphore = m_semaphores.emplace();
#if REN_RG_DEBUG
  m_semaphores[semaphore].name = std::move(name);
#endif
  return semaphore;
}

void RgPersistent::reset() {
  m_texture_arena.clear();
  m_physical_textures.clear();
  m_persistent_textures.clear();
  m_external_textures.clear();
  m_textures.clear();
  m_texture_init_info.clear();
  m_semaphores.clear();
}

void RgPersistent::rotate_textures() {
  ren_assert(m_physical_textures.size() % RG_MAX_TEMPORAL_LAYERS == 0);
  for (usize i = 0; i < m_physical_textures.size();
       i += RG_MAX_TEMPORAL_LAYERS) {
    RgPhysicalTexture &physical_texture = m_physical_textures[i];
#if REN_RG_DEBUG
    String name = std::move(physical_texture.name);
#endif
    VkImageUsageFlags usage = physical_texture.usage;
    Handle<Texture> handle = physical_texture.handle;
    TextureState state = physical_texture.state;
    usize last = i + 1;
    for (; last < i + RG_MAX_TEMPORAL_LAYERS; ++last) {
      RgPhysicalTexture &prev_physical_texture = m_physical_textures[last - 1];
      RgPhysicalTexture &cur_physical_texture = m_physical_textures[last];
      if (!cur_physical_texture.handle) {
        break;
      }
#if REN_RG_DEBUG
      prev_physical_texture.name = std::move(cur_physical_texture.name);
#endif
      prev_physical_texture.usage = cur_physical_texture.usage;
      prev_physical_texture.handle = cur_physical_texture.handle;
      prev_physical_texture.state = cur_physical_texture.state;
    }
    RgPhysicalTexture &last_physical_texture = m_physical_textures[last - 1];
#if REN_RG_DEBUG
    last_physical_texture.name = std::move(physical_texture.name);
#endif
    last_physical_texture.usage = usage;
    last_physical_texture.handle = handle;
    last_physical_texture.state = state;
  }
}

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

RgBuilder::RgBuilder(RgPersistent &rgp, Renderer &renderer,
                     DescriptorAllocatorScope &descriptor_allocator) {
  m_renderer = &renderer;
  m_rgp = &rgp;
  m_data = &rgp.m_build_data;
  m_rt_data = &rgp.m_rt_data;
  m_descriptor_allocator = &descriptor_allocator;

  for (auto &&[_, texture] : m_rgp->m_textures) {
    texture.def = {};
    texture.kill = {};
#if REN_RG_DEBUG
    texture.child = {};
#endif
  }

  auto &bd = *m_data;
  bd.m_passes.clear();
  bd.m_schedule.clear();
  bd.m_physical_buffers.clear();
  bd.m_buffers.clear();
  bd.m_buffer_uses.clear();
  bd.m_texture_uses.clear();
  bd.m_semaphore_signals.clear();

  auto &gd = *m_rt_data;
#if REN_RG_DEBUG
  gd.m_pass_names.clear();
#endif
  gd.m_color_attachments.clear();
  gd.m_depth_stencil_attachments.clear();
}

auto RgBuilder::create_pass(RgPassCreateInfo &&create_info) -> RgPassBuilder {
  RgPassId pass = m_data->m_passes.emplace();
#if REN_RG_DEBUG
  m_rt_data->m_pass_names.insert(pass, std::move(create_info.name));
#endif
  m_data->m_schedule.push_back(pass);
  return RgPassBuilder(pass, *this);
}

auto RgBuilder::add_buffer_use(RgUntypedBufferId buffer,
                               const BufferState &usage, u32 offset)
    -> RgBufferUseId {
  ren_assert(buffer);
  RgBufferUseId id(m_data->m_buffer_uses.size());
  m_data->m_buffer_uses.push_back({
      .buffer = buffer,
      .offset = offset,
      .usage = usage,
  });
  m_rt_data->m_buffers.resize(m_data->m_buffer_uses.size());
  return id;
};

auto RgBuilder::create_virtual_buffer(RgPassId pass, RgDebugName name,
                                      RgUntypedBufferId parent)
    -> RgUntypedBufferId {
  RgPhysicalBufferId physical_buffer;
  if (parent) {
    physical_buffer = m_data->m_buffers[parent].parent;
  } else {
    ren_assert(!pass);
    physical_buffer = RgPhysicalBufferId(m_data->m_physical_buffers.size());
    m_data->m_physical_buffers.emplace_back();
  }

  RgUntypedBufferId buffer = m_data->m_buffers.insert({
#if REN_RG_DEBUG
      .name = std::move(name),
#endif
      .parent = physical_buffer,
      .def = pass,
  });

  if (parent) {
    ren_assert(pass);
    m_data->m_buffers[parent].kill = pass;
  }

#if REN_RG_DEBUG
  if (parent) {
    ren_assert_msg(!m_data->m_buffers[parent].child,
                   "Render graph buffers can only be written once");
    m_data->m_buffers[parent].child = buffer;
  }
#endif

  return buffer;
}

auto RgBuilder::create_buffer(RgDebugName name, BufferHeap heap, usize size)
    -> RgUntypedBufferId {
  RgUntypedBufferId buffer =
      create_virtual_buffer(NullHandle, std::move(name), NullHandle);
  RgPhysicalBufferId physical_buffer = m_data->m_buffers[buffer].parent;
  m_data->m_physical_buffers[physical_buffer] = {
      .heap = heap,
      .size = size,
  };
  return buffer;
}

auto RgBuilder::read_buffer(RgPassId pass, RgUntypedBufferId buffer,
                            const BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  ren_assert(buffer);
  RgBufferUseId use = add_buffer_use(buffer, usage, offset);
  m_data->m_passes[pass].read_buffers.push_back(use);
  return RgUntypedBufferToken(use);
}

auto RgBuilder::write_buffer(RgPassId pass, RgDebugName name,
                             RgUntypedBufferId src, const BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  ren_assert(src);
  ren_assert(m_data->m_buffers[src].def != pass);
  RgBufferUseId use = add_buffer_use(src, usage);
  m_data->m_passes[pass].write_buffers.push_back(use);
  RgUntypedBufferId dst = create_virtual_buffer(pass, std::move(name), src);
  return {dst, RgUntypedBufferToken(use)};
}

void RgBuilder::clear_texture(RgDebugName name, NotNull<RgTextureId *> texture,
                              const glm::vec4 &value) {
  auto pass = create_pass({"clear-texture"});
  auto token =
      pass.write_texture(std::move(name), texture, TRANSFER_DST_TEXTURE);
  pass.set_callback(
      [token, value](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
        cmd.clear_texture(rg.get_texture(token), value);
      });
}

auto RgBuilder::add_texture_use(RgTextureId texture, const TextureState &usage,
                                Handle<Sampler> sampler) -> RgTextureUseId {
  ren_assert(texture);
  RgTextureUseId id(m_data->m_texture_uses.size());
  m_data->m_texture_uses.push_back({
      .texture = texture,
      .sampler = sampler,
      .state = usage,
  });
  return id;
}

auto RgBuilder::create_virtual_texture(RgPassId pass, RgDebugName name,
                                       RgTextureId parent) -> RgTextureId {
  ren_assert(pass);
  ren_assert(parent);
  RgPhysicalTextureId physical_texture = m_rgp->m_textures[parent].parent;
  RgTextureId texture = m_rgp->m_textures.insert({
#if REN_RG_DEBUG
      .name = std::move(name),
#endif
      .parent = physical_texture,
      .def = pass,
  });
  m_rgp->m_frame_textures.push_back(texture);
  m_rgp->m_textures[parent].kill = pass;
#if REN_RG_DEBUG
  ren_assert_msg(!m_rgp->m_textures[parent].child,
                 "Render graph textures can only be written once");
  m_rgp->m_textures[parent].child = texture;
#endif
  return texture;
}

auto RgBuilder::read_texture(RgPassId pass, RgTextureId texture,
                             const TextureState &usage, Handle<Sampler> sampler,
                             u32 temporal_layer) -> RgTextureToken {
  ren_assert(texture);
  if (sampler) {
    ren_assert_msg(usage.access_mask & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   "Sampler must be null if texture is not sampled");
  }
  RgPhysicalTextureId physical_texture = m_rgp->m_textures[texture].parent;
  if (temporal_layer) {
    ren_assert_msg(
        texture == m_rgp->m_physical_textures[physical_texture].id,
        "Only the first declaration of a temporal texture can be used to "
        "read a previous temporal layer");
    ren_assert(temporal_layer < RG_MAX_TEMPORAL_LAYERS);
    texture = m_rgp->m_physical_textures[physical_texture + temporal_layer].id;
    ren_assert_msg(texture, "Temporal layer index out of range");
  }
  RgTextureUseId use = add_texture_use(texture, usage, sampler);
  m_data->m_passes[pass].read_textures.push_back(use);
  return RgTextureToken(use);
}

auto RgBuilder::write_texture(RgPassId pass, RgDebugName name, RgTextureId src,
                              const TextureState &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  ren_assert(src);
  ren_assert(m_rgp->m_textures[src].def != pass);
  RgTextureUseId use = add_texture_use(src, usage);
  m_data->m_passes[pass].write_textures.push_back(use);
  RgTextureId dst = create_virtual_texture(pass, std::move(name), src);
  return {dst, RgTextureToken(use)};
}

auto RgBuilder::write_texture(RgPassId pass, RgTextureId dst_id,
                              RgTextureId src_id, const TextureState &usage)
    -> RgTextureToken {
  ren_assert(pass);
  ren_assert(dst_id);
  ren_assert(src_id);
  RgTexture &dst = m_rgp->m_textures[dst_id];
  RgTexture &src = m_rgp->m_textures[src_id];
  ren_assert(dst.parent == src.parent);
  dst.def = pass;
  src.kill = pass;
#if REN_RG_DEBUG
  ren_assert(!src.child);
  src.child = dst_id;
#endif
  RgTextureUseId use = add_texture_use(src_id, usage);
  m_data->m_passes[pass].write_textures.push_back(use);
  return RgTextureToken(use);
}

void RgBuilder::set_external_buffer(RgUntypedBufferId id,
                                    const BufferView &view,
                                    const BufferState &state) {
  RgPhysicalBufferId physical_buffer_id = m_data->m_buffers[id].parent;
  RgPhysicalBuffer &physical_buffer =
      m_data->m_physical_buffers[physical_buffer_id];
  ren_assert(!physical_buffer.view.buffer);
  physical_buffer.view = view;
  physical_buffer.state = state;
}

void RgBuilder::set_external_texture(RgTextureId id, Handle<Texture> handle,
                                     const TextureState &state) {
  RgPhysicalTextureId physical_texture_id = m_rgp->m_textures[id].parent;
  ren_assert(m_rgp->m_external_textures[physical_texture_id]);
  RgPhysicalTexture &physical_texture =
      m_rgp->m_physical_textures[physical_texture_id];
  physical_texture.handle = handle;
  physical_texture.state = state;
}

auto RgBuilder::add_semaphore_signal(RgSemaphoreId semaphore,
                                     VkPipelineStageFlags2 stage_mask,
                                     u64 value) -> RgSemaphoreSignalId {
  RgSemaphoreSignalId id(m_data->m_semaphore_signals.size());
  m_data->m_semaphore_signals.push_back({
      .semaphore = semaphore,
      .stage_mask = stage_mask,
      .value = value,
  });
  return id;
}

void RgBuilder::wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                               VkPipelineStageFlags2 stage_mask, u64 value) {
  m_data->m_passes[pass].wait_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
}

void RgBuilder::signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                                 VkPipelineStageFlags2 stage_mask, u64 value) {
  m_data->m_passes[pass].signal_semaphores.push_back(
      add_semaphore_signal(semaphore, stage_mask, value));
}

void RgBuilder::set_external_semaphore(RgSemaphoreId semaphore,
                                       Handle<Semaphore> handle) {
  m_rgp->m_semaphores[semaphore].handle = handle;
}

void RgBuilder::dump_pass_schedule() const {
#if REN_RG_DEBUG
  fmt::println(stderr, "Scheduled passes:");

  SmallVector<RgUntypedBufferId> create_buffers;
  SmallVector<RgUntypedBufferId> write_buffers;
  SmallVector<RgTextureId> create_textures;
  SmallVector<RgTextureId> write_textures;

  for (RgPassId pass_id : m_data->m_schedule) {
    const RgPass &pass = m_data->m_passes[pass_id];

    fmt::println(stderr, "  * {}", m_rt_data->m_pass_names[pass_id]);

    const auto &buffers = m_data->m_buffers;
    const auto &buffer_uses = m_data->m_buffer_uses;

    create_buffers.clear();
    write_buffers.clear();
    for (RgBufferUseId use : pass.write_buffers) {
      RgUntypedBufferId id = buffer_uses[use].buffer;
      const RgBuffer &buffer = buffers[id];
      if (buffer.name.empty()) {
        create_buffers.push_back(buffer.child);
      } else {
        write_buffers.push_back(id);
      }
    }

    if (not create_buffers.empty()) {
      fmt::println(stderr, "    Creates buffers:");
      for (RgUntypedBufferId buffer : create_buffers) {
        fmt::println(stderr, "      - {}", buffers[buffer].name);
      }
    }
    if (not pass.read_buffers.empty()) {
      fmt::println(stderr, "    Reads buffers:");
      for (RgBufferUseId use : pass.read_buffers) {
        fmt::println(stderr, "      - {}",
                     buffers[buffer_uses[use].buffer].name);
      }
    }
    if (not write_buffers.empty()) {
      fmt::println(stderr, "    Writes buffers:");
      for (RgUntypedBufferId src_id : write_buffers) {
        const RgBuffer &src = buffers[src_id];
        const RgBuffer &dst = buffers[src.child];
        fmt::println(stderr, "      - {} -> {}", src.name, dst.name);
      }
    }

    const auto &textures = m_rgp->m_textures;
    const auto &texture_uses = m_data->m_texture_uses;

    create_textures.clear();
    write_textures.clear();
    for (RgTextureUseId use : pass.write_textures) {
      RgTextureId id = texture_uses[use].texture;
      const RgTexture &texture = textures[id];
      ren_assert(not texture.name.empty());
      if (texture.name.starts_with("rg#")) {
        create_textures.push_back(texture.child);
      } else {
        write_textures.push_back(id);
      }
    }

    if (not create_textures.empty()) {
      fmt::println(stderr, "    Creates textures:");
      for (RgTextureId texture : create_textures) {
        fmt::println(stderr, "      - {}", textures[texture].name);
      }
    }
    if (not pass.read_textures.empty()) {
      fmt::println(stderr, "    Reads textures:");
      for (RgTextureUseId use : pass.read_textures) {
        fmt::println(stderr, "      - {}",
                     textures[texture_uses[use].texture].name);
      }
    }
    if (not write_textures.empty()) {
      fmt::println(stderr, "    Writes textures:");
      for (RgTextureId src_id : write_textures) {
        const RgTexture &src = textures[src_id];
        const RgTexture &dst = textures[src.child];
        fmt::println(stderr, "      - {} -> {}", src.name, dst.name);
      }
    }

    const auto &semaphores = m_rgp->m_semaphores;
    const auto &semaphore_signals = m_data->m_semaphore_signals;

    if (not pass.wait_semaphores.empty()) {
      fmt::println(stderr, "    Waits for semaphores:");
      for (RgSemaphoreSignalId signal : pass.wait_semaphores) {
        RgSemaphoreId semaphore = semaphore_signals[signal].semaphore;
        fmt::println(stderr, "      - {}", semaphores[semaphore].name);
      }
    }

    if (not pass.signal_semaphores.empty()) {
      fmt::println(stderr, "    Signals semaphores:");
      for (RgSemaphoreSignalId signal : pass.signal_semaphores) {
        RgSemaphoreId semaphore = semaphore_signals[signal].semaphore;
        fmt::println(stderr, "      - {}", semaphores[semaphore].name);
      }
    }

    fmt::println(stderr, "");
  }
#endif
}

void RgBuilder::alloc_textures() {
  bool need_alloc = false;
  auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
    const RgTextureUse &use = m_data->m_texture_uses[use_id];
    const RgTexture &texture = m_rgp->m_textures[use.texture];
    RgPhysicalTextureId physical_texture_id = texture.parent;
    RgPhysicalTexture &physical_texture =
        m_rgp->m_physical_textures[physical_texture_id];
    VkImageUsageFlags usage = get_texture_usage_flags(use.state.access_mask);
    bool needs_usage_update =
        (physical_texture.usage | usage) != physical_texture.usage;
    physical_texture.usage |= usage;
    if (!physical_texture.handle or needs_usage_update) {
      ren_assert(not m_rgp->m_external_textures[physical_texture_id]);
      need_alloc = true;
    }
    if (physical_texture.handle and needs_usage_update) {
      int b = 0;
    }
  };
  for (const auto &[_, pass] : m_data->m_passes) {
    std::ranges::for_each(pass.read_textures, update_texture_usage_flags);
    std::ranges::for_each(pass.write_textures, update_texture_usage_flags);
  }

  if (not need_alloc) {
    return;
  }

  usize num_passes = m_data->m_schedule.size();

  for (auto &&[base_physical_texture_id, init_info] :
       m_rgp->m_texture_init_info) {
    for (auto i : range<usize>(1, RG_MAX_TEMPORAL_LAYERS)) {
      RgPhysicalTextureId physical_texture_id(base_physical_texture_id + i);
      const RgPhysicalTexture &physical_texture =
          m_rgp->m_physical_textures[physical_texture_id];
      if (!physical_texture.id) {
        break;
      }
      auto pass = create_pass({
#if REN_RG_DEBUG
          .name = fmt::format("rg#init-{}", physical_texture.name),
#endif
      });
      RgTextureToken texture =
          write_texture(pass.m_pass, physical_texture.id,
                        physical_texture.init_id, init_info.usage);
      pass.set_callback([texture, cb = &init_info.cb](Renderer &renderer,
                                                      const RgRuntime &rg,
                                                      CommandRecorder &cmd) {
        (*cb)(rg.get_texture(texture), renderer, cmd);
      });
    }
  }

  m_rgp->m_texture_arena.clear();
  for (auto i : range(m_rgp->m_physical_textures.size())) {
    RgPhysicalTexture &physical_texture = m_rgp->m_physical_textures[i];
    // Skip unused temporal textures and external textures.
    if (!physical_texture.usage or m_rgp->m_external_textures[i]) {
      continue;
    }
    physical_texture.handle = m_rgp->m_texture_arena.create_texture({
#if REN_RG_DEBUG
        .name = physical_texture.name,
#endif
        .type = physical_texture.type,
        .format = physical_texture.format,
        .usage = physical_texture.usage,
        .width = physical_texture.size.x,
        .height = physical_texture.size.y,
        .depth = physical_texture.size.z,
        .num_mip_levels = physical_texture.num_mip_levels,
        .num_array_layers = physical_texture.num_array_layers,
    });
    physical_texture.state = {};
  }

  // Schedule init passes before all other passes.
  if (m_data->m_schedule.size() != num_passes) {
    std::ranges::rotate(m_data->m_schedule,
                        m_data->m_schedule.begin() + num_passes);
  }
}

void RgBuilder::alloc_buffers(DeviceBumpAllocator &device_allocator,
                              UploadBumpAllocator &upload_allocator) {
  for (auto i : range(m_data->m_physical_buffers.size())) {
    RgPhysicalBufferId id(i);
    RgPhysicalBuffer &physical_buffer = m_data->m_physical_buffers[id];
    if (physical_buffer.view.buffer) {
      continue;
    }
    switch (BufferHeap heap = physical_buffer.heap) {
    default:
      unreachable("Unsupported RenderGraph buffer heap: {}", int(heap));
    case BufferHeap::Static: {
      physical_buffer.view =
          device_allocator.allocate(physical_buffer.size).slice;
    } break;
    case BufferHeap::Dynamic:
    case BufferHeap::Staging: {
      physical_buffer.view =
          upload_allocator.allocate(physical_buffer.size).slice;
    } break;
    }
  }
}

void RgBuilder::init_runtime_passes() {
  auto &rt_passes = m_rt_data->m_passes;
  rt_passes.clear();
  rt_passes.reserve(m_data->m_schedule.size());

  for (RgPassId pass_id : m_data->m_schedule) {
    RgPass &pass = m_data->m_passes[pass_id];
    RgRtPass &rt_pass = m_rt_data->m_passes.emplace_back();
    rt_pass = {.pass = pass_id};
    pass.ext.visit(OverloadSet{
        [&](Monostate) {
          unreachable("Callback for pass {} has not been "
                      "set!",
#if REN_RG_DEBUG
                      m_rt_data->m_pass_names[pass_id]
#else
                      u32(pass_id)
#endif
          );
        },
        [&](RgHostPass &host_pass) {
          rt_pass.ext = RgRtHostPass{.cb = std::move(host_pass.cb)};
        },
        [&](RgGraphicsPass &graphics_pass) {
          rt_pass.ext = RgRtGraphicsPass{
              .base_color_attachment =
                  u32(m_rt_data->m_color_attachments.size()),
              .num_color_attachments =
                  u32(graphics_pass.color_attachments.size()),
              .depth_attachment = graphics_pass.depth_stencil_attachment.map(
                  [&](const RgDepthStencilAttachment &) -> u32 {
                    return m_rt_data->m_depth_stencil_attachments.size();
                  }),
              .cb = std::move(graphics_pass.cb),
          };
          m_rt_data->m_color_attachments.append(
              graphics_pass.color_attachments);
          graphics_pass.depth_stencil_attachment.map(
              [&](const RgDepthStencilAttachment &att) {
                m_rt_data->m_depth_stencil_attachments.push_back(att);
              });
        },
        [&](RgComputePass &compute_pass) {
          rt_pass.ext = RgRtComputePass{.cb = std::move(compute_pass.cb)};
        },
        [&](RgGenericPass &pass) {
          rt_pass.ext = RgRtGenericPass{.cb = std::move(pass.cb)};
        },
    });
  }
}

void RgBuilder::init_runtime_buffers() {
  auto &rt_buffers = m_rt_data->m_buffers;
  const auto &buffer_uses = m_data->m_buffer_uses;
  rt_buffers.resize(buffer_uses.size());
  for (auto i : range(buffer_uses.size())) {
    RgPhysicalBufferId physical_buffer_id =
        m_data->m_buffers[buffer_uses[i].buffer].parent;
    rt_buffers[i] = m_data->m_physical_buffers[physical_buffer_id].view.slice(
        buffer_uses[i].offset);
  }
}

void RgBuilder::init_runtime_textures() {
  auto &rt_textures = m_rt_data->m_textures;
  const auto &texture_uses = m_data->m_texture_uses;
  const auto &physical_textures = m_rgp->m_physical_textures;

  rt_textures.resize(texture_uses.size());
  usize num_storage_texture_descriptors = 0;
  for (auto i : range(texture_uses.size())) {
    const RgTextureUse &use = texture_uses[i];
    RgPhysicalTextureId physical_texture_id =
        m_rgp->m_textures[use.texture].parent;
    const RgPhysicalTexture &physical_texture =
        physical_textures[physical_texture_id];
    rt_textures[i] = physical_texture.handle;
    num_storage_texture_descriptors +=
        use.state.access_mask & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
            ? physical_texture.num_mip_levels
            : 0;
  }

  auto &rt_storage_texture_descriptors =
      m_rt_data->m_storage_texture_descriptors;
  rt_storage_texture_descriptors.resize(num_storage_texture_descriptors);
  auto &rt_texture_descriptors = m_rt_data->m_texture_descriptors;
  rt_texture_descriptors.resize(texture_uses.size());
  num_storage_texture_descriptors = 0;
  for (auto i : range(texture_uses.size())) {
    const RgTextureUse &use = texture_uses[i];
    RgPhysicalTextureId physical_texture_id =
        m_rgp->m_textures[use.texture].parent;
    const RgPhysicalTexture &physical_texture =
        physical_textures[physical_texture_id];
    RgTextureDescriptors &descriptors = rt_texture_descriptors[i];

    TextureView view = m_renderer->get_texture_view(physical_texture.handle);
    if (use.state.access_mask & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
      if (use.sampler) {
        descriptors.combined = m_descriptor_allocator->allocate_sampled_texture(
            *m_renderer, view, use.sampler);
      } else {
        descriptors.sampled =
            m_descriptor_allocator->allocate_texture(*m_renderer, view);
      }
    } else if (use.state.access_mask & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
      view.num_mip_levels = 1;
      descriptors.num_mips = physical_texture.num_mip_levels;
      descriptors.storage =
          &rt_storage_texture_descriptors[num_storage_texture_descriptors];
      for (i32 mip = 0; mip < descriptors.num_mips; ++mip) {
        view.first_mip_level = mip;
        descriptors.storage[mip] =
            m_descriptor_allocator->allocate_storage_texture(*m_renderer, view);
      }

      num_storage_texture_descriptors += physical_texture.num_mip_levels;
    }
  }
}

void RgBuilder::place_barriers_and_semaphores() {
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

  Vector<BufferState> buffer_after_write_hazard_src_states(
      m_data->m_physical_buffers.size());
  Vector<VkPipelineStageFlags2> buffer_after_read_hazard_src_states(
      m_data->m_physical_buffers.size());

  for (auto i : range(m_data->m_physical_buffers.size())) {
    RgPhysicalBuffer &physical_buffer = m_data->m_physical_buffers[i];
    const BufferState &state = physical_buffer.state;
    if (state.access_mask & WRITE_ONLY_ACCESS_MASK) {
      buffer_after_write_hazard_src_states[i] = {
          .stage_mask = state.stage_mask,
          .access_mask = state.access_mask & WRITE_ONLY_ACCESS_MASK,
      };
    } else {
      buffer_after_read_hazard_src_states[i] = state.access_mask;
    }
  }

  Vector<MemoryState> texture_after_write_hazard_src_states(
      m_rgp->m_physical_textures.size());
  Vector<VkPipelineStageFlags2> texture_after_read_hazard_src_states(
      m_rgp->m_physical_textures.size());
  Vector<VkImageLayout> texture_layouts(m_rgp->m_physical_textures.size());

  for (auto i : range(m_rgp->m_physical_textures.size())) {
    RgPhysicalTexture &physical_texture = m_rgp->m_physical_textures[i];
    if (!physical_texture.handle) {
      continue;
    }
    const TextureState &state = physical_texture.state;
    if (state.access_mask & WRITE_ONLY_ACCESS_MASK) {
      texture_after_write_hazard_src_states[i] = {
          .stage_mask = state.stage_mask,
          .access_mask = state.access_mask & WRITE_ONLY_ACCESS_MASK,
      };
    } else {
      texture_after_read_hazard_src_states[i] = state.access_mask;
    }
    texture_layouts[i] =
        m_rgp->m_persistent_textures[i] or m_rgp->m_external_textures[i]
            ? state.layout
            : VK_IMAGE_LAYOUT_UNDEFINED;
  }

  auto &m_memory_barriers = m_rt_data->m_memory_barriers;
  auto &m_texture_barriers = m_rt_data->m_texture_barriers;
  auto &m_semaphore_submit_info = m_rt_data->m_semaphore_submit_info;
  m_memory_barriers.clear();
  m_texture_barriers.clear();
  m_semaphore_submit_info.clear();
  const auto &m_buffer_uses = m_data->m_buffer_uses;
  const auto &m_buffers = m_data->m_buffers;
  const auto &m_texture_uses = m_data->m_texture_uses;
  const auto &m_textures = m_rgp->m_textures;

  m_memory_barriers.clear();
  m_texture_barriers.clear();

  for (RgRtPass &rt_pass : m_rt_data->m_passes) {
    const RgPass &pass = m_data->m_passes[rt_pass.pass];

    usize old_memory_barrier_count = m_memory_barriers.size();
    usize old_texture_barrier_count = m_texture_barriers.size();
    usize old_semaphore_count = m_semaphore_submit_info.size();

    // TODO: merge separate barriers together if it
    // doesn't change how synchronization happens
    auto maybe_place_barrier_for_buffer = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      RgPhysicalBufferId physical_buffer = m_buffers[use.buffer].parent;

      VkPipelineStageFlags2 dst_stage_mask = use.usage.stage_mask;
      VkAccessFlags2 dst_access_mask = use.usage.access_mask;

      // Don't need a barrier for host-only
      // accesses
      bool is_host_only_access = dst_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_host_only_access) {
        ren_assert(dst_access_mask == VK_ACCESS_2_NONE);
        return;
      }

      VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
      VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

      if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
        BufferState &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        // Reset the source stage mask that the
        // next WAR hazard will use
        src_stage_mask =
            std::exchange(buffer_after_read_hazard_src_states[physical_buffer],
                          VK_PIPELINE_STAGE_2_NONE);
        // If this is a WAR hazard, need to
        // wait for all previous reads to
        // finish. The previous write's memory
        // has already been made available by
        // previous RAW barriers, so it only
        // needs to be made visible
        // FIXME: According to the Vulkan spec,
        // WAR hazards require only an
        // execution barrier
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // No reads were performed between
          // this write and the previous one so
          // this is a WAW hazard. Need to wait
          // for the previous write to finish
          // and make its memory available and
          // visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access
        // masks that further RAW and WAW
        // hazards will use
        after_write_state.stage_mask = dst_stage_mask;
        // Read accesses are redundant for
        // source access mask
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;
      } else {
        // This is a RAW hazard. Need to wait
        // for the previous write to finish and
        // make it's memory available and
        // visible
        // TODO/FIXME: all RAW barriers should
        // be merged, since if they are issued
        // separately they might cause the
        // cache to be flushed multiple times
        const BufferState &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        src_stage_mask = after_write_state.stage_mask;
        src_access_mask = after_write_state.access_mask;
        // Update the source stage mask that
        // the next WAR hazard will use
        buffer_after_read_hazard_src_states[physical_buffer] |= dst_stage_mask;
      }

      // First barrier isn't required and can
      // be skipped
      bool is_first_access = src_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_first_access) {
        ren_assert(src_access_mask == VK_PIPELINE_STAGE_2_NONE);
        return;
      }

      m_memory_barriers.push_back({
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = src_stage_mask,
          .srcAccessMask = src_access_mask,
          .dstStageMask = dst_stage_mask,
          .dstAccessMask = dst_access_mask,
      });
    };

    std::ranges::for_each(pass.read_buffers, maybe_place_barrier_for_buffer);
    std::ranges::for_each(pass.write_buffers, maybe_place_barrier_for_buffer);

    auto maybe_place_barrier_for_texture = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_texture_uses[use_id];
      RgPhysicalTextureId physical_texture = m_textures[use.texture].parent;

      VkPipelineStageFlags2 dst_stage_mask = use.state.stage_mask;
      VkAccessFlags2 dst_access_mask = use.state.access_mask;
      VkImageLayout dst_layout = use.state.layout;
      ren_assert(dst_layout);
      if (dst_layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        ren_assert(dst_stage_mask);
        ren_assert(dst_access_mask);
      }

      VkImageLayout &src_layout = texture_layouts[physical_texture];

      if (dst_layout == src_layout) {
        // Only a memory barrier is required if
        // layout doesn't change NOTE: this code is
        // copy-pasted from above

        VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

        if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
          MemoryState &after_write_state =
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
          const MemoryState &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
          texture_after_read_hazard_src_states[physical_texture] |=
              dst_stage_mask;
        }

        ren_assert(src_stage_mask != VK_PIPELINE_STAGE_2_NONE);

        m_memory_barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
        });
      } else {
        // Need an image barrier to change layout.
        // Layout transitions are read-write
        // operations, so only to take care of WAR
        // and WAW hazards in this case

        MemoryState &after_write_state =
            texture_after_write_hazard_src_states[physical_texture];
        // If this is a WAR hazard, must wait for
        // all previous reads to finish and make
        // the layout transition's memory
        // available. Also reset the source stage
        // mask that the next WAR barrier will use
        VkPipelineStageFlags2 src_stage_mask = std::exchange(
            texture_after_read_hazard_src_states[physical_texture],
            VK_PIPELINE_STAGE_2_NONE);
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // If there were no reads between this
          // write and the previous one, need to
          // wait for the previous write to finish
          // and make it's memory available and the
          // layout transition's memory visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks
        // that further RAW and WAW barriers will
        // use
        after_write_state.stage_mask = dst_stage_mask;
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;

        const Texture &texture = m_renderer->get_texture(
            m_rgp->m_physical_textures[physical_texture].handle);

        m_texture_barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
            .oldLayout = src_layout,
            .newLayout = dst_layout,
            .image = texture.image,
            .subresourceRange =
                {
                    .aspectMask = getVkImageAspectFlags(texture.format),
                    .levelCount = texture.num_mip_levels,
                    .layerCount = texture.num_array_layers,
                },
        });

        // Update current layout
        src_layout = dst_layout;
      }
    };

    std::ranges::for_each(pass.read_textures, maybe_place_barrier_for_texture);
    std::ranges::for_each(pass.write_textures, maybe_place_barrier_for_texture);

    auto place_semaphore = [&](RgSemaphoreSignalId id) {
      const RgSemaphoreSignal &signal = m_data->m_semaphore_signals[id];
      m_semaphore_submit_info.push_back({
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore =
              m_renderer
                  ->get_semaphore(m_rgp->m_semaphores[signal.semaphore].handle)
                  .handle,
          .value = signal.value,
          .stageMask = signal.stage_mask,
      });
    };

    std::ranges::for_each(pass.wait_semaphores, place_semaphore);
    std::ranges::for_each(pass.signal_semaphores, place_semaphore);

    usize new_memory_barrier_count = m_memory_barriers.size();
    usize new_texture_barrier_count = m_texture_barriers.size();

    rt_pass.base_memory_barrier = old_memory_barrier_count;
    rt_pass.num_memory_barriers =
        new_memory_barrier_count - old_memory_barrier_count;
    rt_pass.base_texture_barrier = old_texture_barrier_count;
    rt_pass.num_texture_barriers =
        new_texture_barrier_count - old_texture_barrier_count;
    rt_pass.base_wait_semaphore = old_semaphore_count;
    rt_pass.num_wait_semaphores = pass.wait_semaphores.size();
    rt_pass.base_signal_semaphore =
        rt_pass.base_wait_semaphore + rt_pass.num_wait_semaphores;
    rt_pass.num_signal_semaphores = pass.signal_semaphores.size();
  }

  for (auto i : range(m_data->m_physical_buffers.size())) {
    RgPhysicalBuffer &physical_buffer = m_data->m_physical_buffers[i];
    VkPipelineStageFlags2 stage_mask = buffer_after_read_hazard_src_states[i];
    VkAccessFlags2 access_mask = 0;
    if (!stage_mask) {
      const BufferState &state = buffer_after_write_hazard_src_states[i];
      stage_mask = state.stage_mask;
      access_mask = state.access_mask;
    }
    physical_buffer.state = {
        .stage_mask = stage_mask,
        .access_mask = access_mask,
    };
  }

  for (auto i : range(m_rgp->m_physical_textures.size())) {
    RgPhysicalTexture &physical_texture = m_rgp->m_physical_textures[i];
    VkPipelineStageFlags2 stage_mask = texture_after_read_hazard_src_states[i];
    VkAccessFlags2 access_mask = 0;
    if (!stage_mask) {
      const MemoryState &state = texture_after_write_hazard_src_states[i];
      stage_mask = state.stage_mask;
      access_mask = state.access_mask;
    }
    physical_texture.state = {
        .stage_mask = stage_mask,
        .access_mask = access_mask,
        .layout = texture_layouts[i],
    };
  }
}

auto RgBuilder::build(DeviceBumpAllocator &device_allocator,
                      UploadBumpAllocator &upload_allocator) -> RenderGraph {
  ren_prof_zone("RgBuilder::build");

  m_rgp->rotate_textures();

  alloc_textures();
  alloc_buffers(device_allocator, upload_allocator);

#if 0
  dump_pass_schedule();
#endif

  init_runtime_passes();
  init_runtime_buffers();
  init_runtime_textures();

  place_barriers_and_semaphores();

  RenderGraph rg;
  rg.m_renderer = m_renderer;
  rg.m_rgp = m_rgp;
  rg.m_data = m_rt_data;
  rg.m_upload_allocator = &upload_allocator;
  rg.m_texture_set = m_descriptor_allocator->get_set();
  rg.m_semaphores = &m_rgp->m_semaphores;

  return rg;
}

auto RgBuilder::get_final_buffer_state(RgUntypedBufferId buffer) const
    -> BufferState {
  ren_assert(buffer);
  RgPhysicalBufferId physical_buffer = m_data->m_buffers[buffer].parent;
  return m_data->m_physical_buffers[physical_buffer].state;
}

RgPassBuilder::RgPassBuilder(RgPassId pass, RgBuilder &builder) {
  m_pass = pass;
  m_builder = &builder;
}

auto RgPassBuilder::read_buffer(RgUntypedBufferId buffer,
                                const BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  return m_builder->read_buffer(m_pass, buffer, usage, offset);
}

auto RgPassBuilder::write_buffer(RgDebugName name, RgUntypedBufferId buffer,
                                 const BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  return m_builder->write_buffer(m_pass, std::move(name), buffer, usage);
}

auto RgPassBuilder::read_texture(RgTextureId texture, const TextureState &usage,
                                 u32 temporal_layer) -> RgTextureToken {
  return read_texture(texture, usage, NullHandle, temporal_layer);
}

auto RgPassBuilder::read_texture(RgTextureId texture, const TextureState &usage,
                                 Handle<Sampler> sampler, u32 temporal_layer)
    -> RgTextureToken {
  return m_builder->read_texture(m_pass, texture, usage, sampler,
                                 temporal_layer);
}

auto RgPassBuilder::write_texture(RgDebugName name, RgTextureId texture,
                                  const TextureState &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  return m_builder->write_texture(m_pass, std::move(name), texture, usage);
}

auto RgPassBuilder::write_texture(RgDebugName name, RgTextureId texture,
                                  RgTextureId *new_texture,
                                  const TextureState &usage) -> RgTextureToken {
  RgTextureToken token;
  if (new_texture) {
    std::tie(*new_texture, token) =
        write_texture(std::move(name), texture, usage);
  } else {
    std::tie(std::ignore, token) =
        write_texture(std::move(name), texture, usage);
  }
  return token;
}

auto RgPassBuilder::write_texture(RgDebugName name,
                                  NotNull<RgTextureId *> texture,
                                  const TextureState &usage) -> RgTextureToken {
  RgTextureToken token;
  std::tie(*texture, token) = write_texture(std::move(name), *texture, usage);
  return token;
}

void RgPassBuilder::add_color_attachment(u32 index, RgTextureToken texture,
                                         const ColorAttachmentOperations &ops) {
  auto &color_attachments = m_builder->m_data->m_passes[m_pass]
                                .ext.get_or_emplace<RgGraphicsPass>()
                                .color_attachments;
  if (color_attachments.size() <= index) {
    color_attachments.resize(index + 1);
  }
  color_attachments[index] = {
      .texture = texture,
      .ops = ops,
  };
}

void RgPassBuilder::add_depth_attachment(RgTextureToken texture,
                                         const DepthAttachmentOperations &ops) {
  m_builder->m_data->m_passes[m_pass]
      .ext.get_or_emplace<RgGraphicsPass>()
      .depth_stencil_attachment = RgDepthStencilAttachment{
      .texture = texture,
      .depth_ops = ops,
  };
}

auto RgPassBuilder::write_color_attachment(RgDebugName name,
                                           RgTextureId texture,
                                           const ColorAttachmentOperations &ops,
                                           u32 index)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, COLOR_ATTACHMENT);
  add_color_attachment(index, token, ops);
  return {new_texture, token};
}

auto RgPassBuilder::read_depth_attachment(RgTextureId texture,
                                          u32 temporal_layer)
    -> RgTextureToken {
  RgTextureToken token =
      read_texture(texture, READ_DEPTH_ATTACHMENT, NullHandle, temporal_layer);
  add_depth_attachment(token, {
                                  .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                  .store = VK_ATTACHMENT_STORE_OP_NONE,
                              });
  return token;
}

auto RgPassBuilder::write_depth_attachment(RgDebugName name,
                                           RgTextureId texture,
                                           const DepthAttachmentOperations &ops)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, READ_WRITE_DEPTH_ATTACHMENT);
  add_depth_attachment(token, ops);
  return {new_texture, token};
}

void RgPassBuilder::wait_semaphore(RgSemaphoreId semaphore,
                                   VkPipelineStageFlags2 stages, u64 value) {
  m_builder->wait_semaphore(m_pass, semaphore, stages, value);
}

void RgPassBuilder::signal_semaphore(RgSemaphoreId semaphore,
                                     VkPipelineStageFlags2 stages, u64 value) {
  m_builder->signal_semaphore(m_pass, semaphore, stages, value);
}

void RenderGraph::execute(CommandAllocator &cmd_alloc) {
  ren_prof_zone("RenderGraph::execute");

  RgRuntime rt;
  rt.m_rg = this;

  auto &batch_cmd_buffers = m_data->m_batch_cmd_buffers;
  batch_cmd_buffers.clear();
  Span<const VkSemaphoreSubmitInfo> batch_wait_semaphores;
  Span<const VkSemaphoreSubmitInfo> batch_signal_semaphores;

  auto submit_batch = [&] {
    if (batch_cmd_buffers.empty() and batch_wait_semaphores.empty() and
        batch_signal_semaphores.empty()) {
      return;
    }
    m_renderer->graphicsQueueSubmit(batch_cmd_buffers, batch_wait_semaphores,
                                    batch_signal_semaphores);
    batch_cmd_buffers.clear();
    batch_wait_semaphores = {};
    batch_signal_semaphores = {};
  };

  for (const RgRtPass &pass : m_data->m_passes) {
    ren_prof_zone("RenderGraph::execute_pass");
#if REN_RG_DEBUG
    ren_prof_zone_text(m_data->m_pass_names[pass.pass]);
#endif
    if (pass.num_wait_semaphores > 0) {
      submit_batch();
      batch_wait_semaphores =
          Span(m_data->m_semaphore_submit_info)
              .subspan(pass.base_wait_semaphore, pass.num_wait_semaphores);
    }

    VkCommandBuffer cmd_buffer = nullptr;
    {
      Optional<CommandRecorder> cmd_recorder;
      Optional<DebugRegion> debug_region;
      auto get_command_recorder = [&]() -> CommandRecorder & {
        if (!cmd_recorder) {
          cmd_buffer = cmd_alloc.allocate();
          cmd_recorder.emplace(*m_renderer, cmd_buffer);
#if REN_RG_DEBUG
          debug_region.emplace(cmd_recorder->debug_region(
              m_data->m_pass_names[pass.pass].c_str()));
#endif
        }
        return *cmd_recorder;
      };

      if (pass.num_memory_barriers > 0 or pass.num_texture_barriers > 0) {
        auto memory_barriers =
            Span(m_data->m_memory_barriers)
                .subspan(pass.base_memory_barrier, pass.num_memory_barriers);
        auto texture_barriers =
            Span(m_data->m_texture_barriers)
                .subspan(pass.base_texture_barrier, pass.num_texture_barriers);
        get_command_recorder().pipeline_barrier(memory_barriers,
                                                texture_barriers);
      }

      pass.ext.visit(OverloadSet{
          [&](const RgRtHostPass &host_pass) {
            if (host_pass.cb) {
              host_pass.cb(*m_renderer, rt);
            }
          },
          [&](const RgRtGraphicsPass &graphics_pass) {
            if (!graphics_pass.cb) {
              return;
            }

            glm::uvec2 viewport = {-1, -1};

            auto color_attachments =
                Span(m_data->m_color_attachments)
                    .subspan(graphics_pass.base_color_attachment,
                             graphics_pass.num_color_attachments) |
                map([&](const Optional<RgColorAttachment> &att)
                        -> Optional<ColorAttachment> {
                  return att.map(
                      [&](const RgColorAttachment &att) -> ColorAttachment {
                        TextureView view = m_renderer->get_texture_view(
                            rt.get_texture(att.texture));
                        viewport = m_renderer->get_texture_view_size(view);
                        return {
                            .texture = view,
                            .ops = att.ops,
                        };
                      });
                }) |
                std::ranges::to<StaticVector<Optional<ColorAttachment>,
                                             MAX_COLOR_ATTACHMENTS>>();

            auto depth_stencil_attachment = graphics_pass.depth_attachment.map(
                [&](u32 index) -> DepthStencilAttachment {
                  const RgDepthStencilAttachment &att =
                      m_data->m_depth_stencil_attachments[index];
                  TextureView view =
                      m_renderer->get_texture_view(rt.get_texture(att.texture));
                  viewport = m_renderer->get_texture_view_size(view);
                  return {
                      .texture = view,
                      .depth_ops = att.depth_ops,
                      .stencil_ops = att.stencil_ops,
                  };
                });

            RenderPass render_pass = get_command_recorder().render_pass({
                .color_attachments = color_attachments,
                .depth_stencil_attachment = depth_stencil_attachment,
            });
            render_pass.set_viewports({{
                .width = float(viewport.x),
                .height = float(viewport.y),
                .maxDepth = 1.0f,
            }});
            render_pass.set_scissor_rects(
                {{.extent = {viewport.x, viewport.y}}});

            graphics_pass.cb(*m_renderer, rt, render_pass);
          },
          [&](const RgRtComputePass &compute_pass) {
            if (compute_pass.cb) {
              ComputePass comp = get_command_recorder().compute_pass();
              compute_pass.cb(*m_renderer, rt, comp);
            }
          },
          [&](const RgRtGenericPass &pass) {
            if (pass.cb) {
              pass.cb(*m_renderer, rt, get_command_recorder());
            }
          },
      });
    }

    if (cmd_buffer) {
      batch_cmd_buffers.push_back({
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = cmd_buffer,
      });
    }

    if (pass.num_signal_semaphores > 0) {
      batch_signal_semaphores =
          Span(m_data->m_semaphore_submit_info)
              .subspan(pass.base_signal_semaphore, pass.num_signal_semaphores);
      submit_batch();
    }
  }

  submit_batch();

  for (RgTextureId texture : m_rgp->m_frame_textures) {
    m_rgp->m_textures.erase(texture);
  }
  m_rgp->m_frame_textures.clear();
}

auto RgRuntime::get_buffer(RgUntypedBufferToken buffer) const
    -> const BufferView & {
  ren_assert(buffer);
  return m_rg->m_data->m_buffers[buffer];
}

auto RgRuntime::get_texture(RgTextureToken texture) const -> Handle<Texture> {
  ren_assert(texture);
  return m_rg->m_data->m_textures[texture];
}

auto RgRuntime::get_texture_descriptor(RgTextureToken texture) const
    -> glsl::Texture {
  ren_assert(texture);
  glsl::Texture descriptor =
      m_rg->m_data->m_texture_descriptors[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_texture_descriptor(RgTextureToken texture) const
    -> glsl::Texture {
  if (!texture) {
    return {};
  }
  glsl::Texture descriptor =
      m_rg->m_data->m_texture_descriptors[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_sampled_texture_descriptor(RgTextureToken texture) const
    -> glsl::SampledTexture {
  ren_assert(texture);
  glsl::SampledTexture descriptor =
      m_rg->m_data->m_texture_descriptors[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_sampled_texture_descriptor(RgTextureToken texture) const
    -> glsl::SampledTexture {
  if (!texture) {
    return {};
  }
  glsl::SampledTexture descriptor =
      m_rg->m_data->m_texture_descriptors[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureToken texture,
                                               u32 mip) const
    -> glsl::StorageTexture {
  ren_assert(texture);
  ren_assert(m_rg->m_data->m_texture_descriptors[texture].storage);
  const RgTextureDescriptors &descriptors =
      m_rg->m_data->m_texture_descriptors[texture];
  ren_assert(mip < descriptors.num_mips);
  glsl::StorageTexture descriptor = descriptors.storage[mip];
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_storage_texture_descriptor(RgTextureToken texture,
                                                   u32 mip) const
    -> glsl::StorageTexture {
  if (!texture) {
    return {};
  }
  ren_assert(m_rg->m_data->m_texture_descriptors[texture].storage);
  const RgTextureDescriptors &descriptors =
      m_rg->m_data->m_texture_descriptors[texture];
  if (mip >= descriptors.num_mips) {
    return {};
  }
  glsl::StorageTexture descriptor = descriptors.storage[mip];
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_texture_set() const -> VkDescriptorSet {
  return m_rg->m_texture_set;
}

auto RgRuntime::get_allocator() const -> UploadBumpAllocator & {
  return *m_rg->m_upload_allocator;
}

auto RgRuntime::get_semaphore(RgSemaphoreId semaphore) const
    -> Handle<Semaphore> {
  return m_rg->m_semaphores->get(semaphore).handle;
}

} // namespace ren
