#include "RenderGraph.hpp"
#include "CommandRecorder.hpp"
#include "core/Errors.hpp"
#include "core/Views.hpp"
#include "ren/core/Format.hpp"

#include <algorithm>
#include <tracy/Tracy.hpp>

namespace ren {

auto RgPersistent::create_texture(const RgTextureCreateInfo &create_info)
    -> RgTextureId {
  ScratchArena scratch;

  ren_assert(create_info.name.m_size > 0);

  String8 name = create_info.name;
  if (!create_info.persistent) {
    name = format(scratch, "rg#{}", name);
  }

  RgPhysicalTextureId id(m_physical_textures.m_size);
  m_physical_textures.push(
      m_arena,
      {
          .name = create_info.name.copy(m_arena),
          .format = create_info.format,
          .size = {create_info.width, create_info.height, create_info.depth},
          .cube_map = create_info.cube_map,
          .persistent = create_info.persistent,
          .num_mips = create_info.num_mips,
          .num_layers = create_info.num_layers,
          .id = m_textures.insert(m_arena,
                                  {
                                      .name = name.copy(m_arena),
                                      .parent = id,
                                  }),
      });

  return m_physical_textures[id].id;
}

auto RgPersistent::create_texture(String8 name) -> RgTextureId {
  ren_assert(name.m_size > 0);

  ScratchArena scratch;

  RgPhysicalTextureId id(m_physical_textures.m_size);
  m_physical_textures.push(
      m_arena, {
                   .name = name.copy(m_arena),
                   .external = true,
                   .id = m_textures.insert(
                       m_arena,
                       {
                           .name = format(scratch, "rg#{}", name).copy(m_arena),
                           .parent = id,
                       }),
               });

  return m_physical_textures[id].id;
}

auto RgPersistent::create_semaphore(String8 name) -> RgSemaphoreId {
  RgSemaphoreId semaphore = m_semaphores.insert(m_arena);
  m_semaphores[semaphore].name = name.copy(m_arena);
  return semaphore;
}

RgPersistent RgPersistent::init(NotNull<Arena *> arena,
                                NotNull<Renderer *> renderer) {
  return {
      .m_arena = arena,
      .m_rcs_arena = ResourceArena::init(arena, renderer),
      .m_textures = GenArray<RgTexture>::init(arena),
      .m_semaphores = GenArray<RgSemaphore>::init(arena),
  };
}

void RgPersistent::destroy() { m_rcs_arena.clear(); }

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

void RgBuilder::init(NotNull<Arena *> arena, NotNull<RgPersistent *> rgp,
                     NotNull<Renderer *> renderer,
                     NotNull<DescriptorAllocatorScope *> descriptor_allocator) {
  m_arena = arena;
  m_passes = GenArray<RgPass>::init(arena);
  m_buffers = GenArray<RgBuffer>::init(arena);
  m_renderer = renderer;
  m_rgp = rgp;
  m_descriptor_allocator = descriptor_allocator;

  for (auto &&[_, texture] : m_rgp->m_textures) {
    texture.def = {};
    texture.kill = {};
    texture.child = {};
  }
}

auto RgBuilder::create_pass(const RgPassCreateInfo &create_info)
    -> RgPassBuilder {
  ren_assert(create_info.name.m_size > 0);
  RgQueue queue = create_info.queue;
  if (!m_rgp->m_async_compute) {
    queue = RgQueue::Graphics;
  }
  RgPassId pass_id =
      m_passes.insert(m_arena, RgPass{
                                   .name = create_info.name.copy(m_arena),
                                   .queue = queue,
                               });
  if (queue == RgQueue::Async) {
    m_async_schedule.push(m_arena, pass_id);
  } else {
    m_gfx_schedule.push(m_arena, pass_id);
  }
  return RgPassBuilder(pass_id, *this);
}

auto RgBuilder::add_buffer_use(const RgBufferUse &use) -> RgBufferUseId {
  ren_assert(use.buffer);
  RgBufferUseId id(m_buffer_uses.m_size);
  m_buffer_uses.push(m_arena, use);
  return id;
};

auto RgBuilder::create_virtual_buffer(RgPassId pass, String8 name,
                                      RgUntypedBufferId parent)
    -> RgUntypedBufferId {
  RgPhysicalBufferId physical_buffer;
  if (parent) {
    physical_buffer = m_buffers[parent].parent;
  } else {
    ren_assert(!pass);
    physical_buffer = RgPhysicalBufferId(m_physical_buffers.m_size);
    m_physical_buffers.push(m_arena);
  }

  RgUntypedBufferId buffer =
      m_buffers.insert(m_arena, {
                                    .name = name.copy(m_arena),
                                    .parent = physical_buffer,
                                    .def = pass,
                                });

  if (parent) {
    ren_assert(pass);
    m_buffers[parent].kill = pass;
  }

  if (parent) {
    ren_assert_msg(!m_buffers[parent].child,
                   "Render graph buffers can only be written once");
    m_buffers[parent].child = buffer;
  }

  return buffer;
}

auto RgBuilder::create_buffer(String8 name, rhi::MemoryHeap heap, usize size)
    -> RgUntypedBufferId {
  ren_assert(size > 0);
  RgUntypedBufferId buffer =
      create_virtual_buffer(NullHandle, name, NullHandle);
  RgPhysicalBufferId physical_buffer = m_buffers[buffer].parent;
  m_physical_buffers[physical_buffer] = {
      .heap = heap,
      .size = size,
  };
  return buffer;
}

auto RgBuilder::read_buffer(RgPassId pass_id, RgUntypedBufferId buffer,
                            const rhi::BufferState &usage, u32 offset)
    -> RgUntypedBufferToken {
  ren_assert(buffer);
  RgPass &pass = m_passes[pass_id];
  RgBufferUseId use = add_buffer_use({
      .buffer = buffer,
      .offset = offset,
      .usage = usage,
  });
  pass.read_buffers.push(m_arena, use);
  return RgUntypedBufferToken(use);
}

auto RgBuilder::write_buffer(RgPassId pass_id, String8 name,
                             RgUntypedBufferId src,
                             const rhi::BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  ScratchArena scratch(m_arena);
  ren_assert(src);
  RgBuffer &src_buffer = m_buffers[src];
  ren_assert(src_buffer.def != pass_id);
  RgPass &pass = m_passes[pass_id];
  m_physical_buffers[src_buffer.parent].queues |= pass.queue;
  RgBufferUseId use = add_buffer_use({
      .buffer = src,
      .usage = usage,
  });
  pass.write_buffers.push(m_arena, use);
  if (src_buffer.name == "rg#") {
    ren_assert(name.m_size > 0);
    src_buffer.name = format(scratch, "rg#{}", name);
  }
  RgUntypedBufferId dst = create_virtual_buffer(pass_id, name, src);
  return {dst, RgUntypedBufferToken(use)};
}

void RgBuilder::clear_texture(String8 name, NotNull<RgTextureId *> texture,
                              const glm::vec4 &color, RgQueue queue) {
  auto pass = create_pass({.name = "clear-texture", .queue = queue});
  auto token = pass.write_texture(name, texture, rhi::TRANSFER_DST_IMAGE);
  pass.set_callback(
      [token, color](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
        cmd.clear_texture(rg.get_texture(token), color);
      });
}

void RgBuilder::copy_texture_to_buffer(RgTextureId src, String8 name,
                                       RgUntypedBufferId *dst, RgQueue queue) {
  auto pass = create_pass({.name = "copy-texture-to-buffer", .queue = queue});
  auto src_token = pass.read_texture(src, rhi::TRANSFER_SRC_IMAGE);
  auto dst_token = pass.write_buffer(name, dst, rhi::TRANSFER_DST_BUFFER);
  pass.set_callback([src_token, dst_token](Renderer &, const RgRuntime &rg,
                                           CommandRecorder &cmd) {
    cmd.copy_texture_to_buffer(rg.get_texture(src_token),
                               rg.get_buffer(dst_token));
  });
}

auto RgBuilder::add_texture_use(const RgTextureUse &use) -> RgTextureUseId {
  ren_assert(use.texture);
  RgTextureUseId id(m_texture_uses.m_size);
  m_texture_uses.push(m_arena, use);
  return id;
}

auto RgBuilder::create_virtual_texture(RgPassId pass, String8 name,
                                       RgTextureId parent) -> RgTextureId {
  ren_assert(pass);
  ren_assert(parent);
  RgPhysicalTextureId physical_texture = m_rgp->m_textures[parent].parent;
  RgTextureId texture =
      m_rgp->m_textures.insert(m_rgp->m_arena, {
                                                   .name = name.copy(m_arena),
                                                   .parent = physical_texture,
                                                   .def = pass,
                                               });
  m_frame_textures.push(m_arena, texture);
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
  RgPass &pass = m_passes[pass_id];
  for (RgTextureUseId use_id : pass.read_textures) {
    RgTextureUse &use = m_texture_uses[use_id];
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
  pass.read_textures.push(m_arena, use);
  return RgTextureToken(use);
}

auto RgBuilder::write_texture(RgTextureWriteInfo &&info) -> RgTextureToken {
  RgTextureId src = *info.texture;
  ren_assert(src);
  const RgTexture &tex = m_rgp->m_textures[src];
  const RgPhysicalTexture &ptex = m_rgp->m_physical_textures[tex.parent];
  ren_assert(info.base_mip < ptex.num_mips);
  ren_assert(tex.def != info.pass);
  if (info.sampler) {
    ren_assert_msg(info.usage.access_mask.is_set(rhi::Access::ShaderImageRead),
                   "Sampler must be null if texture is not sampled");
  }
  RgTextureUseId use = add_texture_use({
      .texture = src,
      .sampler = info.sampler,
      .state = info.usage,
      .base_mip = info.base_mip,
  });
  m_passes[info.pass].write_textures.push(m_arena, use);
  *info.texture = create_virtual_texture(info.pass, info.name, src);
  return RgTextureToken(use);
}

void RgBuilder::set_external_buffer(RgUntypedBufferId id,
                                    const BufferView &view) {
  RgPhysicalBufferId physical_buffer_id = m_buffers[id].parent;
  RgPhysicalBuffer &physical_buffer = m_physical_buffers[physical_buffer_id];
  ren_assert(!physical_buffer.view.buffer);
  physical_buffer.view = view;
}

void RgBuilder::set_external_texture(RgTextureId id, Handle<Texture> handle) {
  const Texture &texture = m_renderer->get_texture(handle);
  RgPhysicalTextureId ptex_id = m_rgp->m_textures[id].parent;
  RgPhysicalTexture &ptex = m_rgp->m_physical_textures[ptex_id];
  ren_assert(ptex.external);
  ptex = {
      .name = ptex.name,
      .format = texture.format,
      .usage = texture.usage,
      .size = texture.size,
      .cube_map = texture.cube_map,
      .persistent = false,
      .external = true,
      .num_mips = texture.num_mips,
      .num_layers = texture.num_layers,
      .handle = handle,
      .id = ptex.id,
      .last_queue = RgQueue::None,
      .queue = RgQueue::None,
      .last_time = 0,
      .time = 0,
  };
}

auto RgBuilder::add_semaphore_state(RgSemaphoreId semaphore, u64 value)
    -> RgSemaphoreStateId {
  RgSemaphoreStateId id(m_semaphore_states.m_size);
  m_semaphore_states.push(m_arena, {
                                       .semaphore = semaphore,
                                       .value = value,
                                   });
  return id;
}

void RgBuilder::wait_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                               u64 value) {
  m_passes[pass].wait_semaphores.push(m_arena,
                                      add_semaphore_state(semaphore, value));
}

void RgBuilder::signal_semaphore(RgPassId pass, RgSemaphoreId semaphore,
                                 u64 value) {
  m_passes[pass].signal_semaphores.push(m_arena,
                                        add_semaphore_state(semaphore, value));
}

void RgBuilder::set_external_semaphore(RgSemaphoreId semaphore,
                                       Handle<Semaphore> handle) {
  ren_assert(handle);
  m_rgp->m_semaphores[semaphore].handle = handle;
}

void RgBuilder::dump_pass_schedule() const {
  ScratchArena scratch(m_arena);

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {

    Span<const RgPassId> schedule = m_gfx_schedule;
    if (queue == RgQueue::Graphics) {
      fmt::println(stderr, "Graphics queue passes:");
    } else {
      schedule = m_async_schedule;
      if (schedule.empty()) {
        continue;
      }
      fmt::println(stderr, "Async compute queue passes:");
    }

    for (RgPassId pass_id : schedule) {
      const RgPass &pass = m_passes[pass_id];

      fmt::println(stderr, "  * {}", pass.name);

      DynamicArray<RgUntypedBufferId> create_buffers;
      DynamicArray<RgUntypedBufferId> write_buffers;
      for (RgBufferUseId use : pass.write_buffers) {
        RgUntypedBufferId id = m_buffer_uses[use].buffer;
        const RgBuffer &buffer = m_buffers[id];
        if (buffer.name.m_size == 0) {
          create_buffers.push(scratch, buffer.child);
        } else {
          write_buffers.push(scratch, id);
        }
      }

      if (create_buffers.m_size > 0) {
        fmt::println(stderr, "    Creates buffers:");
        for (RgUntypedBufferId buffer : create_buffers) {
          fmt::println(stderr, "      - {}", m_buffers[buffer].name);
        }
      }
      if (pass.read_buffers.m_size > 0) {
        fmt::println(stderr, "    Reads buffers:");
        for (RgBufferUseId use : pass.read_buffers) {
          fmt::println(stderr, "      - {}",
                       m_buffers[m_buffer_uses[use].buffer].name);
        }
      }
      if (write_buffers.m_size > 0) {
        fmt::println(stderr, "    Writes buffers:");
        for (RgUntypedBufferId src_id : write_buffers) {
          const RgBuffer &src = m_buffers[src_id];
          const RgBuffer &dst = m_buffers[src.child];
          fmt::println(stderr, "      - {} -> {}", src.name, dst.name);
        }
      }

      const auto &textures = m_rgp->m_textures;

      DynamicArray<RgTextureId> create_textures;
      DynamicArray<RgTextureId> write_textures;
      for (RgTextureUseId use : pass.write_textures) {
        RgTextureId id = m_texture_uses[use].texture;
        const RgTexture &texture = textures[id];
        ren_assert(texture.name.m_size > 0);
        if (texture.name.starts_with("rg#")) {
          create_textures.push(scratch, texture.child);
        } else {
          write_textures.push(scratch, id);
        }
      }

      if (create_textures.m_size > 0) {
        fmt::println(stderr, "    Creates textures:");
        for (RgTextureId texture : create_textures) {
          fmt::println(stderr, "      - {}", textures[texture].name);
        }
      }
      if (pass.read_textures.m_size > 0) {
        fmt::println(stderr, "    Reads textures:");
        for (RgTextureUseId use : pass.read_textures) {
          fmt::println(stderr, "      - {}",
                       textures[m_texture_uses[use].texture].name);
        }
      }
      if (write_textures.m_size > 0) {
        fmt::println(stderr, "    Writes textures:");
        for (RgTextureId src_id : write_textures) {
          const RgTexture &src = textures[src_id];
          const RgTexture &dst = textures[src.child];
          fmt::println(stderr, "      - {} -> {}", src.name, dst.name);
        }
      }

      const auto &semaphores = m_rgp->m_semaphores;

      if (pass.wait_semaphores.m_size > 0) {
        fmt::println(stderr, "    Waits for semaphores:");
        for (RgSemaphoreStateId state_id : pass.wait_semaphores) {
          const RgSemaphoreState &state = m_semaphore_states[state_id];
          if (state.value) {
            fmt::println(stderr, "      - {}, {}",
                         semaphores[state.semaphore].name, state.value);
          } else {
            fmt::println(stderr, "      - {}",
                         semaphores[state.semaphore].name);
          }
        }
      }

      if (pass.signal_semaphores.m_size > 0) {
        fmt::println(stderr, "    Signals semaphores:");
        for (RgSemaphoreStateId state_id : pass.signal_semaphores) {
          const RgSemaphoreState &state = m_semaphore_states[state_id];
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
}

auto RgBuilder::alloc_textures() -> Result<void, Error> {
  bool need_alloc = false;
  auto update_texture_usage_flags = [&](RgTextureUseId use_id) {
    const RgTextureUse &use = m_texture_uses[use_id];
    const RgTexture &texture = m_rgp->m_textures[use.texture];
    RgPhysicalTextureId ptex_id = texture.parent;
    RgPhysicalTexture &ptex = m_rgp->m_physical_textures[ptex_id];
    rhi::ImageUsageFlags usage = get_texture_usage_flags(use.state.access_mask);
    if (ptex.external) {
      ren_assert((ptex.usage & usage) == usage);
    } else {
      bool needs_usage_update = (ptex.usage | usage) != ptex.usage;
      ptex.usage |= usage;
      if (!ptex.handle or needs_usage_update) {
        need_alloc = true;
      }
    }
  };
  for (const auto &[_, pass] : m_passes) {
    std::ranges::for_each(pass.read_textures, update_texture_usage_flags);
    std::ranges::for_each(pass.write_textures, update_texture_usage_flags);
  }

  if (not need_alloc) {
    for (RgPhysicalTexture &ptex : m_rgp->m_physical_textures) {
      ptex.layout = ptex.persistent ? ptex.layout : rhi::ImageLayout::Undefined;
    }
    return {};
  }

  usize num_gfx_passes = m_gfx_schedule.m_size;

  m_renderer->wait_idle();
  m_rgp->m_rcs_arena.clear();
  for (RgPhysicalTexture &ptex : m_rgp->m_physical_textures) {
    // Skip unused temporal or external textures.
    if (!ptex.usage or ptex.external) {
      continue;
    }
    ren_try(ptex.handle, m_rgp->m_rcs_arena.create_texture({
                             .name = ptex.name,
                             .format = ptex.format,
                             .usage = ptex.usage,
                             .width = ptex.size.x,
                             .height = ptex.size.y,
                             .depth = ptex.size.z,
                             .cube_map = ptex.cube_map,
                             .num_mips = ptex.num_mips,
                             .num_layers = ptex.num_layers,
                         }));
    ptex.layout = rhi::ImageLayout::Undefined;
  }

  ren_try(m_rgp->m_gfx_semaphore,
          m_rgp->m_rcs_arena.create_semaphore(

              {
                  .name = "Render graph graphics queue timeline",
                  .type = rhi::SemaphoreType::Timeline,
                  .initial_value = m_rgp->m_gfx_time,
              }));
  ren_try(m_rgp->m_async_semaphore,
          m_rgp->m_rcs_arena.create_semaphore({
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
  if (m_gfx_schedule.m_size != num_gfx_passes) {
    std::ranges::rotate(m_gfx_schedule,
                        m_gfx_schedule.begin() + num_gfx_passes);
  }

  return {};
}

void RgBuilder::alloc_buffers(DeviceBumpAllocator &gfx_allocator,
                              DeviceBumpAllocator &async_allocator,
                              DeviceBumpAllocator &shared_allocator,
                              UploadBumpAllocator &upload_allocator) {
  for (auto i : range(m_physical_buffers.m_size)) {
    RgPhysicalBufferId id(i);
    RgPhysicalBuffer &physical_buffer = m_physical_buffers[id];
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
    ren_assert(m_async_schedule.m_size == 0);
  }

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    auto *rt_passes = &m_rg.m_gfx_passes;
    Span<const RgPassId> schedule = m_gfx_schedule;
    if (queue == RgQueue::Async) {
      rt_passes = &m_rg.m_async_passes;
      schedule = m_async_schedule;
    }
    *rt_passes = Span<RgRtPass>::allocate(m_arena, schedule.size());
    for (usize i : range(schedule.size())) {
      const RgPass &pass = m_passes[schedule[i]];
      (*rt_passes)[i] = {
          .name = pass.name,
          .rp_cb = pass.rp_cb,
          .cb = pass.cb,
          .render_targets = Span(pass.render_targets, pass.num_render_targets),
          .depth_stencil_target = pass.depth_stencil_target,
      };
    }
  }
}

void RgBuilder::add_inter_queue_semaphores() {
  usize new_gfx_time = m_rgp->m_gfx_time;
  usize new_async_time = m_rgp->m_async_time;

  for (RgPassId pass_id : m_gfx_schedule) {
    RgPass &pass = m_passes[pass_id];
    pass.signal_time = ++new_gfx_time;
  }

  for (RgPassId pass_id : m_async_schedule) {
    RgPass &pass = m_passes[pass_id];
    pass.signal_time = ++new_async_time;
  }

  // Compute dependencies and update last-used info.
  for (auto schedule : {m_gfx_schedule, m_async_schedule}) {
    for (RgPassId pass_id : schedule) {
      RgPass &pass = m_passes[pass_id];

      for (RgBufferUseId use : pass.read_buffers) {
        const RgBuffer &buffer = m_buffers[m_buffer_uses[use].buffer];
        if (buffer.def) {
          const RgPass &def = m_passes[buffer.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        }
        if (buffer.kill) {
          RgPass &kill = m_passes[buffer.kill];
          if (kill.queue != pass.queue) {
            kill.wait_time = std::max(kill.wait_time, pass.signal_time);
          }
        }
      }

      for (RgBufferUseId use : pass.write_buffers) {
        const RgBuffer &buffer = m_buffers[m_buffer_uses[use].buffer];
        if (buffer.def) {
          const RgPass &def = m_passes[buffer.def];
          if (def.queue != pass.queue) {
            pass.wait_time = std::max(pass.wait_time, def.signal_time);
          }
        }
      }

      for (RgTextureUseId use : pass.read_textures) {
        const RgTexture &texture =
            m_rgp->m_textures[m_texture_uses[use].texture];

        if (texture.def) {
          const RgPass &def = m_passes[texture.def];
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
          RgPass &kill = m_passes[texture.kill];
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
            m_rgp->m_textures[m_texture_uses[use].texture];
        if (texture.def) {
          const RgPass &def = m_passes[texture.def];
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
    Span<const RgPassId> schedule = m_gfx_schedule;
    Span<const RgPassId> other_schedule = m_async_schedule;
    usize first_signaled_time = m_rgp->m_async_time + 1;
    if (queue == RgQueue::Async) {
      schedule = m_async_schedule;
      other_schedule = m_gfx_schedule;
      first_signaled_time = m_rgp->m_gfx_time + 1;
    }
    usize last_waited_time = 0;

    for (RgPassId pass_id : schedule) {
      RgPass &pass = m_passes[pass_id];

      for (RgTextureUseId use : pass.write_textures) {
        const RgTexture &src_texture =
            m_rgp->m_textures[m_texture_uses[use].texture];
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
        RgPass &signal_pass = m_passes[signal_pass_id];
        ren_assert(pass.wait_time == signal_pass.signal_time);
        signal_pass.signal = true;
      }
    }
    if (not schedule.empty()) {
      m_passes[schedule.back()].signal = true;
    }
  }

  for (RgQueue queue : {RgQueue::Graphics, RgQueue::Async}) {
    Span<const RgPassId> schedule = m_gfx_schedule;
    RgSemaphoreId semaphore = m_rgp->m_gfx_semaphore_id;
    RgSemaphoreId other_semaphore = m_rgp->m_async_semaphore_id;
    if (queue == RgQueue::Async) {
      schedule = m_async_schedule;
      semaphore = m_rgp->m_async_semaphore_id;
      other_semaphore = m_rgp->m_gfx_semaphore_id;
    }
    for (RgPassId pass_id : std::views::reverse(schedule)) {
      RgPass &pass = m_passes[pass_id];
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
  m_rg.m_buffers = Span<BufferView>::allocate(m_arena, m_buffer_uses.m_size);
  for (auto i : range(m_buffer_uses.m_size)) {
    RgPhysicalBufferId physical_buffer_id =
        m_buffers[m_buffer_uses[i].buffer].parent;
    m_rg.m_buffers[i] = m_physical_buffers[physical_buffer_id].view.slice(
        m_buffer_uses[i].offset);
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

void RgBuilder::init_runtime_textures() {
  m_rg.m_textures = Span<RgRtTexture>::allocate(m_arena, m_texture_uses.m_size);
  for (auto i : range(m_texture_uses.m_size)) {
    const RgTextureUse &use = m_texture_uses[i];
    RgPhysicalTextureId physical_texture_id =
        m_rgp->m_textures[use.texture].parent;
    const RgPhysicalTexture &physical_texture =
        m_rgp->m_physical_textures[physical_texture_id];

    RgRtTexture texture = {physical_texture.handle};

    if (use.state.access_mask.is_set(rhi::Access::ShaderImageRead)) {
      SrvDesc srv = {
          .texture = physical_texture.handle,
          .dimension = get_view_dimension(physical_texture),
      };
      if (use.sampler) {
        texture.combined = m_descriptor_allocator->allocate_sampled_texture(
            *m_renderer, srv, use.sampler);
      } else {
        texture.sampled =
            m_descriptor_allocator->allocate_texture(*m_renderer, srv);
      }
    }
    if (use.state.access_mask.is_set(rhi::Access::UnorderedAccess)) {
      texture.num_mips = physical_texture.num_mips - use.base_mip;
      texture.storage = m_arena->allocate<u32>(texture.num_mips);
      for (u32 mip : range(texture.num_mips)) {
        texture.storage[mip] =
            m_descriptor_allocator
                ->allocate_storage_texture(
                    *m_renderer,
                    {
                        .texture = physical_texture.handle,
                        .dimension = get_view_dimension(physical_texture),
                        .mip = use.base_mip + mip,
                    })
                .m_id;
      }
    }

    m_rg.m_textures[i] = texture;
  }
}

void RgBuilder::place_barriers_and_semaphores() {
  ScratchArena scratch(m_arena);

  auto *buffer_after_write_hazard_src_states =
      scratch->allocate<rhi::BufferState>(m_physical_buffers.m_size);
  auto *buffer_after_read_hazard_src_states =
      scratch->allocate<rhi::PipelineStageMask>(m_physical_buffers.m_size);

  auto *texture_after_write_hazard_src_states =
      scratch->allocate<rhi::MemoryState>(m_rgp->m_physical_textures.m_size);
  auto *texture_after_read_hazard_src_states =
      scratch->allocate<rhi::PipelineStageMask>(
          m_rgp->m_physical_textures.m_size);

  usize gfx_i = 0;
  usize async_i = 0;

  DynamicArray<rhi::MemoryBarrier> memory_barriers;
  DynamicArray<TextureBarrier> texture_barriers;

  while (gfx_i < m_rg.m_gfx_passes.size() or
         async_i < m_rg.m_async_passes.size()) {
    RgRtPass *rt_pass = nullptr;
    const RgPass *pass = nullptr;

    if (gfx_i == m_rg.m_gfx_passes.size()) {
      rt_pass = &m_rg.m_async_passes[async_i];
      pass = &m_passes[m_async_schedule[async_i]];
      async_i++;
    } else if (async_i == m_rg.m_async_passes.size()) {
      rt_pass = &m_rg.m_gfx_passes[gfx_i];
      pass = &m_passes[m_gfx_schedule[gfx_i]];
      gfx_i++;
    } else {
      const RgPass &gfx_pass = m_passes[m_gfx_schedule[gfx_i]];
      const RgPass &async_pass = m_passes[m_async_schedule[async_i]];
      if (gfx_pass.wait_time < async_pass.signal_time) {
        rt_pass = &m_rg.m_gfx_passes[gfx_i];
        pass = &gfx_pass;
        gfx_i++;
      } else {
        rt_pass = &m_rg.m_async_passes[async_i];
        pass = &async_pass;
        async_i++;
      }
    }

    auto maybe_place_barrier_for_buffer = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      const RgBuffer &buffer = m_buffers[use.buffer];
      const RgPass *def_pass = buffer.def ? &m_passes[buffer.def] : nullptr;
      const RgPass *kill_pass = buffer.kill ? &m_passes[buffer.kill] : nullptr;
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
        ren_assert(kill_pass == pass);
        rhi::BufferState &after_write_state =
            buffer_after_write_hazard_src_states[pbuf_id];
        // Reset the source stage mask that the next WAR hazard will use
        src_stage_mask =
            std::exchange(buffer_after_read_hazard_src_states[pbuf_id], {});
        // If this is a WAR hazard, need to wait for all previous reads on the
        // same queue to finish. The previous write's memory has already been
        // made available by previous RAW barriers or semaphore waits, so it
        // only needs to be made visible.
        if (!src_stage_mask and def_pass and def_pass->queue == pass->queue) {
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
        if (def_pass and def_pass->queue == pass->queue) {
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        if (kill_pass and kill_pass->queue == pass->queue) {
          // Update the source stage mask that the next WAR hazard will use if
          // it's on the same queue.
          buffer_after_read_hazard_src_states[pbuf_id] |= dst_stage_mask;
        }
      }

      if (!src_stage_mask) {
        ren_assert(!src_access_mask);
        return;
      }

      memory_barriers.push(m_arena, {
                                        .src_stage_mask = src_stage_mask,
                                        .src_access_mask = src_access_mask,
                                        .dst_stage_mask = dst_stage_mask,
                                        .dst_access_mask = dst_access_mask,
                                    });
    };

    auto maybe_place_barrier_for_texture = [&](RgTextureUseId use_id) {
      const RgTextureUse &use = m_texture_uses[use_id];
      const RgTexture &texture = m_rgp->m_textures[use.texture];
      const RgPass *def_pass = texture.def ? &m_passes[texture.def] : nullptr;
      const RgPass *kill_pass =
          texture.kill ? &m_passes[texture.kill] : nullptr;
      RgPhysicalTextureId ptex_id = m_rgp->m_textures[use.texture].parent;
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
          ren_assert(kill_pass == pass);
          rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[ptex_id];
          src_stage_mask =
              std::exchange(texture_after_read_hazard_src_states[ptex_id], {});
          if (!src_stage_mask and def_pass and def_pass->queue == pass->queue) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          after_write_state.stage_mask = dst_stage_mask;
          after_write_state.access_mask =
              dst_access_mask & rhi::WRITE_ONLY_ACCESS_MASK;
        } else {
          const rhi::MemoryState &after_write_state =
              texture_after_write_hazard_src_states[ptex_id];
          if (def_pass and def_pass->queue == pass->queue) {
            src_stage_mask = after_write_state.stage_mask;
            src_access_mask = after_write_state.access_mask;
          }
          if (kill_pass and kill_pass->queue == pass->queue) {
            texture_after_read_hazard_src_states[ptex_id] |= dst_stage_mask;
          }
        }

        if (!src_stage_mask) {
          ren_assert(!src_access_mask);
          return;
        }

        texture_barriers.push(m_arena, {
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
        if (!src_stage_mask and def_pass and def_pass->queue == pass->queue) {
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
        auto get_layout_name = [](rhi::ImageLayout layout) {
          switch (layout) {
          case rhi::ImageLayout::Undefined:
            return "Undefined";
          case rhi::ImageLayout::General:
            return "General";
          case rhi::ImageLayout::RenderTarget:
            return "RenderTarget";
          case rhi::ImageLayout::TransferSrc:
            return "TransferSrc";
          case rhi::ImageLayout::TransferDst:
            return "TransferDst";
          case rhi::ImageLayout::Present:
            return "Present";
          }
        };

        fmt::println(stderr, "{}: transition {} from {} to {}", rt_pass->name,
                     m_rgp->m_textures[use.texture].name,
                     get_layout_name(ptex.layout), get_layout_name(dst_layout));
#endif

        texture_barriers.push(m_arena, {
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

    memory_barriers.clear();
    texture_barriers.clear();
    std::ranges::for_each(pass->read_buffers, maybe_place_barrier_for_buffer);
    std::ranges::for_each(pass->write_buffers, maybe_place_barrier_for_buffer);
    std::ranges::for_each(pass->read_textures, maybe_place_barrier_for_texture);
    std::ranges::for_each(pass->write_textures,
                          maybe_place_barrier_for_texture);

    rt_pass->memory_barriers =
        Span<rhi::MemoryBarrier>::allocate(m_arena, memory_barriers.m_size);
    std::ranges::copy(memory_barriers, rt_pass->memory_barriers.data());

    rt_pass->texture_barriers =
        Span<TextureBarrier>::allocate(m_arena, texture_barriers.m_size);
    std::ranges::copy(texture_barriers, rt_pass->texture_barriers.data());

    rt_pass->wait_semaphores =
        Span<SemaphoreState>::allocate(m_arena, pass->wait_semaphores.m_size);
    for (usize i : range(pass->wait_semaphores.m_size)) {
      RgSemaphoreStateId id = pass->wait_semaphores[i];
      const RgSemaphoreState &state = m_semaphore_states[id];
      rt_pass->wait_semaphores[i] = {
          .semaphore = m_rgp->m_semaphores[state.semaphore].handle,
          .value = state.value,
      };
    }
    rt_pass->signal_semaphores =
        Span<SemaphoreState>::allocate(m_arena, pass->signal_semaphores.m_size);
    for (usize i : range(pass->signal_semaphores.m_size)) {
      RgSemaphoreStateId id = pass->signal_semaphores[i];
      const RgSemaphoreState &state = m_semaphore_states[id];
      rt_pass->signal_semaphores[i] = {
          .semaphore = m_rgp->m_semaphores[state.semaphore].handle,
          .value = state.value,
      };
    }
  }
}

auto RgBuilder::build(const RgBuildInfo &build_info)
    -> Result<RenderGraph, Error> {
  ZoneScoped;

  ren_try_to(alloc_textures());
  alloc_buffers(*build_info.gfx_allocator, *build_info.async_allocator,
                *build_info.shared_allocator, *build_info.upload_allocator);

  add_inter_queue_semaphores();

#if 0
  dump_pass_schedule();
#endif

  init_runtime_passes();
  init_runtime_buffers();
  init_runtime_textures();

  place_barriers_and_semaphores();

  for (RgTextureId texture : m_frame_textures) {
    m_rgp->m_textures.erase(texture);
  }

  m_rg.m_renderer = m_renderer;
  m_rg.m_rgp = m_rgp;
  m_rg.m_upload_allocator = build_info.upload_allocator;

  return m_rg;
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

auto RgPassBuilder::write_buffer(String8 name, RgUntypedBufferId buffer,
                                 const rhi::BufferState &usage)
    -> std::tuple<RgUntypedBufferId, RgUntypedBufferToken> {
  return m_builder->write_buffer(m_pass, name, buffer, usage);
}

auto RgPassBuilder::write_texture(String8 name, RgTextureId texture,
                                  const rhi::ImageState &usage)
    -> RgTextureToken {
  return write_texture(name, &texture, usage);
}

auto RgPassBuilder::write_texture(String8 name, NotNull<RgTextureId *> texture,
                                  const rhi::ImageState &usage)
    -> RgTextureToken {
  return m_builder->write_texture({
      .name = name,
      .pass = m_pass,
      .texture = texture,
      .usage = usage,
  });
}

auto RgPassBuilder::write_texture(String8 name, NotNull<RgTextureId *> texture,
                                  const rhi::ImageState &usage,
                                  const rhi::SamplerCreateInfo &sampler,
                                  u32 base_mip) -> RgTextureToken {
  return m_builder->write_texture({
      .name = name,
      .pass = m_pass,
      .texture = texture,
      .usage = usage,
      .sampler = m_builder->m_renderer->get_sampler(sampler).value(),
      .base_mip = base_mip,
  });
}

void RgPassBuilder::add_render_target(u32 index, RgTextureToken texture,
                                      const rhi::RenderTargetOperations &ops) {
  RgPass &rp = m_builder->m_passes[m_pass];
  if (rp.num_render_targets <= index) {
    rp.num_render_targets = index + 1;
  }
  rp.render_targets[index] = {
      .texture = texture,
      .ops = ops,
  };
}

void RgPassBuilder::add_depth_stencil_target(
    RgTextureToken texture, const rhi::DepthTargetOperations &ops) {
  m_builder->m_passes[m_pass].depth_stencil_target = RgDepthStencilTarget{
      .texture = texture,
      .ops = ops,
  };
}

auto RgPassBuilder::write_render_target(String8 name,
                                        NotNull<RgTextureId *> texture,
                                        const rhi::RenderTargetOperations &ops,
                                        u32 index) -> RgTextureToken {
  RgTextureToken token = write_texture(name, texture, rhi::RENDER_TARGET);
  add_render_target(index, token, ops);
  return token;
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
    String8 name, NotNull<RgTextureId *> texture,
    const rhi::DepthTargetOperations &ops) -> RgTextureToken {
  RgTextureToken token =
      write_texture(name, texture, rhi::DEPTH_STENCIL_TARGET);
  add_depth_stencil_target(token, ops);
  return token;
}

void RgPassBuilder::wait_semaphore(RgSemaphoreId semaphore, u64 value) {
  m_builder->wait_semaphore(m_pass, semaphore, value);
}

void RgPassBuilder::signal_semaphore(RgSemaphoreId semaphore, u64 value) {
  m_builder->signal_semaphore(m_pass, semaphore, value);
}

auto execute(const RenderGraph &rg, const RgExecuteInfo &exec_info)
    -> Result<void, Error> {
  ZoneScoped;

  RgRuntime rt;
  rt.m_rg = &rg;

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
      ren_try_to(rg.m_renderer->submit(queue_family, {cmd_buffer},
                                       batch_wait_semaphores,
                                       batch_signal_semaphores));
      batch_wait_semaphores = {};
      batch_signal_semaphores = {};
      return {};
    };

    Span<const RgRtPass> passes = rg.m_gfx_passes;
    Handle<CommandPool> cmd_pool = exec_info.gfx_cmd_pool;
    if (queue_family == rhi::QueueFamily::Compute) {
      passes = rg.m_async_passes;
      cmd_pool = exec_info.async_cmd_pool;
    }

    for (const RgRtPass &pass : passes) {
      ZoneScopedN("RenderGraph::execute_pass");
      ZoneText(pass.name.m_str, pass.name.m_size);
      if (not pass.wait_semaphores.empty()) {
        ren_try_to(submit_batch());
        batch_wait_semaphores = pass.wait_semaphores;
      }

      if (!cmd) {
        ren_try_to(cmd.begin(*rg.m_renderer, cmd_pool));
      }

      {
        DebugRegion debug_region = cmd.debug_region(pass.name);

        if (not pass.memory_barriers.empty() or
            not pass.texture_barriers.empty()) {
          cmd.pipeline_barrier(pass.memory_barriers, pass.texture_barriers);
        }

        if (pass.rp_cb) {
          glm::uvec2 viewport = {-1, -1};

          RenderTarget render_targets[rhi::MAX_NUM_RENDER_TARGETS];
          for (usize i : range(pass.render_targets.size())) {
            const RgRenderTarget &rdt = pass.render_targets[i];
            if (!rdt.texture) {
              continue;
            }
            Handle<Texture> texture = rt.get_texture(rdt.texture);
            viewport = rg.m_renderer->get_texture(texture).size,
            render_targets[i] = {.rtv = {texture}, .ops = rdt.ops};
          }

          DepthStencilTarget depth_stencil_target;
          if (pass.depth_stencil_target.texture) {
            Handle<Texture> texture =
                rt.get_texture(pass.depth_stencil_target.texture);
            viewport = rg.m_renderer->get_texture(texture).size,
            depth_stencil_target = {
                .dsv = {texture},
                .ops = pass.depth_stencil_target.ops,
            };
          }

          RenderPass render_pass = cmd.render_pass({
              .render_targets =
                  Span(render_targets, pass.render_targets.size()),
              .depth_stencil_target = depth_stencil_target,
          });
          render_pass.set_viewports({{.size = viewport}});
          render_pass.set_scissor_rects({{.size = viewport}});

          pass.rp_cb(*rg.m_renderer, rt, render_pass);
        } else {
          pass.cb(*rg.m_renderer, rt, cmd);
        }
      }

      if (not pass.signal_semaphores.empty()) {
        batch_signal_semaphores = pass.signal_semaphores;
        ren_try_to(submit_batch());
      }
    }

    ren_try_to(submit_batch());
  }

  if (exec_info.frame_end_semaphore) {
    if (rg.m_rgp->m_async_compute) {
      *exec_info.frame_end_semaphore = rg.m_rgp->m_async_semaphore;
      *exec_info.frame_end_time = rg.m_rgp->m_async_time;
    } else {
      *exec_info.frame_end_semaphore = rg.m_rgp->m_gfx_semaphore;
      *exec_info.frame_end_time = rg.m_rgp->m_gfx_time;
    }
  }

  return {};
}

auto RgRuntime::get_untyped_buffer(RgUntypedBufferToken buffer) const
    -> const BufferView & {
  ren_assert(buffer);
  return m_rg->m_buffers[buffer];
}

auto RgRuntime::get_texture(RgTextureToken texture) const -> Handle<Texture> {
  ren_assert(texture);
  return m_rg->m_textures[texture].handle;
}

auto RgRuntime::get_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  ren_assert(texture);
  sh::Handle<void> descriptor = m_rg->m_textures[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  if (!texture) {
    return {};
  }
  sh::Handle<void> descriptor = m_rg->m_textures[texture].sampled;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_sampled_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  ren_assert(texture);
  sh::Handle<void> descriptor = m_rg->m_textures[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_sampled_texture_descriptor(RgTextureToken texture) const
    -> sh::Handle<void> {
  if (!texture) {
    return {};
  }
  sh::Handle<void> descriptor = m_rg->m_textures[texture].combined;
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureToken handle,
                                               u32 mip) const
    -> sh::Handle<void> {
  ren_assert(handle);
  ren_assert(m_rg->m_textures[handle].storage);
  const RgRtTexture &texture = m_rg->m_textures[handle];
  ren_assert(mip < texture.num_mips);
  sh::Handle<void> descriptor(texture.storage[mip],
                              sh::DescriptorKind::RWTexture);
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::try_get_storage_texture_descriptor(RgTextureToken handle,
                                                   u32 mip) const
    -> sh::Handle<void> {
  if (!handle) {
    return {};
  }
  ren_assert(m_rg->m_textures[handle].storage);
  const RgRtTexture &texture = m_rg->m_textures[handle];
  if (mip >= texture.num_mips) {
    return {};
  }
  sh::Handle<void> descriptor(texture.storage[mip],
                              sh::DescriptorKind::RWTexture);
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_allocator() const -> UploadBumpAllocator & {
  return *m_rg->m_upload_allocator;
}

} // namespace ren
