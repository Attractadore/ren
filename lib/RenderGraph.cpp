#include "RenderGraph.hpp"
#include "CommandRecorder.hpp"
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

  rhi::ImageUsageFlags usage = {};
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
        usage = rhi::ImageUsage::TransferSrc | rhi::ImageUsage::TransferDst;
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

    m_physical_textures[id] = RgPhysicalTexture{
#if REN_RG_DEBUG
        .name = std::move(handle_name),
#endif
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
    rhi::ImageUsageFlags usage = physical_texture.usage;
    Handle<Texture> handle = physical_texture.handle;
    rhi::ImageState state = physical_texture.state;
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

auto get_texture_usage_flags(rhi::AccessMask accesses) -> rhi::ImageUsageFlags {
  rhi::ImageUsageFlags flags = {};
  if (accesses.is_set(rhi::Access::ShaderImageRead)) {
    flags |= rhi::ImageUsage::ShaderResource;
  }
  if (accesses.is_set(rhi::Access::UnorderedAccess)) {
    flags |= rhi::ImageUsage::UnorderedAccess;
  }
  if (accesses.is_set(rhi::Access::RenderTarget)) {
    flags |= rhi::ImageUsage::RenderTarget;
  }
  if (accesses.is_any_set(rhi::Access::DepthStencilRead |
                          rhi::Access::DepthStencilWrite)) {
    flags |= rhi::ImageUsage::DepthStencilTarget;
  }
  if (accesses.is_set(rhi::Access::TransferRead)) {
    flags |= rhi::ImageUsage::TransferSrc;
  }
  if (accesses.is_set(rhi::Access::TransferWrite)) {
    flags |= rhi::ImageUsage::TransferDst;
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
  bd.m_semaphore_states.clear();

  auto &gd = *m_rt_data;
#if REN_RG_DEBUG
  gd.m_pass_names.clear();
#endif
  gd.m_render_targets.clear();
  gd.m_depth_stencil_targets.clear();
}

auto RgBuilder::create_pass(RgPassCreateInfo &&create_info) -> RgPassBuilder {
  RgPassId pass = m_data->m_passes.emplace(RgPass{.queue = create_info.queue});
#if REN_RG_DEBUG
  m_rt_data->m_pass_names.insert(pass, std::move(create_info.name));
#endif
  m_data->m_schedule.push_back(pass);
  return RgPassBuilder(pass, *this);
}

auto RgBuilder::add_buffer_use(RgUntypedBufferId buffer,
                               const rhi::BufferState &usage, u32 offset)
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

auto RgBuilder::create_buffer(RgDebugName name, rhi::MemoryHeap heap,
                              usize size) -> RgUntypedBufferId {
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
                            const rhi::BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  ren_assert(buffer);
  RgBufferUseId use = add_buffer_use(buffer, usage, offset);
  m_data->m_passes[pass].read_buffers.push_back(use);
  return RgUntypedBufferToken(use);
}

auto RgBuilder::write_buffer(RgPassId pass, RgDebugName name,
                             RgUntypedBufferId src,
                             const rhi::BufferState &usage)
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
      pass.write_texture(std::move(name), texture, rhi::TRANSFER_DST_IMAGE);
  pass.set_callback(
      [token, value](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
        cmd.clear_texture(rg.get_texture(token), value);
      });
}

auto RgBuilder::add_texture_use(RgTextureId texture,
                                const rhi::ImageState &usage,
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
                             const rhi::ImageState &usage,
                             Handle<Sampler> sampler, u32 temporal_layer)
    -> RgTextureToken {
  ren_assert(texture);
  if (sampler) {
    ren_assert_msg(usage.access_mask.is_set(rhi::Access::ShaderImageRead),
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
                              const rhi::ImageState &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  ren_assert(src);
  ren_assert(m_rgp->m_textures[src].def != pass);
  RgTextureUseId use = add_texture_use(src, usage);
  m_data->m_passes[pass].write_textures.push_back(use);
  RgTextureId dst = create_virtual_texture(pass, std::move(name), src);
  return {dst, RgTextureToken(use)};
}

auto RgBuilder::write_texture(RgPassId pass, RgTextureId dst_id,
                              RgTextureId src_id, const rhi::ImageState &usage)
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
                                    const rhi::BufferState &state) {
  RgPhysicalBufferId physical_buffer_id = m_data->m_buffers[id].parent;
  RgPhysicalBuffer &physical_buffer =
      m_data->m_physical_buffers[physical_buffer_id];
  ren_assert(!physical_buffer.view.buffer);
  physical_buffer.view = view;
  physical_buffer.state = state;
}

void RgBuilder::set_external_texture(RgTextureId id, Handle<Texture> handle,
                                     const rhi::ImageState &state) {
  RgPhysicalTextureId physical_texture_id = m_rgp->m_textures[id].parent;
  ren_assert(m_rgp->m_external_textures[physical_texture_id]);
  RgPhysicalTexture &physical_texture =
      m_rgp->m_physical_textures[physical_texture_id];
  physical_texture.handle = handle;
  physical_texture.state = state;
}

auto RgBuilder::add_semaphore_state(RgSemaphoreId semaphore, u64 value)
    -> RgSemaphoreStateId {
  RgSemaphoreStateId id(m_data->m_semaphore_states.size());
  m_data->m_semaphore_states.push_back({
      .semaphore = semaphore,
      .value = value,
  });
  return id;
}

void RgBuilder::wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                               u64 value) {
  m_data->m_passes[pass].wait_semaphores.push_back(
      add_semaphore_state(semaphore, value));
}

void RgBuilder::signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                                 u64 value) {
  m_data->m_passes[pass].signal_semaphores.push_back(
      add_semaphore_state(semaphore, value));
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
    const auto &semaphore_states = m_data->m_semaphore_states;

    if (not pass.wait_semaphores.empty()) {
      fmt::println(stderr, "    Waits for semaphores:");
      for (RgSemaphoreStateId state : pass.wait_semaphores) {
        RgSemaphoreId semaphore = semaphore_states[state].semaphore;
        fmt::println(stderr, "      - {}", semaphores[semaphore].name);
      }
    }

    if (not pass.signal_semaphores.empty()) {
      fmt::println(stderr, "    Signals semaphores:");
      for (RgSemaphoreStateId state : pass.signal_semaphores) {
        RgSemaphoreId semaphore = semaphore_states[state].semaphore;
        fmt::println(stderr, "      - {}", semaphores[semaphore].name);
      }
    }

    fmt::println(stderr, "");
  }
#endif
}

auto RgBuilder::alloc_textures() -> Result<void, Error> {
  bool need_alloc = false;
  auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
    const RgTextureUse &use = m_data->m_texture_uses[use_id];
    const RgTexture &texture = m_rgp->m_textures[use.texture];
    RgPhysicalTextureId physical_texture_id = texture.parent;
    RgPhysicalTexture &physical_texture =
        m_rgp->m_physical_textures[physical_texture_id];
    rhi::ImageUsageFlags usage = get_texture_usage_flags(use.state.access_mask);
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
    return {};
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
    ren_try(physical_texture.handle,
            m_rgp->m_texture_arena.create_texture({
#if REN_RG_DEBUG
                .name = physical_texture.name,
#endif
                .format = physical_texture.format,
                .usage = physical_texture.usage,
                .width = physical_texture.size.x,
                .height = physical_texture.size.y,
                .depth = physical_texture.size.z,
                .num_mip_levels = physical_texture.num_mip_levels,
                .num_array_layers = physical_texture.num_array_layers,
            }));
    physical_texture.state = {};
  }

  // Schedule init passes before all other passes.
  if (m_data->m_schedule.size() != num_passes) {
    std::ranges::rotate(m_data->m_schedule,
                        m_data->m_schedule.begin() + num_passes);
  }

  return {};
}

void RgBuilder::alloc_buffers(DeviceBumpAllocator &device_allocator,
                              UploadBumpAllocator &upload_allocator) {
  for (auto i : range(m_data->m_physical_buffers.size())) {
    RgPhysicalBufferId id(i);
    RgPhysicalBuffer &physical_buffer = m_data->m_physical_buffers[id];
    if (physical_buffer.view.buffer) {
      continue;
    }
    switch (rhi::MemoryHeap heap = physical_buffer.heap) {
    default:
      unreachable("Unsupported RenderGraph buffer heap: {}", int(heap));
    case rhi::MemoryHeap::Default: {
      physical_buffer.view =
          device_allocator.allocate(physical_buffer.size).slice;
    } break;
    case rhi::MemoryHeap::Upload:
    case rhi::MemoryHeap::DeviceUpload: {
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
        [&](RgRenderPass &graphics_pass) {
          rt_pass.ext = RgRtRenderPass{
              .base_render_target = u32(m_rt_data->m_render_targets.size()),
              .num_render_targets = u32(graphics_pass.render_targets.size()),
              .depth_stencil_target = graphics_pass.depth_stencil_target.map(
                  [&](const RgDepthStencilTarget &) -> u32 {
                    return m_rt_data->m_depth_stencil_targets.size();
                  }),
              .cb = std::move(graphics_pass.cb),
          };
          m_rt_data->m_render_targets.append(graphics_pass.render_targets);
          graphics_pass.depth_stencil_target.map(
              [&](const RgDepthStencilTarget &att) {
                m_rt_data->m_depth_stencil_targets.push_back(att);
              });
        },
        [&](RgCallback &cb) { rt_pass.ext = std::move(cb); },
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

auto RgBuilder::init_runtime_textures() -> Result<void, Error> {
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
        use.state.access_mask.is_set(rhi::Access::UnorderedAccess)
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

    if (use.state.access_mask.is_set(rhi::Access::ShaderImageRead)) {
      SrvDesc srv = {
          .texture = physical_texture.handle,
          .dimension = rhi::ImageViewDimension::e2D,
      };
      if (use.sampler) {
        ren_try(descriptors.combined,
                m_descriptor_allocator->allocate_sampled_texture(
                    *m_renderer, srv, use.sampler));
      } else {
        ren_try(descriptors.sampled,
                m_descriptor_allocator->allocate_texture(*m_renderer, srv));
      }
    } else if (use.state.access_mask.is_set(rhi::Access::UnorderedAccess)) {
      descriptors.num_mips = physical_texture.num_mip_levels;
      descriptors.storage =
          &rt_storage_texture_descriptors[num_storage_texture_descriptors];
      for (u32 mip : range(descriptors.num_mips)) {
        ren_try(descriptors.storage[mip],
                m_descriptor_allocator->allocate_storage_texture(
                    *m_renderer, {
                                     .texture = physical_texture.handle,
                                     .dimension = rhi::ImageViewDimension::e2D,
                                     .mip_level = mip,
                                 }));
      }

      num_storage_texture_descriptors += physical_texture.num_mip_levels;
    }
  }

  return {};
}

void RgBuilder::place_barriers_and_semaphores() {
  Vector<rhi::BufferState> buffer_after_write_hazard_src_states(
      m_data->m_physical_buffers.size());
  Vector<rhi::PipelineStageMask> buffer_after_read_hazard_src_states(
      m_data->m_physical_buffers.size());

  for (auto i : range(m_data->m_physical_buffers.size())) {
    RgPhysicalBuffer &physical_buffer = m_data->m_physical_buffers[i];
    const rhi::BufferState &state = physical_buffer.state;
    if (state.access_mask.is_any_set(rhi::WRITE_ONLY_ACCESS_MASK)) {
      buffer_after_write_hazard_src_states[i] = {
          .stage_mask = state.stage_mask,
          .access_mask = state.access_mask & rhi::WRITE_ONLY_ACCESS_MASK,
      };
    } else {
      buffer_after_read_hazard_src_states[i] = state.stage_mask;
    }
  }

  Vector<rhi::MemoryState> texture_after_write_hazard_src_states(
      m_rgp->m_physical_textures.size());
  Vector<rhi::PipelineStageMask> texture_after_read_hazard_src_states(
      m_rgp->m_physical_textures.size());
  Vector<rhi::ImageLayout> texture_layouts(m_rgp->m_physical_textures.size());

  for (auto i : range(m_rgp->m_physical_textures.size())) {
    RgPhysicalTexture &physical_texture = m_rgp->m_physical_textures[i];
    if (!physical_texture.handle) {
      continue;
    }
    const rhi::ImageState &state = physical_texture.state;
    if (state.access_mask.is_any_set(rhi::WRITE_ONLY_ACCESS_MASK)) {
      texture_after_write_hazard_src_states[i] = {
          .stage_mask = state.stage_mask,
          .access_mask = state.access_mask & rhi::WRITE_ONLY_ACCESS_MASK,
      };
    } else {
      texture_after_read_hazard_src_states[i] = state.stage_mask;
    }
    texture_layouts[i] =
        m_rgp->m_persistent_textures[i] or m_rgp->m_external_textures[i]
            ? state.layout
            : rhi::ImageLayout::Undefined;
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

      rhi::PipelineStageMask dst_stage_mask = use.usage.stage_mask;
      rhi::AccessMask dst_access_mask = use.usage.access_mask;

      // Don't need a barrier for host-only accesses
      if (!dst_stage_mask) {
        ren_assert(!dst_access_mask);
        return;
      }

      rhi::PipelineStageMask src_stage_mask;
      rhi::AccessMask src_access_mask;

      if (dst_access_mask.is_any_set(rhi::WRITE_ONLY_ACCESS_MASK)) {
        rhi::BufferState &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        // Reset the source stage mask that the
        // next WAR hazard will use
        src_stage_mask = std::exchange(
            buffer_after_read_hazard_src_states[physical_buffer], {});
        // If this is a WAR hazard, need to
        // wait for all previous reads to
        // finish. The previous write's memory
        // has already been made available by
        // previous RAW barriers, so it only
        // needs to be made visible
        // FIXME: According to the Vulkan spec,
        // WAR hazards require only an
        // execution barrier
        if (!src_stage_mask) {
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
            dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;
      } else {
        // This is a RAW hazard. Need to wait
        // for the previous write to finish and
        // make it's memory available and
        // visible
        // TODO/FIXME: all RAW barriers should
        // be merged, since if they are issued
        // separately they might cause the
        // cache to be flushed multiple times
        const rhi::BufferState &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        src_stage_mask = after_write_state.stage_mask;
        src_access_mask = after_write_state.access_mask;
        // Update the source stage mask that
        // the next WAR hazard will use
        buffer_after_read_hazard_src_states[physical_buffer] |= dst_stage_mask;
      }

      // First barrier isn't required and can
      // be skipped
      if (!src_stage_mask) {
        ren_assert(!src_access_mask);
        return;
      }

      m_memory_barriers.push_back({
          .src_stage_mask = src_stage_mask,
          .src_access_mask = src_access_mask,
          .dst_stage_mask = dst_stage_mask,
          .dst_access_mask = dst_access_mask,
      });
    };

    std::ranges::for_each(pass.read_buffers, maybe_place_barrier_for_buffer);
    std::ranges::for_each(pass.write_buffers, maybe_place_barrier_for_buffer);

    auto maybe_place_barrier_for_texture = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_texture_uses[use_id];
      RgPhysicalTextureId physical_texture = m_textures[use.texture].parent;

      rhi::PipelineStageMask dst_stage_mask = use.state.stage_mask;
      rhi::AccessMask dst_access_mask = use.state.access_mask;
      rhi::ImageLayout dst_layout = use.state.layout;
      ren_assert(dst_layout != rhi::ImageLayout::Undefined);
      if (dst_layout != rhi::ImageLayout::Present) {
        ren_assert(dst_stage_mask);
        ren_assert(dst_access_mask);
      }

      rhi::ImageLayout &src_layout = texture_layouts[physical_texture];

      if (dst_layout == src_layout) {
        // Only a memory barrier is required if
        // layout doesn't change NOTE: this code is
        // copy-pasted from above

        rhi::PipelineStageMask src_stage_mask;
        rhi::AccessMask src_access_mask;

        if (dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK) {
          rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = std::exchange(
              texture_after_read_hazard_src_states[physical_texture], {});
          if (!src_stage_mask) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          after_write_state.stage_mask = dst_stage_mask;
          after_write_state.access_mask =
              dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;
        } else {
          const rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
          texture_after_read_hazard_src_states[physical_texture] |=
              dst_stage_mask;
        }

        ren_assert(src_stage_mask);

        m_memory_barriers.push_back({
            .src_stage_mask = src_stage_mask,
            .src_access_mask = src_access_mask,
            .dst_stage_mask = dst_stage_mask,
            .dst_access_mask = dst_access_mask,
        });
      } else {
        // Need an image barrier to change layout.
        // Layout transitions are read-write
        // operations, so only to take care of WAR
        // and WAW hazards in this case

        rhi::MemoryState &after_write_state =
            texture_after_write_hazard_src_states[physical_texture];
        // If this is a WAR hazard, must wait for
        // all previous reads to finish and make
        // the layout transition's memory
        // available. Also reset the source stage
        // mask that the next WAR barrier will use
        rhi::PipelineStageMask src_stage_mask = std::exchange(
            texture_after_read_hazard_src_states[physical_texture], {});
        rhi::AccessMask src_access_mask;
        if (!src_stage_mask) {
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
            dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;

        m_texture_barriers.push_back({
            .resource = {m_rgp->m_physical_textures[physical_texture].handle},
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

    std::ranges::for_each(pass.read_textures, maybe_place_barrier_for_texture);
    std::ranges::for_each(pass.write_textures, maybe_place_barrier_for_texture);

    auto place_semaphore = [&](RgSemaphoreStateId id) {
      const RgSemaphoreSignal &signal = m_data->m_semaphore_states[id];
      m_semaphore_submit_info.push_back({
          .semaphore = m_rgp->m_semaphores[signal.semaphore].handle,
          .value = signal.value,
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
    rhi::PipelineStageMask stage_mask = buffer_after_read_hazard_src_states[i];
    rhi::AccessMask access_mask;
    if (!stage_mask) {
      const rhi::BufferState &state = buffer_after_write_hazard_src_states[i];
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
    rhi::PipelineStageMask stage_mask = texture_after_read_hazard_src_states[i];
    rhi::AccessMask access_mask;
    if (!stage_mask) {
      const rhi::MemoryState &state = texture_after_write_hazard_src_states[i];
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
                      UploadBumpAllocator &upload_allocator)
    -> Result<RenderGraph, Error> {
  ren_prof_zone("RgBuilder::build");

  m_rgp->rotate_textures();

  ren_try_to(alloc_textures());
  alloc_buffers(device_allocator, upload_allocator);

#if 0
  dump_pass_schedule();
#endif

  init_runtime_passes();
  init_runtime_buffers();
  ren_try_to(init_runtime_textures());

  place_barriers_and_semaphores();

  RenderGraph rg;
  rg.m_renderer = m_renderer;
  rg.m_rgp = m_rgp;
  rg.m_data = m_rt_data;
  rg.m_upload_allocator = &upload_allocator;
  rg.m_resource_descriptor_heap =
      m_descriptor_allocator->get_resource_descriptor_heap();
  rg.m_sampler_descriptor_heap =
      m_descriptor_allocator->get_sampler_descriptor_heap();
  rg.m_semaphores = &m_rgp->m_semaphores;

  return rg;
}

auto RgBuilder::get_final_buffer_state(RgUntypedBufferId buffer) const
    -> rhi::BufferState {
  ren_assert(buffer);
  RgPhysicalBufferId physical_buffer = m_data->m_buffers[buffer].parent;
  return m_data->m_physical_buffers[physical_buffer].state;
}

RgPassBuilder::RgPassBuilder(RgPassId pass, RgBuilder &builder) {
  m_pass = pass;
  m_builder = &builder;
}

auto RgPassBuilder::read_buffer(RgUntypedBufferId buffer,
                                const rhi::BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  return m_builder->read_buffer(m_pass, buffer, usage, offset);
}

auto RgPassBuilder::write_buffer(RgDebugName name, RgUntypedBufferId buffer,
                                 const rhi::BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  return m_builder->write_buffer(m_pass, std::move(name), buffer, usage);
}

auto RgPassBuilder::read_texture(RgTextureId texture,
                                 const rhi::ImageState &usage,
                                 u32 temporal_layer) -> RgTextureToken {
  return read_texture(texture, usage, NullHandle, temporal_layer);
}

auto RgPassBuilder::read_texture(RgTextureId texture,
                                 const rhi::ImageState &usage,
                                 Handle<Sampler> sampler, u32 temporal_layer)
    -> RgTextureToken {
  return m_builder->read_texture(m_pass, texture, usage, sampler,
                                 temporal_layer);
}

auto RgPassBuilder::write_texture(RgDebugName name, RgTextureId texture,
                                  const rhi::ImageState &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  return m_builder->write_texture(m_pass, std::move(name), texture, usage);
}

auto RgPassBuilder::write_texture(RgDebugName name, RgTextureId texture,
                                  RgTextureId *new_texture,
                                  const rhi::ImageState &usage)
    -> RgTextureToken {
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
                                  const rhi::ImageState &usage)
    -> RgTextureToken {
  RgTextureToken token;
  std::tie(*texture, token) = write_texture(std::move(name), *texture, usage);
  return token;
}

void RgPassBuilder::add_render_target(u32 index, RgTextureToken texture,
                                      const ColorAttachmentOperations &ops) {
  auto &render_targets = m_builder->m_data->m_passes[m_pass]
                             .ext.get_or_emplace<RgRenderPass>()
                             .render_targets;
  if (render_targets.size() <= index) {
    render_targets.resize(index + 1);
  }
  render_targets[index] = {
      .texture = texture,
      .ops = ops,
  };
}

void RgPassBuilder::add_depth_stencil_target(
    RgTextureToken texture, const DepthAttachmentOperations &ops) {
  m_builder->m_data->m_passes[m_pass]
      .ext.get_or_emplace<RgRenderPass>()
      .depth_stencil_target = RgDepthStencilTarget{
      .texture = texture,
      .ops = ops,
  };
}

auto RgPassBuilder::write_render_target(RgDebugName name, RgTextureId texture,
                                        const ColorAttachmentOperations &ops,
                                        u32 index)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, rhi::RENDER_TARGET);
  add_render_target(index, token, ops);
  return {new_texture, token};
}

auto RgPassBuilder::read_depth_stencil_target(RgTextureId texture,
                                              u32 temporal_layer)
    -> RgTextureToken {
  RgTextureToken token = read_texture(texture, rhi::READ_DEPTH_STENCIL_TARGET,
                                      NullHandle, temporal_layer);
  add_depth_stencil_target(token, {
                                      .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                      .store = VK_ATTACHMENT_STORE_OP_NONE,
                                  });
  return token;
}

auto RgPassBuilder::write_depth_stencil_target(
    RgDebugName name, RgTextureId texture, const DepthAttachmentOperations &ops)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, rhi::DEPTH_STENCIL_TARGET);
  add_depth_stencil_target(token, ops);
  return {new_texture, token};
}

void RgPassBuilder::wait_semaphore(RgSemaphoreId semaphore, u64 value) {
  m_builder->wait_semaphore(m_pass, semaphore, value);
}

void RgPassBuilder::signal_semaphore(RgSemaphoreId semaphore, u64 value) {
  m_builder->signal_semaphore(m_pass, semaphore, value);
}

auto RenderGraph::execute(Handle<CommandPool> cmd_pool) -> Result<void, Error> {
  ren_prof_zone("RenderGraph::execute");

  RgRuntime rt;
  rt.m_rg = this;

  CommandRecorder cmd;
  Span<const SemaphoreState> batch_wait_semaphores;
  Span<const SemaphoreState> batch_signal_semaphores;

  auto submit_batch = [&] -> Result<void, Error> {
    if (!cmd and batch_wait_semaphores.empty() and
        batch_signal_semaphores.empty()) {
      return {};
    }
    ren_assert(cmd);
    ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());
    ren_try_to(m_renderer->submit(rhi::QueueFamily::Graphics, {cmd_buffer},
                                  batch_wait_semaphores,
                                  batch_signal_semaphores));
    batch_wait_semaphores = {};
    batch_signal_semaphores = {};
    return {};
  };

  for (const RgRtPass &pass : m_data->m_passes) {
    ren_prof_zone("RenderGraph::execute_pass");
#if REN_RG_DEBUG
    ren_prof_zone_text(m_data->m_pass_names[pass.pass]);
#endif
    if (pass.num_wait_semaphores > 0) {
      ren_try_to(submit_batch());
      batch_wait_semaphores =
          Span(m_data->m_semaphore_submit_info)
              .subspan(pass.base_wait_semaphore, pass.num_wait_semaphores);
    }

    if (!cmd) {
      ren_try_to(cmd.begin(*m_renderer, cmd_pool));
    }

    {
#if REN_RG_DEBUG
      DebugRegion debug_region =
          cmd.debug_region(m_data->m_pass_names[pass.pass].c_str());
#endif

      if (pass.num_memory_barriers > 0 or pass.num_texture_barriers > 0) {
        auto memory_barriers =
            Span(m_data->m_memory_barriers)
                .subspan(pass.base_memory_barrier, pass.num_memory_barriers);
        auto texture_barriers =
            Span(m_data->m_texture_barriers)
                .subspan(pass.base_texture_barrier, pass.num_texture_barriers);
        cmd.pipeline_barrier(memory_barriers, texture_barriers);
      }

      pass.ext.visit(OverloadSet{
          [&](const RgRtRenderPass &pass) {
            glm::uvec2 viewport = {-1, -1};

            auto render_targets =
                Span(m_data->m_render_targets)
                    .subspan(pass.base_render_target, pass.num_render_targets) |
                map([&](const Optional<RgRenderTarget> &att)
                        -> Optional<ColorAttachment> {
                  return att.map(
                      [&](const RgRenderTarget &att) -> ColorAttachment {
                        Handle<Texture> texture = rt.get_texture(att.texture);
                        viewport = get_texture_size(*m_renderer, texture);
                        return {.rtv = {texture}, .ops = att.ops};
                      });
                }) |
                std::ranges::to<StaticVector<Optional<ColorAttachment>,
                                             rhi::MAX_NUM_RENDER_TARGETS>>();

            auto depth_stencil_target = pass.depth_stencil_target.map(
                [&](u32 index) -> DepthStencilAttachment {
                  const RgDepthStencilTarget &att =
                      m_data->m_depth_stencil_targets[index];
                  Handle<Texture> texture = rt.get_texture(att.texture);
                  viewport = get_texture_size(*m_renderer, texture);
                  return {.dsv = {texture}, .ops = att.ops};
                });

            RenderPass render_pass = cmd.render_pass({
                .color_attachments = render_targets,
                .depth_stencil_attachment = depth_stencil_target,
            });
            render_pass.set_viewports({{
                .width = float(viewport.x),
                .height = float(viewport.y),
                .maxDepth = 1.0f,
            }});
            render_pass.set_scissor_rects(
                {{.extent = {viewport.x, viewport.y}}});

            pass.cb(*m_renderer, rt, render_pass);
          },
          [&](const RgCallback &cb) { cb(*m_renderer, rt, cmd); },
      });
    }

    if (pass.num_signal_semaphores > 0) {
      batch_signal_semaphores =
          Span(m_data->m_semaphore_submit_info)
              .subspan(pass.base_signal_semaphore, pass.num_signal_semaphores);
      ren_try_to(submit_batch());
    }
  }

  ren_try_to(submit_batch());

  for (RgTextureId texture : m_rgp->m_frame_textures) {
    m_rgp->m_textures.erase(texture);
  }
  m_rgp->m_frame_textures.clear();

  return {};
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

auto RgRuntime::get_allocator() const -> UploadBumpAllocator & {
  return *m_rg->m_upload_allocator;
}

auto RgRuntime::get_semaphore(RgSemaphoreId semaphore) const
    -> Handle<Semaphore> {
  return m_rg->m_semaphores->get(semaphore).handle;
}

} // namespace ren
