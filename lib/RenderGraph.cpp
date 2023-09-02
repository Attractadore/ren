#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "PipelineLoading.hpp"
#include "Support/Algorithm.hpp"
#include "Support/Errors.hpp"
#include "Support/Math.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

namespace ren {

auto RenderGraph::physical_buffers() const {
  return range<int>(1, m_buffers.size()) |
         map([](int idx) { return RgPhysicalBufferId(idx); });
}

auto RenderGraph::get_physical_buffer(RgBufferId buffer) const
    -> RgPhysicalBufferId {
  RgBufferId parent = m_buffer_parents[buffer];
  assert(m_buffer_parents[parent] == parent);
  return RgPhysicalBufferId(parent);
}

auto RgRuntime::get_buffer(RgBufferId buffer) const -> const BufferView & {
  assert(buffer);
  return m_rg->m_buffers[m_rg->get_physical_buffer(buffer)];
}

auto RgRuntime::map_buffer_impl(RgBufferId buffer, usize offset) const
    -> std::byte * {
  const BufferView &view = get_buffer(buffer);
  return m_rg->m_device->map_buffer<std::byte>(view, offset);
}

auto RenderGraph::get_physical_texture(RgTextureId texture) const
    -> RgPhysicalTextureId {
  RgTextureId parent = m_texture_parents[texture];
  assert(m_texture_parents[parent] == parent);
  return RgPhysicalTextureId(parent);
}

auto RgRuntime::get_texture(RgTextureId texture) const -> Handle<Texture> {
  assert(texture);
  return m_rg->m_textures[m_rg->get_physical_texture(texture)];
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureId texture) const
    -> StorageTextureID {
  assert(texture);
  return m_rg
      ->m_storage_texture_descriptors[m_rg->get_physical_texture(texture)];
}

RgUpdate::RgUpdate(RenderGraph &rg) { m_rg = &rg; }

void RgUpdate::resize_buffer(RgBufferId buffer, usize size) {
  m_rg->m_buffer_descs[m_rg->get_physical_buffer(buffer)].size = size;
}

auto RgUpdate::get_texture_desc(RgTextureId texture_id) const
    -> RgPublicTextureDesc {
  const Texture &texture = m_rg->m_device->get_texture(
      m_rg->m_textures[m_rg->get_physical_texture(texture_id)]);
  return {
      .type = texture.type,
      .format = texture.format,
      .size = texture.size,
      .num_mip_levels = texture.num_mip_levels,
      .num_array_layers = texture.num_array_layers,
  };
}

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

  m_pipeline_layout = create_pipeline_layout(
      m_arena, m_tex_alloc->get_set_layout(), {}, "Render graph");
}

auto RenderGraph::is_pass_valid(StringView pass) const -> bool {
  return m_pass_ids.contains(pass);
}

auto RenderGraph::is_buffer_valid(StringView buffer) const -> bool {
  return m_buffer_ids.contains(buffer);
}

auto RenderGraph::is_texture_valid(StringView texture) const -> bool {
  return m_texture_ids.contains(texture);
}

void RenderGraph::rotate_resources() {
  rotate_left(m_heap_buffers);

  rotate_left(Span(m_semaphores)
                  .subspan(m_acquire_semaphore, m_acquire_semaphores.size()));
  rotate_left(Span(m_semaphores)
                  .subspan(m_present_semaphore, m_present_semaphores.size()));

  for (const auto &[texture, desc] : m_texture_descs) {
    rotate_left(Span(m_textures).subspan(texture, desc.num_instances));
    rotate_left(Span(m_storage_texture_descriptors)
                    .subspan(texture, desc.num_instances));
  }

  m_swapchain->acquireImage(m_semaphores[m_acquire_semaphore]);
  m_textures[m_backbuffer] = m_swapchain->getTexture().texture;
}

void RenderGraph::allocate_buffers() {
  // Calculate required size for each buffer heap
  std::array<usize, NUM_BUFFER_HEAPS> required_heap_sizes = {};
  for (RgPhysicalBufferId buffer : physical_buffers()) {
    const RgBufferDesc &desc = m_buffer_descs[buffer];
    auto heap = int(desc.heap);
    usize heap_size = required_heap_sizes[heap];
    heap_size = pad(heap_size, GPU_COALESCING_WIDTH) + desc.size;
    required_heap_sizes[heap] = heap_size;
  }

  // Resize each buffer heap if necessary
  Span<Handle<Buffer>> heaps = m_heap_buffers.front();
  for (int heap = 0; heap < heaps.size(); ++heap) {
    usize required_heap_size = required_heap_sizes[heap];
    const Buffer &heap_buffer = m_device->get_buffer(heaps[heap]);
    if (heap_buffer.size < required_heap_size) {
      Handle<Buffer> old_heap = heaps[heap];
      heaps[heap] = m_arena.create_buffer({
          .name = fmt::format("Render graph buffer for heap {}", heap),
          .heap = heap_buffer.heap,
          .usage = heap_buffer.usage,
          .size = std::bit_ceil(required_heap_size),
      });
      m_arena.destroy_buffer(old_heap);
    }
  }

  // Allocate buffers
  std::array<usize, NUM_BUFFER_HEAPS> stack_tops = {};
  for (RgPhysicalBufferId buffer : physical_buffers()) {
    const RgBufferDesc &desc = m_buffer_descs[buffer];
    auto heap = int(desc.heap);
    usize offset = pad(stack_tops[heap], GPU_COALESCING_WIDTH);
    usize size = desc.size;
    assert(offset + size <= required_heap_sizes[heap]);
    stack_tops[heap] = offset + size;
    m_buffers[buffer] = {
        .buffer = heaps[heap],
        .offset = offset,
        .size = size,
    };
  }
}

void RenderGraph::execute(CommandAllocator &cmd_alloc) {
  rotate_resources();

  allocate_buffers();

  RgRuntime rt;
  rt.m_rg = this;

  Vector<VkCommandBufferSubmitInfo> batch_cmd_buffers;
  Vector<VkSemaphoreSubmitInfo> batch_wait_semaphores;
  Vector<VkSemaphoreSubmitInfo> batch_signal_semaphores;

  auto submit_batch = [&] {
    if (batch_cmd_buffers.empty() and batch_wait_semaphores.empty() and
        batch_signal_semaphores.empty()) {
      return;
    }
    m_device->graphicsQueueSubmit(batch_cmd_buffers, batch_wait_semaphores,
                                  batch_signal_semaphores);
    batch_cmd_buffers.clear();
    batch_wait_semaphores.clear();
    batch_signal_semaphores.clear();
  };

  Vector<VkMemoryBarrier2> vk_memory_barriers;
  Vector<VkImageMemoryBarrier2> vk_image_barriers;

  Span<RgMemoryBarrier> rg_memory_barriers = m_memory_barriers;
  Span<RgTextureBarrier> rg_texture_barriers = m_texture_barriers;
  Span<RgSemaphoreSignal> rg_wait_semaphores = m_wait_semaphores;
  Span<RgSemaphoreSignal> rg_signal_semaphores = m_signal_semaphores;

  Span<Optional<RgColorAttachment>> rg_color_attachments = m_color_attachments;
  Span<RgDepthStencilAttachment> rg_depth_stencil_attachments =
      m_depth_stencil_attachments;

  for (const RgPassRuntimeInfo &pass : m_passes) {
    if (pass.num_wait_semaphores > 0) {
      submit_batch();
    }

    VkCommandBuffer cmd_buffer = nullptr;
    Optional<CommandRecorder> cmd_recorder;
    Optional<DebugRegion> debug_region;
    auto get_command_recorder = [&]() -> CommandRecorder & {
      if (!cmd_recorder) {
        cmd_buffer = cmd_alloc.allocate();
        cmd_recorder.emplace(*m_device, cmd_buffer);
#if REN_RG_DEBUG
        debug_region.emplace(
            cmd_recorder->debug_region(m_pass_names[pass.pass].c_str()));
#endif
      }
      return *cmd_recorder;
    };

    auto get_vk_memory_barrier =
        [](const RgMemoryBarrier &barrier) -> VkMemoryBarrier2 {
      return {
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = barrier.src_stage_mask,
          .srcAccessMask = barrier.src_access_mask,
          .dstStageMask = barrier.dst_stage_mask,
          .dstAccessMask = barrier.dst_access_mask,
      };
    };

    auto get_vk_image_barrier =
        [&](const RgTextureBarrier &barrier) -> VkImageMemoryBarrier2 {
      const Texture &texture =
          m_device->get_texture(m_textures[barrier.texture]);
      return {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = barrier.src_stage_mask,
          .srcAccessMask = barrier.src_access_mask,
          .dstStageMask = barrier.dst_stage_mask,
          .dstAccessMask = barrier.dst_access_mask,
          .oldLayout = barrier.src_layout,
          .newLayout = barrier.dst_layout,
          .image = texture.image,
          .subresourceRange =
              {
                  .aspectMask = getVkImageAspectFlags(texture.format),
                  .levelCount = texture.num_mip_levels,
                  .layerCount = texture.num_array_layers,
              },
      };
    };

    vk_memory_barriers.assign(
        rg_memory_barriers.pop_front(pass.num_memory_barriers));

    vk_image_barriers.assign(
        rg_texture_barriers.pop_front(pass.num_texture_barriers) |
        map(get_vk_image_barrier));

    if (not vk_memory_barriers.empty() or not vk_image_barriers.empty()) {
      CommandRecorder &rec = get_command_recorder();
      rec.pipeline_barrier(vk_memory_barriers, vk_image_barriers);
    }

    pass.type.visit(OverloadSet{
        [&](const RgHostPass &host_pass) {
          if (host_pass.cb) {
            host_pass.cb(*m_device, rt, m_pass_datas[pass.pass]);
          }
        },
        [&](const RgGraphicsPass &graphics_pass) {
          if (!graphics_pass.cb) {
            return;
          }

          glm::uvec2 viewport = {-1, -1};

          auto color_attachments =
              rg_color_attachments.pop_front(
                  graphics_pass.num_color_attachments) |
              map([&](const Optional<RgColorAttachment> &att)
                      -> Optional<ColorAttachment> {
                return att.map(
                    [&](const RgColorAttachment &att) -> ColorAttachment {
                      TextureView view =
                          m_device->get_texture_view(m_textures[att.texture]);
                      viewport = m_device->get_texture_view_size(view);
                      return {
                          .texture = view,
                          .ops = att.ops,
                      };
                    });
              }) |
              ranges::to<StaticVector<Optional<ColorAttachment>,
                                      MAX_COLOR_ATTACHMENTS>>;

          Optional<DepthStencilAttachment> depth_stencil_attachment;
          if (graphics_pass.has_depth_attachment) {
            const RgDepthStencilAttachment &att =
                rg_depth_stencil_attachments.pop_front();
            TextureView view =
                m_device->get_texture_view(m_textures[att.texture]);
            viewport = m_device->get_texture_view_size(view);
            depth_stencil_attachment = {
                .texture = view,
                .depth_ops = att.depth_ops,
                .stencil_ops = att.stencil_ops,
            };
          }

          RenderPass render_pass = get_command_recorder().render_pass({
              .color_attachments = color_attachments,
              .depth_stencil_attachment = depth_stencil_attachment,
          });
          render_pass.set_viewports({{
              .width = float(viewport.x),
              .height = float(viewport.y),
          }});
          render_pass.set_scissor_rects({{.extent = {viewport.x, viewport.y}}});
          render_pass.bind_descriptor_sets(m_pipeline_layout,
                                           {m_tex_alloc->get_set()});

          graphics_pass.cb(*m_device, rt, render_pass, m_pass_datas[pass.pass]);
        },
        [&](const RgComputePass &compute_pass) {
          if (compute_pass.cb) {
            ComputePass comp = get_command_recorder().compute_pass();
            comp.bind_descriptor_sets(m_pipeline_layout,
                                      {m_tex_alloc->get_set()});
            compute_pass.cb(*m_device, rt, comp, m_pass_datas[pass.pass]);
          }
        },
        [&](const RgTransferPass &transfer_pass) {
          if (transfer_pass.cb) {
            transfer_pass.cb(*m_device, rt, get_command_recorder(),
                             m_pass_datas[pass.pass]);
          }
        },
    });

    auto get_vk_semaphore =
        [&](const RgSemaphoreSignal &signal) -> VkSemaphoreSubmitInfo {
      return {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore =
              m_device->get_semaphore(m_semaphores[signal.semaphore]).handle,
          .value = signal.value,
          .stageMask = signal.stage_mask,
      };
    };

    batch_wait_semaphores.append(
        rg_wait_semaphores.pop_front(pass.num_wait_semaphores) |
        map(get_vk_semaphore));

    batch_signal_semaphores.append(
        rg_signal_semaphores.pop_front(pass.num_signal_semaphores) |
        map(get_vk_semaphore));

    if (pass.num_signal_semaphores > 0) {
      submit_batch();
    }
  }

  submit_batch();

  m_swapchain->presentImage(m_semaphores[m_present_semaphore]);
}

} // namespace ren
