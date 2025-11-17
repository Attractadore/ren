#include "CommandRecorder.hpp"
#include "Renderer.hpp"
#include "core/Math.hpp"
#include "ren/core/Assert.hpp"

#include <utility>

namespace ren {

void CommandRecorder::begin(Renderer &renderer, Handle<CommandPool> cmd_pool) {
  m_renderer = &renderer;
  m_cmd = rhi::begin_command_buffer(renderer.get_rhi_device(),
                                    renderer.get_command_pool(cmd_pool).handle);
}

rhi::CommandBuffer CommandRecorder::end() {
  rhi::end_command_buffer(m_cmd);
  return std::exchange(m_cmd, {});
}

void CommandRecorder::copy_buffer(const BufferView &src,
                                  const BufferView &dst) {
  ren_assert(src.size_bytes() <= dst.size_bytes());
  rhi::cmd_copy_buffer(m_cmd,
                       {
                           .src = m_renderer->get_buffer(src.buffer).handle,
                           .dst = m_renderer->get_buffer(dst.buffer).handle,
                           .src_offset = src.offset,
                           .dst_offset = dst.offset,
                           .size = src.size_bytes(),
                       });
}

void CommandRecorder::copy_buffer_to_texture(const BufferView &src,
                                             Handle<Texture> dst, u32 base_mip,
                                             u32 num_mips) {
  const Buffer &buffer = m_renderer->get_buffer(src.buffer);
  const Texture &texture = m_renderer->get_texture(dst);
  ren_assert(base_mip < texture.num_mips);
  if (num_mips == ALL_MIPS) {
    num_mips = texture.num_mips - base_mip;
  }
  ren_assert(base_mip + num_mips <= texture.num_mips);
  rhi::ImageAspectMask aspect_mask =
      rhi::get_format_aspect_mask(texture.format);
  u32 num_layers = (texture.cube_map ? 6 : 1);
  usize offset = src.offset;
  for (u32 mip : range(base_mip, base_mip + num_mips)) {
    glm::uvec3 size = get_mip_size(texture.size, mip);
    rhi::cmd_copy_buffer_to_image(m_cmd, {
                                             .buffer = buffer.handle,
                                             .image = texture.handle,
                                             .buffer_offset = offset,
                                             .aspect_mask = aspect_mask,
                                             .mip = mip,
                                             .num_layers = num_layers,
                                             .image_size = size,
                                         });
    offset += get_mip_byte_size(texture.format, size, num_layers);
  }
}

void CommandRecorder::copy_texture_to_buffer(Handle<Texture> src,
                                             const BufferView &dst,
                                             u32 base_mip, u32 num_mips) {
  const Texture &texture = m_renderer->get_texture(src);
  const Buffer &buffer = m_renderer->get_buffer(dst.buffer);
  ren_assert(base_mip < texture.num_mips);
  if (num_mips == ALL_MIPS) {
    num_mips = texture.num_mips - base_mip;
  }
  ren_assert(base_mip + num_mips <= texture.num_mips);
  rhi::ImageAspectMask aspect_mask =
      rhi::get_format_aspect_mask(texture.format);
  u32 num_layers = (texture.cube_map ? 6 : 1);
  usize offset = dst.offset;
  for (u32 mip : range(base_mip, base_mip + num_mips)) {
    glm::uvec3 size = get_mip_size(texture.size, mip);
    rhi::cmd_copy_image_to_buffer(m_cmd, {
                                             .buffer = buffer.handle,
                                             .image = texture.handle,
                                             .buffer_offset = offset,
                                             .aspect_mask = aspect_mask,
                                             .mip = mip,
                                             .num_layers = num_layers,
                                             .image_size = size,
                                         });
    offset += get_mip_byte_size(texture.format, size, num_layers);
  }
}

void CommandRecorder::fill_buffer(const BufferView &view, u32 value) {
  rhi::cmd_fill_buffer(m_cmd,
                       {
                           .buffer = m_renderer->get_buffer(view.buffer).handle,
                           .offset = view.offset,
                           .size = view.size_bytes(),
                           .value = value,
                       });
}

void CommandRecorder::clear_texture(Handle<Texture> handle,
                                    const glm::vec4 &color) {
  const Texture &texture = m_renderer->get_texture(handle);
  rhi::cmd_clear_image(
      m_cmd, {
                 .image = texture.handle,
                 .color = color,
                 .aspect_mask = rhi::get_format_aspect_mask(texture.format),
                 .num_mips = texture.num_mips,
                 .num_layers = texture.cube_map ? 6u : 1u,
             });
}

void CommandRecorder::pipeline_barrier(
    Span<const rhi::MemoryBarrier> memory_barriers,
    Span<const TextureBarrier> texture_barriers) {
  ScratchArena scratch;
  auto *image_barriers =
      scratch->allocate<rhi::ImageBarrier>(texture_barriers.m_size);
  for (usize i : range(texture_barriers.m_size)) {
    const TextureBarrier &barrier = texture_barriers[i];
    const Texture &texture = m_renderer->get_texture(barrier.resource.handle);
    image_barriers[i] = {
        .image = texture.handle,
        .aspect_mask = rhi::get_format_aspect_mask(texture.format),
        .base_mip = barrier.resource.base_mip,
        .num_mips = barrier.resource.num_mips,
        .num_layers = texture.cube_map ? 6u : 1u,
        .src_stage_mask = barrier.src_stage_mask,
        .src_access_mask = barrier.src_access_mask,
        .src_layout = barrier.src_layout,
        .dst_stage_mask = barrier.dst_stage_mask,
        .dst_access_mask = barrier.dst_access_mask,
        .dst_layout = barrier.dst_layout,
    };
  }
  rhi::cmd_pipeline_barrier(m_cmd, memory_barriers,
                            Span(image_barriers, texture_barriers.m_size));
}

void CommandRecorder::set_event(Handle<Event> event,
                                Span<const rhi::MemoryBarrier> memory_barriers,
                                Span<const TextureBarrier> texture_barriers) {
  ScratchArena scratch;
  auto *image_barriers =
      scratch->allocate<rhi::ImageBarrier>(texture_barriers.m_size);
  for (usize i : range(texture_barriers.m_size)) {
    const TextureBarrier &barrier = texture_barriers[i];
    const Texture &texture = m_renderer->get_texture(barrier.resource.handle);
    image_barriers[i] = {
        .image = texture.handle,
        .aspect_mask = rhi::get_format_aspect_mask(texture.format),
        .base_mip = barrier.resource.base_mip,
        .num_mips = barrier.resource.num_mips,
        .num_layers = texture.cube_map ? 6u : 1u,
        .src_stage_mask = barrier.src_stage_mask,
        .src_access_mask = barrier.src_access_mask,
        .src_layout = barrier.src_layout,
        .dst_stage_mask = barrier.dst_stage_mask,
        .dst_access_mask = barrier.dst_access_mask,
        .dst_layout = barrier.dst_layout,
    };
  }
  rhi::cmd_set_event(m_cmd, m_renderer->get_event(event).handle,
                     memory_barriers,
                     Span(image_barriers, texture_barriers.m_size));
}

void CommandRecorder::wait_event(Handle<Event> event,
                                 Span<const rhi::MemoryBarrier> memory_barriers,
                                 Span<const TextureBarrier> texture_barriers) {
  ScratchArena scratch;
  auto *image_barriers =
      scratch->allocate<rhi::ImageBarrier>(texture_barriers.m_size);
  for (usize i : range(texture_barriers.m_size)) {
    const TextureBarrier &barrier = texture_barriers[i];
    const Texture &texture = m_renderer->get_texture(barrier.resource.handle);
    image_barriers[i] = {
        .image = texture.handle,
        .aspect_mask = rhi::get_format_aspect_mask(texture.format),
        .base_mip = barrier.resource.base_mip,
        .num_mips = barrier.resource.num_mips,
        .num_layers = texture.cube_map ? 6u : 1u,
        .src_stage_mask = barrier.src_stage_mask,
        .src_access_mask = barrier.src_access_mask,
        .src_layout = barrier.src_layout,
        .dst_stage_mask = barrier.dst_stage_mask,
        .dst_access_mask = barrier.dst_access_mask,
        .dst_layout = barrier.dst_layout,
    };
  }
  rhi::cmd_wait_event(m_cmd, m_renderer->get_event(event).handle,
                      memory_barriers,
                      Span(image_barriers, texture_barriers.m_size));
}

void CommandRecorder::reset_event(Handle<Event> event,
                                  rhi::PipelineStageMask stages) {
  rhi::cmd_reset_event(m_cmd, m_renderer->get_event(event).handle, stages);
}

auto CommandRecorder::render_pass(const RenderPassInfo &&begin_info)
    -> RenderPass {
  return RenderPass(*m_renderer, m_cmd, std::move(begin_info));
}

RenderPass::RenderPass(Renderer &renderer, rhi::CommandBuffer cmd,
                       const RenderPassInfo &&info) {
  m_renderer = &renderer;
  m_cmd = cmd;

  glm::uvec2 max_render_area = {-1, -1};
  glm::uvec2 render_area = max_render_area;

  rhi::RenderTarget render_targets[rhi::MAX_NUM_RENDER_TARGETS];
  for (usize i : range(info.render_targets.m_size)) {
    const RenderTarget &rt = info.render_targets[i];
    if (!rt.rtv.texture) {
      continue;
    }
    render_targets[i] = {
        .rtv = m_renderer->get_rtv(rt.rtv),
        .ops = rt.ops,
    };
    render_area = glm::min(
        render_area, glm::uvec2(m_renderer->get_texture(rt.rtv.texture).size));
  }

  rhi::DepthStencilTarget depth_stencil_target;
  if (info.depth_stencil_target.dsv.texture) {
    const DepthStencilTarget &dst = info.depth_stencil_target;
    depth_stencil_target = {
        .dsv = m_renderer->get_rtv(dst.dsv),
        .ops = dst.ops,
    };
    render_area = glm::min(
        render_area, glm::uvec2(m_renderer->get_texture(dst.dsv.texture).size));
  }

  ren_assert_msg(render_area != max_render_area,
                 "At least one attachment must be provided");

  rhi::cmd_begin_render_pass(
      m_cmd,
      {
          .render_targets = Span(render_targets, info.render_targets.m_size),
          .depth_stencil_target = depth_stencil_target,
          .render_area = render_area,
      });
}

RenderPass::~RenderPass() {
  if (m_cmd) {
    end();
  }
}

void RenderPass::end() {
  rhi::cmd_end_render_pass(m_cmd);
  m_cmd = {};
}

void RenderPass::set_viewports(Span<const rhi::Viewport> viewports) {
  rhi::cmd_set_viewports(m_cmd, viewports);
}

void RenderPass::set_scissor_rects(Span<const rhi::Rect2D> rects) {
  rhi::cmd_set_scissor_rects(m_cmd, rects);
}

void RenderPass::bind_graphics_pipeline(Handle<GraphicsPipeline> pipeline) {
  rhi::cmd_bind_pipeline(m_cmd, rhi::PipelineBindPoint::Graphics,
                         m_renderer->get_graphics_pipeline(pipeline).handle);
}

void RenderPass::push_constants(Span<const std::byte> data, unsigned offset) {
  rhi::cmd_push_constants(m_cmd, offset, data);
}

void RenderPass::bind_index_buffer(const BufferView &view,
                                   rhi::IndexType index_type) {
  rhi::cmd_bind_index_buffer(m_cmd, m_renderer->get_buffer(view.buffer).handle,
                             view.offset, index_type);
}

void RenderPass::draw(const rhi::DrawInfo &draw_info) {
  rhi::cmd_draw(m_cmd, draw_info);
}

void RenderPass::draw_indexed(const rhi::DrawIndexedInfo &draw_info) {
  rhi::cmd_draw_indexed(m_cmd, draw_info);
}

void RenderPass::draw_indirect_count(
    const BufferSlice<sh::DrawIndirectCommand> &slice,
    const BufferSlice<u32> &counter) {
  const Buffer &buffer = m_renderer->get_buffer(slice.buffer);
  const Buffer &count_buffer = m_renderer->get_buffer(counter.buffer);
  rhi::cmd_draw_indirect_count(
      m_cmd, {
                 .buffer = buffer.handle,
                 .buffer_offset = slice.offset,
                 .buffer_stride = sizeof(sh::DrawIndirectCommand),
                 .count_buffer = count_buffer.handle,
                 .count_buffer_offset = counter.offset,
                 .max_count = slice.count,
             });
}

void RenderPass::draw_indexed_indirect_count(
    const BufferSlice<sh::DrawIndexedIndirectCommand> &slice,
    const BufferSlice<u32> &counter) {
  const Buffer &buffer = m_renderer->get_buffer(slice.buffer);
  const Buffer &count_buffer = m_renderer->get_buffer(counter.buffer);
  rhi::cmd_draw_indexed_indirect_count(
      m_cmd, {
                 .buffer = buffer.handle,
                 .buffer_offset = slice.offset,
                 .buffer_stride = sizeof(sh::DrawIndexedIndirectCommand),
                 .count_buffer = count_buffer.handle,
                 .count_buffer_offset = counter.offset,
                 .max_count = slice.count,
             });
}

void CommandRecorder::bind_compute_pipeline(Handle<ComputePipeline> pipeline) {
  m_pipeline = pipeline;
  rhi::cmd_bind_pipeline(m_cmd, rhi::PipelineBindPoint::Compute,
                         m_renderer->get_compute_pipeline(pipeline).handle);
}

void CommandRecorder::push_constants(Span<const std::byte> data,
                                     unsigned offset) {
  rhi::cmd_push_constants(m_cmd, offset, data);
}

void CommandRecorder::dispatch(u32 num_groups_x, u32 num_groups_y,
                               u32 num_groups_z) {
  // ren_assert_msg(num_groups_x < 2048, "Suspiciously big dispatch");
  // ren_assert_msg(num_groups_y < 2048, "Suspiciously big dispatch");
  // ren_assert_msg(num_groups_z < 2048, "Suspiciously big dispatch");
  rhi::cmd_dispatch(m_cmd, num_groups_x, num_groups_y, num_groups_z);
}

void CommandRecorder::dispatch(glm::uvec2 num_groups) {
  dispatch(num_groups.x, num_groups.y);
}

void CommandRecorder::dispatch(glm::uvec3 num_groups) {
  dispatch(num_groups.x, num_groups.y, num_groups.z);
}

void CommandRecorder::dispatch_grid(u32 size, u32 group_size_mult) {
  dispatch_grid_3d({size, 1, 1}, {group_size_mult, 1, 1});
}

void CommandRecorder::dispatch_grid_2d(glm::uvec2 size,
                                       glm::uvec2 group_size_mult) {
  dispatch_grid_3d({size, 1}, {group_size_mult, 1});
}

void CommandRecorder::dispatch_grid_3d(glm::uvec3 size,
                                       glm::uvec3 group_size_mult) {
  glm::uvec3 block_size =
      m_renderer->get_compute_pipeline(m_pipeline).local_size * group_size_mult;
  glm::uvec3 num_groups;
  for (int i = 0; i < num_groups.length(); ++i) {
    num_groups[i] = ceil_div(size[i], block_size[i]);
  }
  dispatch(num_groups);
}

void CommandRecorder::dispatch_indirect(
    const BufferSlice<sh::DispatchIndirectCommand> &slice) {
  rhi::cmd_dispatch_indirect(m_cmd, m_renderer->get_buffer(slice.buffer).handle,
                             slice.offset);
}

auto CommandRecorder::debug_region(String8 label) -> DebugRegion {
  return DebugRegion(m_cmd, label);
}

DebugRegion::DebugRegion(rhi::CommandBuffer cmd, String8 label) {
  m_cmd = cmd;
  rhi::cmd_begin_debug_label(cmd, label);
}

DebugRegion::~DebugRegion() {
  if (m_cmd) {
    end();
  }
}

void DebugRegion::end() {
  rhi::cmd_end_debug_label(m_cmd);
  m_cmd = {};
}

#if 0

auto init_event_pool(ResourceArena &arena) -> EventPool {
  return {
      .m_arena = &arena,
      .m_events = {{}},
      .m_index = 1,
  };
};

auto set_event(CommandRecorder &cmd, EventPool &pool,
               Span<const rhi::MemoryBarrier> memory_barriers,
               Span<const TextureBarrier> texture_barriers) -> EventId {
  [[unlikely]] if (pool.m_index == pool.m_events.m_size) {
    pool.m_events.push_back({
        .handle = pool.m_arena->create_event(),
    });
  }

  EventData &event = pool.m_events[pool.m_index];
  event.memory_barrier_offset = pool.memory_barriers.m_size;
  event.memory_barrier_count = memory_barriers.m_size;
  event.texture_barrier_offset = pool.texture_barriers.m_size;
  event.texture_barrier_count = texture_barriers.m_size;
  pool.memory_barriers.append(memory_barriers);
  pool.texture_barriers.append(texture_barriers);

  cmd.set_event(event.handle, memory_barriers, texture_barriers);

  return EventId(pool.m_index++);
}

void wait_event(CommandRecorder &cmd, const EventPool &pool, EventId id) {
  const EventData &event = pool.m_events[id];
  cmd.wait_event(
      event.handle,
      Span(pool.memory_barriers)
          .subspan(event.memory_barrier_offset, event.memory_barrier_count),
      Span(pool.texture_barriers)
          .subspan(event.texture_barrier_offset, event.texture_barrier_count));
}

void reset_event_pool(CommandRecorder &cmd, EventPool &pool) {
  for (usize i : range<usize>(1, pool.m_events.m_size)) {
    cmd.reset_event(pool.m_events[i].handle);
  }
  pool.memory_barriers.clear();
  pool.texture_barriers.clear();
  pool.m_index = 1;
}

#endif

} // namespace ren
