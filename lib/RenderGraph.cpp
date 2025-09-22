#include "RenderGraph.hpp"
#include "CommandRecorder.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"

#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace ren {

auto RgPersistent::create_texture(RgTextureCreateInfo &&create_info)
    -> RgTextureId {
#if REN_RG_DEBUG
  ren_assert(not create_info.name.empty());
#endif

  RgPhysicalTextureId id(m_physical_textures.size());
  m_physical_textures.emplace_back();
  m_persistent_textures.push_back(create_info.persistent);
  m_external_textures.push_back(false);

#if REN_RG_DEBUG
  String handle_name = std::move(create_info.name);
  String name;
  if (!create_info.persistent) {
    name = fmt::format("rg#{}", handle_name);
  } else {
    name = handle_name;
  }
#endif

  m_physical_textures.back() = RgPhysicalTexture{
#if REN_RG_DEBUG
      .name = std::move(handle_name),
#endif
      .format = create_info.format,
      .size = {create_info.width, create_info.height, create_info.depth},
      .cube_map = create_info.cube_map,
      .num_mips = create_info.num_mips,
      .num_layers = create_info.num_layers,
      .id = m_textures.insert({
#if REN_RG_DEBUG
          .name = std::move(name),
#endif
          .parent = id,
      }),
  };

  m_persistent_textures[id] = create_info.persistent;

  return m_physical_textures[id].id;
}

auto RgPersistent::create_texture(RgDebugName name) -> RgTextureId {
#if REN_RG_DEBUG
  ren_assert(not name.empty());
#endif

  RgPhysicalTextureId id(m_physical_textures.size());
  m_physical_textures.emplace_back();
  m_persistent_textures.push_back(false);
  m_external_textures.push_back(true);

#if REN_RG_DEBUG
  String handle_name = std::move(name);
  name = fmt::format("rg#{}", handle_name);
#endif

  m_physical_textures[id] = {
#if REN_RG_DEBUG
      .name = std::move(handle_name),
#endif
      .id = m_textures.insert({
#if REN_RG_DEBUG
          .name = std::move(name),
#endif
          .parent = id,
      }),
  };

  return m_physical_textures[id].id;
}

auto RgPersistent::create_semaphore(RgDebugName name) -> RgSemaphoreId {
  RgSemaphoreId semaphore = m_semaphores.emplace();
#if REN_RG_DEBUG
  m_semaphores[semaphore].name = std::move(name);
#endif
  return semaphore;
}

void RgPersistent::set_async_compute_enabled(bool enabled) {
  m_async_compute = enabled;
}

void RgPersistent::reset() {
  m_arena.clear();
  m_physical_textures.clear();
  m_persistent_textures.clear();
  m_external_textures.clear();
  m_textures.clear();
  m_semaphores.clear();
  m_gfx_semaphore_id = {};
  m_async_semaphore_id = {};
}

void RgPersistent::rotate_textures() {
  for (usize i : range(m_physical_textures.size())) {
    if (not m_persistent_textures[i]) {
      m_physical_textures[i].layout = rhi::ImageLayout::Undefined;
    }
  }
}

namespace {

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
    texture.child = {};
  }

  auto &bd = *m_data;
  bd.m_passes.clear();
  bd.m_gfx_schedule.clear();
  bd.m_async_schedule.clear();
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
  RgQueue queue = create_info.queue;
  if (!m_rgp->m_async_compute) {
    queue = RgQueue::Graphics;
  }
  RgPassId pass_id = m_data->m_passes.emplace(RgPass{.queue = queue});
#if REN_RG_DEBUG
  m_rt_data->m_pass_names.insert(pass_id, std::move(create_info.name));
#endif
  if (queue == RgQueue::Async) {
    m_data->m_async_schedule.push_back(pass_id);
  } else {
    m_data->m_gfx_schedule.push_back(pass_id);
  }
  return RgPassBuilder(pass_id, *this);
}

auto RgBuilder::add_buffer_use(const RgBufferUse &use) -> RgBufferUseId {
  ren_assert(use.buffer);
  RgBufferUseId id(m_data->m_buffer_uses.size());
  m_data->m_buffer_uses.push_back(use);
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

auto RgBuilder::read_buffer(RgPassId pass_id, RgUntypedBufferId buffer,
                            const rhi::BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  ren_assert(buffer);
  RgPass &pass = m_data->m_passes[pass_id];
  RgBufferUseId use = add_buffer_use({
      .buffer = buffer,
      .offset = offset,
      .usage = usage,
  });
  pass.read_buffers.push_back(use);
  return RgUntypedBufferToken(use);
}

auto RgBuilder::write_buffer(RgPassId pass_id, RgDebugName name,
                             RgUntypedBufferId src,
                             const rhi::BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  ren_assert(src);
  RgBuffer &src_buffer = m_data->m_buffers[src];
  ren_assert(src_buffer.def != pass_id);
  RgPass &pass = m_data->m_passes[pass_id];
  m_data->m_physical_buffers[src_buffer.parent].queues |= pass.queue;
  RgBufferUseId use = add_buffer_use({
      .buffer = src,
      .usage = usage,
  });
  pass.write_buffers.push_back(use);
#if REN_RG_DEBUG
  if (src_buffer.name == "rg#") {
    ren_assert(not name.empty());
    src_buffer.name = fmt::format("rg#{}", name);
  }
#endif
  RgUntypedBufferId dst = create_virtual_buffer(pass_id, std::move(name), src);
  return {dst, RgUntypedBufferToken(use)};
}

void RgBuilder::clear_texture(RgDebugName name, NotNull<RgTextureId *> texture,
                              const glm::vec4 &color, RgQueue queue) {
  auto pass = create_pass({.name = "clear-texture", .queue = queue});
  auto token =
      pass.write_texture(std::move(name), texture, rhi::TRANSFER_DST_IMAGE);
  pass.set_callback(
      [token, color](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
        cmd.clear_texture(rg.get_texture(token), color);
      });
}

void RgBuilder::copy_texture_to_buffer(RgTextureId src, RgDebugName name,
                                       RgUntypedBufferId *dst, RgQueue queue) {
  auto pass = create_pass({.name = "copy-texture-to-buffer", .queue = queue});
  auto src_token = pass.read_texture(src, rhi::TRANSFER_SRC_IMAGE);
  auto dst_token =
      pass.write_buffer(std::move(name), dst, rhi::TRANSFER_DST_BUFFER);
  pass.set_callback([src_token, dst_token](Renderer &, const RgRuntime &rg,
                                           CommandRecorder &cmd) {
    cmd.copy_texture_to_buffer(rg.get_texture(src_token),
                               rg.get_buffer(dst_token));
  });
}

auto RgBuilder::add_texture_use(const RgTextureUse &use) -> RgTextureUseId {
  ren_assert(use.texture);
  RgTextureUseId id(m_data->m_texture_uses.size());
  m_data->m_texture_uses.push_back(use);
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
  ren_assert_msg(!m_rgp->m_textures[parent].child,
                 "Render graph textures can only be written once");
  m_rgp->m_textures[parent].child = texture;
  return texture;
}

auto RgBuilder::read_texture(RgPassId pass_id, RgTextureId texture,
                             const rhi::ImageState &usage, rhi::Sampler sampler)
    -> RgTextureToken {
  ren_assert(texture);
  if (sampler) {
    ren_assert_msg(usage.access_mask.is_set(rhi::Access::ShaderImageRead),
                   "Sampler must be null if texture is not sampled");
  }
  RgPass &pass = m_data->m_passes[pass_id];
  for (RgTextureUseId use_id : pass.read_textures) {
    RgTextureUse &use = m_data->m_texture_uses[use_id];
    if (use.texture == texture) {
      use.state = use.state | usage;
      ren_assert(!use.sampler or use.sampler == sampler);
      use.sampler = sampler;
      return RgTextureToken(use_id);
    }
  }
  RgPhysicalTextureId physical_texture = m_rgp->m_textures[texture].parent;
  RgTextureUseId use = add_texture_use({
      .texture = texture,
      .sampler = sampler,
      .state = usage,
  });
  pass.read_textures.push_back(use);
  return RgTextureToken(use);
}

auto RgBuilder::write_texture(RgPassId pass_id, RgDebugName name,
                              RgTextureId src, const rhi::ImageState &usage)
    -> std::tuple<RgTextureId, RgTextureToken> {
  ren_assert(src);
  ren_assert(m_rgp->m_textures[src].def != pass_id);
  RgPass &pass = m_data->m_passes[pass_id];
  RgTextureId dst = create_virtual_texture(pass_id, std::move(name), src);
  RgTextureUseId use = add_texture_use({
      .texture = src,
      .state = usage,
  });
  pass.write_textures.push_back(use);
  return {dst, RgTextureToken(use)};
}

auto RgBuilder::write_texture(RgPassId pass_id, RgTextureId dst_id,
                              RgTextureId src_id, const rhi::ImageState &usage)
    -> RgTextureToken {
  ren_assert(pass_id);
  ren_assert(dst_id);
  ren_assert(src_id);
  RgTexture &dst = m_rgp->m_textures[dst_id];
  RgTexture &src = m_rgp->m_textures[src_id];
  ren_assert(dst.parent == src.parent);
  dst.def = pass_id;
  src.kill = pass_id;
  ren_assert(!src.child);
  src.child = dst_id;
  RgPass &pass = m_data->m_passes[pass_id];
  RgTextureUseId use = add_texture_use({
      .texture = src_id,
      .state = usage,
  });
  pass.write_textures.push_back(use);
  return RgTextureToken(use);
}

void RgBuilder::set_external_buffer(RgUntypedBufferId id,
                                    const BufferView &view) {
  RgPhysicalBufferId physical_buffer_id = m_data->m_buffers[id].parent;
  RgPhysicalBuffer &physical_buffer =
      m_data->m_physical_buffers[physical_buffer_id];
  ren_assert(!physical_buffer.view.buffer);
  physical_buffer.view = view;
}

void RgBuilder::set_external_texture(RgTextureId id, Handle<Texture> handle) {
  const Texture &texture = m_renderer->get_texture(handle);
  RgPhysicalTextureId ptex_id = m_rgp->m_textures[id].parent;
  ren_assert(m_rgp->m_external_textures[ptex_id]);
  RgPhysicalTexture &ptex = m_rgp->m_physical_textures[ptex_id];
  ptex = {
#if REN_RG_DEBUG
      .name = std::move(ptex.name),
#endif
      .format = texture.format,
      .usage = texture.usage,
      .size = texture.size,
      .num_mips = texture.num_mips,
      .num_layers = texture.num_layers,
      .handle = handle,
      .layout = ptex.layout,
      .id = ptex.id,
      .last_queue = RgQueue::None,
      .queue = RgQueue::None,
      .last_time = 0,
      .time = 0,
  };
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
  ren_assert(handle);
  m_rgp->m_semaphores[semaphore].handle = handle;
}

void RgBuilder::dump_pass_schedule() const {
#if REN_RG_DEBUG
  SmallVector<RgUntypedBufferId> create_buffers;
  SmallVector<RgUntypedBufferId> write_buffers;
  SmallVector<RgTextureId> create_textures;
  SmallVector<RgTextureId> write_textures;

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    Span<const RgPassId> schedule = m_data->m_gfx_schedule;
    if (queue == RgQueue::Graphics) {
      fmt::println(stderr, "Graphics queue passes:");
    } else {
      schedule = m_data->m_async_schedule;
      if (schedule.empty()) {
        continue;
      }
      fmt::println(stderr, "Async compute queue passes:");
    }

    for (RgPassId pass_id : schedule) {
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
        for (RgSemaphoreStateId state_id : pass.wait_semaphores) {
          const RgSemaphoreState &state = semaphore_states[state_id];
          if (state.value) {
            fmt::println(stderr, "      - {}, {}",
                         semaphores[state.semaphore].name, state.value);
          } else {
            fmt::println(stderr, "      - {}",
                         semaphores[state.semaphore].name);
          }
        }
      }

      if (not pass.signal_semaphores.empty()) {
        fmt::println(stderr, "    Signals semaphores:");
        for (RgSemaphoreStateId state_id : pass.signal_semaphores) {
          const RgSemaphoreState &state = semaphore_states[state_id];
          if (state.value) {
            fmt::println(stderr, "      - {}, {}",
                         semaphores[state.semaphore].name, state.value);
          } else {
            fmt::println(stderr, "      - {}",
                         semaphores[state.semaphore].name);
          }
        }
      }

      fmt::println(stderr, "");
    }
  }
#endif
}

auto RgBuilder::alloc_textures() -> Result<void, Error> {
  bool need_alloc = false;
  auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
    const RgTextureUse &use = m_data->m_texture_uses[use_id];
    const RgTexture &texture = m_rgp->m_textures[use.texture];
    RgPhysicalTextureId ptex_id = texture.parent;
    RgPhysicalTexture &ptex = m_rgp->m_physical_textures[ptex_id];
    rhi::ImageUsageFlags usage = get_texture_usage_flags(use.state.access_mask);
    if (m_rgp->m_external_textures[ptex_id]) {
      ren_assert((ptex.usage & usage) == usage);
    } else {
      bool needs_usage_update = (ptex.usage | usage) != ptex.usage;
      ptex.usage |= usage;
      if (!ptex.handle or needs_usage_update) {
        need_alloc = true;
      }
    }
  };
  for (const auto &[_, pass] : m_data->m_passes) {
    std::ranges::for_each(pass.read_textures, update_texture_usage_flags);
    std::ranges::for_each(pass.write_textures, update_texture_usage_flags);
  }

  if (not need_alloc) {
    return {};
  }

  usize num_gfx_passes = m_data->m_gfx_schedule.size();

  m_rgp->m_arena.clear();
  for (auto i : range(m_rgp->m_physical_textures.size())) {
    RgPhysicalTexture &physical_texture = m_rgp->m_physical_textures[i];
    // Skip unused temporal or external textures.
    if (!physical_texture.usage or m_rgp->m_external_textures[i]) {
      continue;
    }
    // Preprocessor statement inside function-like macro is UB
#if REN_RG_DEBUG
#define name_eq_physical_texture_name .name = physical_texture.name,
#else
#define name_eq_physical_texture_name
#endif
    ren_try(physical_texture.handle,
            m_rgp->m_arena.create_texture({
                // clang-format off
                name_eq_physical_texture_name
                .format = physical_texture.format,
                .usage = physical_texture.usage,
                .width = physical_texture.size.x,
                .height = physical_texture.size.y,
                .depth = physical_texture.size.z,
                .cube_map = physical_texture.cube_map,
                .num_mips = physical_texture.num_mips,
                .num_layers = physical_texture.num_layers,
                // clang-format on
            }));
    physical_texture.layout = rhi::ImageLayout::Undefined;
  }

  ren_try(m_rgp->m_gfx_semaphore,
          m_rgp->m_arena.create_semaphore({
              .name = "Render graph graphics queue timeline",
              .type = rhi::SemaphoreType::Timeline,
              .initial_value = m_rgp->m_gfx_time,
          }));
  ren_try(m_rgp->m_async_semaphore,
          m_rgp->m_arena.create_semaphore({
              .name = "Render graph async compute queue timeline",
              .type = rhi::SemaphoreType::Timeline,
              .initial_value = m_rgp->m_async_time,
          }));
  if (!m_rgp->m_gfx_semaphore_id) {
    m_rgp->m_gfx_semaphore_id = m_rgp->create_semaphore("gfx-queue-timeline");
  }
  if (!m_rgp->m_async_semaphore_id) {
    m_rgp->m_async_semaphore_id =
        m_rgp->create_semaphore("async-queue-timeline");
  }
  set_external_semaphore(m_rgp->m_gfx_semaphore_id, m_rgp->m_gfx_semaphore);
  set_external_semaphore(m_rgp->m_async_semaphore_id, m_rgp->m_async_semaphore);

  // Schedule init passes before all other passes.
  if (m_data->m_gfx_schedule.size() != num_gfx_passes) {
    std::ranges::rotate(m_data->m_gfx_schedule,
                        m_data->m_gfx_schedule.begin() + num_gfx_passes);
  }

  return {};
}

void RgBuilder::alloc_buffers(DeviceBumpAllocator &gfx_allocator,
                              DeviceBumpAllocator &async_allocator,
                              DeviceBumpAllocator &shared_allocator,
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
      DeviceBumpAllocator *allocator = nullptr;
      if (physical_buffer.queues == RgQueue::Graphics) {
        allocator = &gfx_allocator;
      } else if (physical_buffer.queues == RgQueue::Async) {
        allocator = &async_allocator;
      } else {
        ren_assert(physical_buffer.queues != RgQueue::None);
        allocator = &shared_allocator;
      }
      physical_buffer.view = allocator->allocate(physical_buffer.size).slice;
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
  if (not m_rgp->m_async_compute) {
    ren_assert(m_data->m_async_schedule.empty());
  }

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    auto *rt_passes = &m_rt_data->m_gfx_passes;
    Span<const RgPassId> schedule = m_data->m_gfx_schedule;
    if (queue == RgQueue::Async) {
      rt_passes = &m_rt_data->m_async_passes;
      schedule = m_data->m_async_schedule;
    }

    rt_passes->clear();
    rt_passes->reserve(schedule.size());

    for (RgPassId pass_id : schedule) {
      RgPass &pass = m_data->m_passes[pass_id];
      RgRtPass &rt_pass = rt_passes->emplace_back();
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
                .depth_stencil_target =
                    graphics_pass.depth_stencil_target.texture
                        ? Optional(m_rt_data->m_depth_stencil_targets.size())
                        : None,
                .cb = std::move(graphics_pass.cb),
            };
            m_rt_data->m_render_targets.append(graphics_pass.render_targets);
            if (graphics_pass.depth_stencil_target.texture) {
              m_rt_data->m_depth_stencil_targets.push_back(
                  graphics_pass.depth_stencil_target);
            }
          },
          [&](RgCallback &cb) { rt_pass.ext = std::move(cb); },
      });
    }
  }
}

void RgBuilder::add_inter_queue_semaphores() {
  usize new_gfx_time = m_rgp->m_gfx_time;
  usize new_async_time = m_rgp->m_async_time;

  for (RgPassId pass_id : m_data->m_gfx_schedule) {
    RgPass &pass = m_data->m_passes[pass_id];
    pass.signal_time = ++new_gfx_time;
  }

  for (RgPassId pass_id : m_data->m_async_schedule) {
    RgPass &pass = m_data->m_passes[pass_id];
    pass.signal_time = ++new_async_time;
  }

  // Compute dependencies and update last-used info.
  for (auto schedule : {m_data->m_gfx_schedule, m_data->m_async_schedule}) {
    for (RgPassId pass_id : schedule) {
      RgPass &pass = m_data->m_passes[pass_id];

      for (RgBufferUseId use : pass.read_buffers) {
        const RgBuffer &buffer =
            m_data->m_buffers[m_data->m_buffer_uses[use].buffer];
        if (buffer.def) {
          const RgPass &def = m_data->m_passes[buffer.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        }
        if (buffer.kill) {
          RgPass &kill = m_data->m_passes[buffer.kill];
          if (kill.queue != pass.queue) {
            kill.wait_time = std::max(kill.wait_time, pass.signal_time);
          }
        }
      }

      for (RgBufferUseId use : pass.write_buffers) {
        const RgBuffer &buffer =
            m_data->m_buffers[m_data->m_buffer_uses[use].buffer];
        if (buffer.def) {
          const RgPass &def = m_data->m_passes[buffer.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        }
      }

      for (RgTextureUseId use : pass.read_textures) {
        const RgTexture &texture =
            m_rgp->m_textures[m_data->m_texture_uses[use].texture];

        if (texture.def) {
          const RgPass &def = m_data->m_passes[texture.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        } else {
          // Wait for signal from previous frame.
          const RgPhysicalTexture &ptex =
              m_rgp->m_physical_textures[texture.parent];
          if (pass.queue != ptex.last_queue) {
            pass.wait_time = std::max(pass.wait_time, ptex.last_time);
          }
        }

        if (texture.kill) {
          RgPass &kill = m_data->m_passes[texture.kill];
          if (kill.queue != pass.queue) {
            kill.wait_time = std::max(kill.wait_time, pass.signal_time);
          }
        } else {
          RgPhysicalTexture &ptex = m_rgp->m_physical_textures[texture.parent];
          ren_assert(ptex.queue == RgQueue::None or ptex.queue == pass.queue);
          ptex.queue = pass.queue;
          ptex.time = std::max(ptex.time, pass.signal_time);
        }
      }

      for (RgTextureUseId use : pass.write_textures) {
        const RgTexture &texture =
            m_rgp->m_textures[m_data->m_texture_uses[use].texture];
        if (texture.def) {
          const RgPass &def = m_data->m_passes[texture.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        } else {
          // Wait for signal from previous frame.
          const RgPhysicalTexture &ptex =
              m_rgp->m_physical_textures[texture.parent];
          if (pass.queue != ptex.last_queue) {
            pass.wait_time = std::max(pass.wait_time, ptex.last_time);
          }
        }
      }
    }
  }

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    Span<const RgPassId> schedule = m_data->m_gfx_schedule;
    Span<const RgPassId> other_schedule = m_data->m_async_schedule;
    usize first_signaled_time = m_rgp->m_async_time + 1;
    if (queue == RgQueue::Async) {
      schedule = m_data->m_async_schedule;
      other_schedule = m_data->m_gfx_schedule;
      first_signaled_time = m_rgp->m_gfx_time + 1;
    }
    usize last_waited_time = 0;

    for (RgPassId pass_id : schedule) {
      RgPass &pass = m_data->m_passes[pass_id];

      for (RgTextureUseId use : pass.write_textures) {
        const RgTexture &src_texture =
            m_rgp->m_textures[m_data->m_texture_uses[use].texture];
        const RgTexture &dst_texture = m_rgp->m_textures[src_texture.child];
        RgPhysicalTexture &ptex =
            m_rgp->m_physical_textures[src_texture.parent];
        if (!dst_texture.kill and ptex.queue == RgQueue::None) {
          ptex.queue = pass.queue;
          ptex.time = pass.signal_time;
        }
      }

      // No dependencies or already waited in a previous pass on the same queue.
      if (pass.wait_time <= last_waited_time) {
        continue;
      }

      ren_assert(last_waited_time < pass.wait_time);
      last_waited_time = pass.wait_time;
      pass.wait = true;
      if (pass.wait_time >= first_signaled_time) {
        RgPassId signal_pass_id =
            other_schedule[pass.wait_time - first_signaled_time];
        RgPass &signal_pass = m_data->m_passes[signal_pass_id];
        ren_assert(pass.wait_time == signal_pass.signal_time);
        signal_pass.signal = true;
      }
    }
    if (not schedule.empty()) {
      m_data->m_passes[schedule.back()].signal = true;
    }
  }

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    Span<const RgPassId> schedule = m_data->m_gfx_schedule;
    RgSemaphoreId semaphore = m_rgp->m_gfx_semaphore_id;
    RgSemaphoreId other_semaphore = m_rgp->m_async_semaphore_id;
    if (queue == RgQueue::Async) {
      schedule = m_data->m_async_schedule;
      semaphore = m_rgp->m_async_semaphore_id;
      other_semaphore = m_rgp->m_gfx_semaphore_id;
    }
    for (RgPassId pass_id : std::views::reverse(schedule)) {
      RgPass &pass = m_data->m_passes[pass_id];
      if (pass.signal) {
        signal_semaphore(pass_id, semaphore, pass.signal_time);
      }
      if (pass.wait) {
        ren_assert(pass.wait_time);
        wait_semaphore(pass_id, other_semaphore, pass.wait_time);
      }
    }
  }

  for (RgPhysicalTexture &ptex : m_rgp->m_physical_textures) {
    ptex.last_queue = ptex.queue;
    ptex.last_time = ptex.time;
    ptex.queue = RgQueue::None;
    ptex.time = 0;
  }

  m_rgp->m_gfx_time = new_gfx_time;
  m_rgp->m_async_time = new_async_time;
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

namespace {

auto get_view_dimension(const RgPhysicalTexture &ptex)
    -> rhi::ImageViewDimension {
  if (ptex.size.z > 1) {
    return rhi::ImageViewDimension::e3D;
  }
  if (ptex.cube_map) {
    if (ptex.num_layers > 1) {
      return rhi::ImageViewDimension::eArrayCube;
    }
    return rhi::ImageViewDimension::eCube;
  }
  if (ptex.num_layers > 1) {
    return rhi::ImageViewDimension::eArray2D;
  }
  return rhi::ImageViewDimension::e2D;
}

} // namespace

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
            ? physical_texture.num_mips
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
          .dimension = get_view_dimension(physical_texture),
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
      descriptors.num_mips = physical_texture.num_mips;
      descriptors.storage =
          &rt_storage_texture_descriptors[num_storage_texture_descriptors];
      for (u32 mip : range(descriptors.num_mips)) {
        ren_try(sh::Handle<void> descriptor,
                m_descriptor_allocator->allocate_storage_texture(
                    *m_renderer,
                    {
                        .texture = physical_texture.handle,
                        .dimension = get_view_dimension(physical_texture),
                        .mip = mip,
                    }));
        descriptors.storage[mip] = descriptor.m_id;
      }

      num_storage_texture_descriptors += physical_texture.num_mips;
    }
  }

  return {};
}

void RgBuilder::place_barriers_and_semaphores() {
  Vector<rhi::BufferState> buffer_after_write_hazard_src_states(
      m_data->m_physical_buffers.size());
  Vector<rhi::PipelineStageMask> buffer_after_read_hazard_src_states(
      m_data->m_physical_buffers.size());

  Vector<rhi::MemoryState> texture_after_write_hazard_src_states(
      m_rgp->m_physical_textures.size());
  Vector<rhi::PipelineStageMask> texture_after_read_hazard_src_states(
      m_rgp->m_physical_textures.size());

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

  usize gfx_i = 0;
  usize async_i = 0;

  auto &rt_gfx_passes = m_rt_data->m_gfx_passes;
  auto &rt_async_passes = m_rt_data->m_async_passes;

  while (gfx_i < rt_gfx_passes.size() or async_i < rt_async_passes.size()) {
    RgRtPass &rt_pass = [&]() -> RgRtPass & {
      if (gfx_i == rt_gfx_passes.size()) {
        return rt_async_passes[async_i++];
      }
      if (async_i == rt_async_passes.size()) {
        return rt_gfx_passes[gfx_i++];
      }
      RgRtPass &rt_gfx_pass = rt_gfx_passes[gfx_i];
      const RgPass &gfx_pass = m_data->m_passes[rt_gfx_pass.pass];
      RgRtPass &rt_async_pass = rt_async_passes[async_i];
      const RgPass &async_pass = m_data->m_passes[rt_async_pass.pass];
      if (gfx_pass.wait_time < async_pass.signal_time) {
        gfx_i++;
        return rt_gfx_pass;
      }
      async_i++;
      return rt_async_pass;
    }();
    const RgPass &pass = m_data->m_passes[rt_pass.pass];

    usize old_memory_barrier_count = m_memory_barriers.size();
    usize old_texture_barrier_count = m_texture_barriers.size();
    usize old_semaphore_count = m_semaphore_submit_info.size();

    auto maybe_place_barrier_for_buffer = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      const RgBuffer &buffer = m_data->m_buffers[use.buffer];
      const RgPass *def_pass =
          buffer.def ? &m_data->m_passes[buffer.def] : nullptr;
      const RgPass *kill_pass =
          buffer.kill ? &m_data->m_passes[buffer.kill] : nullptr;
      RgPhysicalBufferId pbuf_id = buffer.parent;

      rhi::PipelineStageMask dst_stage_mask = use.usage.stage_mask;
      rhi::AccessMask dst_access_mask = use.usage.access_mask;

      // Don't need a barrier for host-only accesses
      if (!dst_stage_mask) {
        ren_assert(!dst_access_mask);
        return;
      }

      rhi::PipelineStageMask src_stage_mask;
      rhi::AccessMask src_access_mask;

      if (dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK) {
        ren_assert(kill_pass);
        ren_assert(kill_pass == &pass);
        rhi::BufferState &after_write_state =
            buffer_after_write_hazard_src_states[pbuf_id];
        // Reset the source stage mask that the next WAR hazard will use
        src_stage_mask =
            std::exchange(buffer_after_read_hazard_src_states[pbuf_id], {});
        // If this is a WAR hazard, need to wait for all previous reads on the
        // same queue to finish. The previous write's memory has already been
        // made available by previous RAW barriers or semaphore waits, so it
        // only needs to be made visible.
        if (!src_stage_mask and def_pass and def_pass->queue == pass.queue) {
          // No reads were performed between this write and the previous one so
          // this is a WAW hazard. If the previous write was on the same queue,
          // need to wait for the previous write to finish and make its memory
          // available and visible.
          // NOTE/FIXME: the source stage mask can also be empty if all previous
          // reads were performed on another queue, but let's ignore this for
          // now.
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // hazards on the same queue will use.
        after_write_state.stage_mask = dst_stage_mask;
        // Read accesses are redundant for source access mask.
        after_write_state.access_mask =
            dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;
      } else {
        // This is a RAW hazard. Need to wait for the previous write to finish
        // and make it's memory available and visible if it was performed on the
        // same queue.
        // TODO/FIXME: all RAW barriers should be merged, since if they are
        // issued separately they might cause the cache to be flushed multiple
        // times.
        const rhi::BufferState &after_write_state =
            buffer_after_write_hazard_src_states[pbuf_id];
        if (def_pass and def_pass->queue == pass.queue) {
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        if (kill_pass and kill_pass->queue == pass.queue) {
          // Update the source stage mask that the next WAR hazard will use if
          // it's on the same queue.
          buffer_after_read_hazard_src_states[pbuf_id] |= dst_stage_mask;
        }
      }

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
      const RgTexture &texture = m_rgp->m_textures[use.texture];
      const RgPass *def_pass =
          texture.def ? &m_data->m_passes[texture.def] : nullptr;
      const RgPass *kill_pass =
          texture.kill ? &m_data->m_passes[texture.kill] : nullptr;
      RgPhysicalTextureId ptex_id = m_textures[use.texture].parent;
      RgPhysicalTexture &ptex = m_rgp->m_physical_textures[ptex_id];

      rhi::PipelineStageMask dst_stage_mask = use.state.stage_mask;
      rhi::AccessMask dst_access_mask = use.state.access_mask;
      rhi::ImageLayout dst_layout = use.state.layout;
      ren_assert(dst_layout != rhi::ImageLayout::Undefined);
      if (dst_layout != rhi::ImageLayout::Present) {
        ren_assert(dst_stage_mask);
        ren_assert(dst_access_mask);
      }

      if (dst_layout == ptex.layout) {
        // Only a memory barrier is required if layout doesn't change.
        // However, this can cause the driver (RADV) to be overly conservative
        // with cache flushes. So use a texture barrier instead.
        // NOTE: this code is copy-pasted from above.

        rhi::PipelineStageMask src_stage_mask;
        rhi::AccessMask src_access_mask;

        if (dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK) {
          ren_assert(kill_pass);
          ren_assert(kill_pass == &pass);
          rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[ptex_id];
          src_stage_mask =
              std::exchange(texture_after_read_hazard_src_states[ptex_id], {});
          if (!src_stage_mask and def_pass and def_pass->queue != pass.queue) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          after_write_state.stage_mask = dst_stage_mask;
          after_write_state.access_mask =
              dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;
        } else {
          const rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[ptex_id];
          if (def_pass and def_pass->queue == pass.queue) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          if (kill_pass and kill_pass->queue == pass.queue) {
            texture_after_read_hazard_src_states[ptex_id] |= dst_stage_mask;
          }
        }

        if (!src_stage_mask) {
          ren_assert(!src_access_mask);
          return;
        }

        m_texture_barriers.push_back({
            .resource = {ptex.handle},
            .src_stage_mask = src_stage_mask,
            .src_access_mask = src_access_mask,
            .dst_stage_mask = dst_stage_mask,
            .dst_access_mask = dst_access_mask,
        });
      } else {
        // Need an image barrier to change the layout. Layout transitions are
        // read-write operations, so only to take care of WAR and WAW hazards in
        // this case

        rhi::MemoryState &after_write_state =
            texture_after_write_hazard_src_states[ptex_id];
        // If this is a WAR hazard, must wait for all previous reads to finish
        // and make the layout transition's memory available. Also reset the
        // source stage mask that the next WAR barrier will use.
        rhi::PipelineStageMask src_stage_mask =
            std::exchange(texture_after_read_hazard_src_states[ptex_id], {});
        rhi::AccessMask src_access_mask;
        if (!src_stage_mask and def_pass and def_pass->queue == pass.queue) {
          // If there were no reads between this write and the previous one,
          // need to wait for the previous write to finish and make it's memory
          // available and the layout transition's memory visible.
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // hazards will use if they are on the same queue.
        after_write_state.stage_mask = dst_stage_mask;
        after_write_state.access_mask =
            dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;

#if 0
#if REN_RG_DEBUG
        auto get_layout_name = [](rhi::ImageLayout layout) {
          switch (layout) {
          case rhi::ImageLayout::Undefined:
            return "Undefined";
          case rhi::ImageLayout::ShaderResource:
            return "ShaderResource";
          case rhi::ImageLayout::UnorderedAccess:
            return "UnorderedAccess";
          case rhi::ImageLayout::RenderTarget:
            return "RenderTarget";
          case rhi::ImageLayout::DepthStencilRead:
            return "DepthStencilRead";
          case rhi::ImageLayout::DepthStencilWrite:
            return "DepthStencilWrite";
          case rhi::ImageLayout::TransferSrc:
            return "TransferSrc";
          case rhi::ImageLayout::TransferDst:
            return "TransferDst";
          case rhi::ImageLayout::Present:
            return "Present";
          }
        };

        fmt::println(stderr, "{}: transition {} from {} to {}",
                     m_rt_data->m_pass_names[rt_pass.pass],
                     m_rgp->m_textures[use.texture].name,
                     get_layout_name(ptex.layout), get_layout_name(dst_layout));
#endif
#endif

        m_texture_barriers.push_back({
            .resource = {ptex.handle},
            .src_stage_mask = src_stage_mask,
            .src_access_mask = src_access_mask,
            .src_layout = ptex.layout,
            .dst_stage_mask = dst_stage_mask,
            .dst_access_mask = dst_access_mask,
            .dst_layout = dst_layout,
        });

        ptex.layout = dst_layout;
      }
    };

    std::ranges::for_each(pass.read_textures, maybe_place_barrier_for_texture);
    std::ranges::for_each(pass.write_textures, maybe_place_barrier_for_texture);

    auto place_semaphore = [&](RgSemaphoreStateId id) {
      const RgSemaphoreState &state = m_data->m_semaphore_states[id];
      m_semaphore_submit_info.push_back({
          .semaphore = m_rgp->m_semaphores[state.semaphore].handle,
          .value = state.value,
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
}

auto RgBuilder::build(const RgBuildInfo &build_info)
    -> Result<RenderGraph, Error> {
  ZoneScoped;

  m_rgp->rotate_textures();

  ren_try_to(alloc_textures());
  alloc_buffers(*build_info.gfx_allocator, *build_info.async_allocator,
                *build_info.shared_allocator, *build_info.upload_allocator);

  add_inter_queue_semaphores();

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
  rg.m_upload_allocator = build_info.upload_allocator;
  rg.m_semaphores = &m_rgp->m_semaphores;

  return rg;
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
                                      const rhi::RenderTargetOperations &ops) {
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
    RgTextureToken texture, const rhi::DepthTargetOperations &ops) {
  m_builder->m_data->m_passes[m_pass]
      .ext.get_or_emplace<RgRenderPass>()
      .depth_stencil_target = RgDepthStencilTarget{
      .texture = texture,
      .ops = ops,
  };
}

auto RgPassBuilder::write_render_target(RgDebugName name, RgTextureId texture,
                                        const rhi::RenderTargetOperations &ops,
                                        u32 index)
    -> std::tuple<RgTextureId, RgTextureToken> {
  auto [new_texture, token] =
      write_texture(std::move(name), texture, rhi::RENDER_TARGET);
  add_render_target(index, token, ops);
  return {new_texture, token};
}

auto RgPassBuilder::read_depth_stencil_target(RgTextureId texture)
    -> RgTextureToken {
  RgTextureToken token = read_texture(texture, rhi::READ_DEPTH_STENCIL_TARGET);
  add_depth_stencil_target(token, {
                                      .load = rhi::RenderPassLoadOp::Load,
                                      .store = rhi::RenderPassStoreOp::None,
                                  });
  return token;
}

auto RgPassBuilder::write_depth_stencil_target(
    RgDebugName name, RgTextureId texture,
    const rhi::DepthTargetOperations &ops)
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

auto RenderGraph::execute(const RgExecuteInfo &exec_info)
    -> Result<void, Error> {
  ZoneScoped;

  RgRuntime rg;
  rg.m_rg = this;

  for (rhi::QueueFamily queue_family :
       {rhi::QueueFamily::Graphics, rhi::QueueFamily::Compute}) {
    ZoneScopedN("RenderGraph::submit_queue");

    CommandRecorder cmd;
    Span<const SemaphoreState> batch_wait_semaphores;
    Span<const SemaphoreState> batch_signal_semaphores;

    auto submit_batch = [&]() -> Result<void, Error> {
      if (!cmd and batch_wait_semaphores.empty() and
          batch_signal_semaphores.empty()) {
        return {};
      }
      ren_assert(cmd);
      ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());
      ren_try_to(m_renderer->submit(queue_family, {cmd_buffer},
                                    batch_wait_semaphores,
                                    batch_signal_semaphores));
      batch_wait_semaphores = {};
      batch_signal_semaphores = {};
      return {};
    };

    Span<const RgRtPass> passes = m_data->m_gfx_passes;
    Handle<CommandPool> cmd_pool = exec_info.gfx_cmd_pool;
    rg.m_event_pool = exec_info.gfx_event_pool;
    if (queue_family == rhi::QueueFamily::Compute) {
      passes = m_data->m_async_passes;
      cmd_pool = exec_info.async_cmd_pool;
      rg.m_event_pool = exec_info.async_event_pool;
    }

    for (const RgRtPass &pass : passes) {
      ZoneScopedN("RenderGraph::execute_pass");
#if REN_RG_DEBUG
      StringView pass_name = m_data->m_pass_names[pass.pass];
      ZoneText(pass_name.data(), pass_name.size());
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
          auto texture_barriers = Span(m_data->m_texture_barriers)
                                      .subspan(pass.base_texture_barrier,
                                               pass.num_texture_barriers);
          cmd.pipeline_barrier(memory_barriers, texture_barriers);
        }

        pass.ext.visit(OverloadSet{
            [&](const RgRtRenderPass &pass) {
              glm::uvec2 viewport = {-1, -1};

              StaticVector<RenderTarget, rhi::MAX_NUM_RENDER_TARGETS>
                  render_targets(pass.num_render_targets);
              for (usize i : range(pass.num_render_targets)) {
                const RgRenderTarget &rt =
                    m_data->m_render_targets[pass.base_render_target + i];
                if (!rt.texture) {
                  continue;
                }
                Handle<Texture> texture = rg.get_texture(rt.texture);
                viewport = m_renderer->get_texture(texture).size,
                render_targets[i] = {.rtv = {texture}, .ops = rt.ops};
              }

              DepthStencilTarget depth_stencil_target;
              if (pass.depth_stencil_target) {
                const RgDepthStencilTarget &dst =
                    m_data->m_depth_stencil_targets[*pass.depth_stencil_target];
                Handle<Texture> texture = rg.get_texture(dst.texture);
                viewport = m_renderer->get_texture(texture).size,
                depth_stencil_target = {.dsv = {texture}, .ops = dst.ops};
              }

              RenderPass render_pass = cmd.render_pass({
                  .render_targets = render_targets,
                  .depth_stencil_target = depth_stencil_target,
              });
              render_pass.set_viewports({{.size = viewport}});
              render_pass.set_scissor_rects({{.size = viewport}});

              pass.cb(*m_renderer, rg, render_pass);
            },
            [&](const RgCallback &cb) { cb(*m_renderer, rg, cmd); },
        });
      }

      if (pass.num_signal_semaphores > 0) {
        batch_signal_semaphores = Span(m_data->m_semaphore_submit_info)
                                      .subspan(pass.base_signal_semaphore,
                                               pass.num_signal_semaphores);
        ren_try_to(submit_batch());
      }
    }

    ren_try_to(submit_batch());
  }

  for (RgTextureId texture : m_rgp->m_frame_textures) {
    m_rgp->m_textures.erase(texture);
  }
  m_rgp->m_frame_textures.clear();

  if (exec_info.frame_end_semaphore) {
    if (m_rgp->m_async_compute) {
      *exec_info.frame_end_semaphore = m_rgp->m_async_semaphore;
      *exec_info.frame_end_time = m_rgp->m_async_time;
    } else {
      *exec_info.frame_end_semaphore = m_rgp->m_gfx_semaphore;
      *exec_info.frame_end_time = m_rgp->m_gfx_time;
    }
  }

  return {};
}

auto RgRuntime::get_untyped_buffer(RgUntypedBufferToken buffer) const
    -> const BufferView & {
  ren_assert(buffer);
  return m_rg->m_data->m_buffers[buffer];
}

auto RgRuntime::get_texture(RgTextureToken texture) const -> Handle<Texture> {
  ren_assert(texture);
  return m_rg->m_data->m_textures[texture];
}

auto RgRuntime::get_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  ren_assert(texture);
  sh::Handle<void> descriptor =
      m_rg->m_data->m_texture_descriptors[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  if (!texture) {
    return {};
  }
  sh::Handle<void> descriptor =
      m_rg->m_data->m_texture_descriptors[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_sampled_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  ren_assert(texture);
  sh::Handle<void> descriptor =
      m_rg->m_data->m_texture_descriptors[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_sampled_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  if (!texture) {
    return {};
  }
  sh::Handle<void> descriptor =
      m_rg->m_data->m_texture_descriptors[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureToken texture,
                                               u32 mip) const
    -> sh::Handle<void> {
  ren_assert(texture);
  ren_assert(m_rg->m_data->m_texture_descriptors[texture].storage);
  const RgTextureDescriptors &descriptors =
      m_rg->m_data->m_texture_descriptors[texture];
  ren_assert(mip < descriptors.num_mips);
  sh::Handle<void> descriptor(descriptors.storage[mip],
                              sh::DescriptorKind::RWTexture);
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_storage_texture_descriptor(RgTextureToken texture,
                                                   u32 mip) const
    -> sh::Handle<void> {
  if (!texture) {
    return {};
  }
  ren_assert(m_rg->m_data->m_texture_descriptors[texture].storage);
  const RgTextureDescriptors &descriptors =
      m_rg->m_data->m_texture_descriptors[texture];
  if (mip >= descriptors.num_mips) {
    return {};
  }
  sh::Handle<void> descriptor(descriptors.storage[mip],
                              sh::DescriptorKind::RWTexture);
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
