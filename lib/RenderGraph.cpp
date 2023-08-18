#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

namespace ren {

auto RgRuntime::get_buffer(RgRtBuffer buffer) const -> const BufferView & {
  assert(buffer);
  return m_rg->m_buffers[buffer];
}

template <>
auto RgRuntime::map_buffer<std::byte>(RgRtBuffer buffer, usize offset) const
    -> std::byte * {
  const BufferView &view = get_buffer(buffer);
  return m_rg->m_device->map_buffer<std::byte>(view, offset);
}

auto RgRuntime::get_texture(RgRtTexture texture) const -> Handle<Texture> {
  assert(texture);
  return m_rg->m_textures[texture];
}

auto RgRuntime::get_storage_texture_descriptor(RgRtTexture texture) const
    -> StorageTextureID {
  assert(texture);
  return m_rg->m_storage_texture_descriptors[texture];
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
}

auto RenderGraph::is_pass_valid(StringView pass) -> bool { todo(); }

void RenderGraph::execute(CommandAllocator &cmd_alloc) {
  todo();

  RgRuntime rt;
  rt.m_rg = this;

  m_swapchain->acquireImage(m_semaphores[m_acquire_semaphore]);
  m_textures[m_backbuffer] = m_swapchain->getTexture().texture;

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

  Span<RgHostPass> rg_host_passes = m_host_passes;

  Span<RgGraphicsPass> rg_graphics_passes = m_graphics_passes;
  Span<Optional<RgColorAttachment>> rg_color_attachments = m_color_attachments;
  Span<RgDepthStencilAttachment> rg_depth_stencil_attachments =
      m_depth_stencil_attachments;

  Span<RgComputePass> rg_compute_passes = m_compute_passes;

  Span<RgTransferPass> rg_transfer_passes = m_transfer_passes;

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
        debug_region.emplace(cmd_recorder->debug_region(pass.name.c_str()));
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
        rg_memory_barriers.pop_front(pass.num_memory_barriers) |
        map(get_vk_memory_barrier));

    vk_image_barriers.assign(
        rg_texture_barriers.pop_front(pass.num_texture_barriers) |
        map(get_vk_image_barrier));

    if (not vk_memory_barriers.empty() or not vk_image_barriers.empty()) {
      CommandRecorder &rec = get_command_recorder();
      rec.pipeline_barrier(vk_memory_barriers, vk_image_barriers);
    }

    switch (pass.type) {
    case RgPassType::Host: {
      RgHostPass &host_pass = rg_host_passes.pop_front();
      if (host_pass.cb) {
        host_pass.cb(*m_device, rt, m_pass_data[pass.name]);
      }
    } break;
    case RgPassType::Graphics: {
      RgGraphicsPass &graphics_pass = rg_graphics_passes.pop_front();
      if (!graphics_pass.cb) {
        break;
      }

      CommandRecorder &rec = get_command_recorder();

      auto color_attachments =
          rg_color_attachments.pop_front(graphics_pass.num_color_attachments) |
          map([&](const Optional<RgColorAttachment> &att)
                  -> Optional<ColorAttachment> {
            return att.map(
                [&](const RgColorAttachment &att) -> ColorAttachment {
                  return {
                      .texture =
                          m_device->get_texture_view(m_textures[att.texture]),
                      .ops = att.ops,
                  };
                });
          }) |
          ranges::to<
              StaticVector<Optional<ColorAttachment>, MAX_COLOR_ATTACHMENTS>>;

      Optional<DepthStencilAttachment> depth_stencil_attachment;
      if (graphics_pass.has_depth_attachment) {
        const RgDepthStencilAttachment &att =
            rg_depth_stencil_attachments.pop_front();
        depth_stencil_attachment = {
            .texture = m_device->get_texture_view(m_textures[att.texture]),
            .depth_ops = att.depth_ops,
            .stencil_ops = att.stencil_ops,
        };
      }

      RenderPass render_pass = rec.render_pass({
          .color_attachments = color_attachments,
          .depth_stencil_attachment = depth_stencil_attachment,
      });
      render_pass.set_viewports({{
          .width = float(graphics_pass.viewport.x),
          .height = float(graphics_pass.viewport.y),
          .maxDepth = 1.0f,
      }});
      render_pass.set_scissor_rects({{
          .extent = {graphics_pass.viewport.x, graphics_pass.viewport.y},
      }});
      graphics_pass.cb(*m_device, rt, render_pass, m_pass_data[pass.name]);
    } break;
    case RgPassType::Compute: {
      RgComputePass &compute_pass = rg_compute_passes.pop_front();
      if (compute_pass.cb) {
        ComputePass comp = get_command_recorder().compute_pass();
        compute_pass.cb(*m_device, rt, comp, m_pass_data[pass.name]);
      }
    } break;
    case RgPassType::Transfer: {
      RgTransferPass &transfer_pass = rg_transfer_passes.pop_front();
      if (transfer_pass.cb) {
        transfer_pass.cb(*m_device, rt, get_command_recorder(),
                         m_pass_data[pass.name]);
      }
    } break;
    }

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
