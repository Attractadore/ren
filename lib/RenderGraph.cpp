#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "Support/Algorithm.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

namespace ren {

auto RgRuntime::get_buffer(RgBufferToken buffer) const -> const BufferView & {
  ren_assert(buffer);
  return m_rg->m_buffers[m_rg->m_physical_buffers[buffer]];
}

auto RgRuntime::get_texture(RgTextureToken texture) const -> Handle<Texture> {
  ren_assert(texture);
  return m_rg->m_textures[m_rg->m_physical_textures[texture]];
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureToken texture) const
    -> StorageTextureId {
  ren_assert(texture);
  return m_rg
      ->m_storage_texture_descriptors[m_rg->m_physical_textures[texture]];
}

auto RgRuntime::get_texture_set() const -> VkDescriptorSet {
  return m_rg->m_tex_alloc.get_set();
}

auto RgRuntime::get_device_allocator() const -> DeviceBumpAllocator & {
  return m_rg->m_device_allocator;
}

auto RgRuntime::get_upload_allocator() const -> UploadBumpAllocator & {
  return m_rg->m_upload_allocator;
}

RenderGraph::RenderGraph(Renderer &renderer, TextureIdAllocator &tex_alloc)
    : m_tex_alloc(tex_alloc), m_arena(renderer), m_device_allocator(renderer),
      m_upload_allocator(renderer) {
  m_renderer = &renderer;
}

void RenderGraph::set_texture(RgTextureId id, Handle<Texture> texture,
                              const RgTextureUsage &usage) {
  ren_assert(id);
  ren_assert(texture);
  RgPhysicalTextureId physical_texture = m_physical_textures[id];
  m_textures[physical_texture] = texture;
  m_texture_usages[physical_texture] = usage;
}

void RenderGraph::set_semaphore(RgSemaphoreId id, Handle<Semaphore> semaphore) {
  ren_assert(id);
  ren_assert(semaphore);
  m_semaphores[id] = semaphore;
}

void RenderGraph::rotate_resources() {
  for (usize buffer = 0; buffer < m_buffers.size(); buffer += PIPELINE_DEPTH) {
    rotate_left(Span(m_buffers).subspan(buffer, PIPELINE_DEPTH));
  }

  for (usize texture = 0; texture < m_textures.size();) {
    usize num_temporal_layers = m_texture_temporal_layer_count[texture];
    rotate_left(Span(m_textures).subspan(texture, num_temporal_layers));
    rotate_left(Span(m_texture_usages).subspan(texture, num_temporal_layers));
    rotate_left(Span(m_storage_texture_descriptors)
                    .subspan(texture, num_temporal_layers));
    texture += num_temporal_layers;
  }
}

void RenderGraph::execute(CommandAllocator &cmd_alloc) {
  rotate_resources();
  place_barriers();

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
    m_renderer->graphicsQueueSubmit(batch_cmd_buffers, batch_wait_semaphores,
                                    batch_signal_semaphores);
    batch_cmd_buffers.clear();
    batch_wait_semaphores.clear();
    batch_signal_semaphores.clear();
  };

  Vector<VkMemoryBarrier2> vk_memory_barriers;
  Vector<VkImageMemoryBarrier2> vk_image_barriers;

  for (const RgPassRuntimeInfo &pass : m_passes) {
    if (not pass.wait_semaphores.empty()) {
      submit_batch();
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
          debug_region.emplace(
              cmd_recorder->debug_region(m_pass_names[pass.pass].c_str()));
#endif
        }
        return *cmd_recorder;
      };

      if (pass.num_memory_barriers > 0 or pass.num_texture_barriers > 0) {
        auto memory_barriers =
            Span(m_memory_barriers)
                .subspan(pass.base_memory_barrier, pass.num_memory_barriers);
        auto texture_barriers =
            Span(m_texture_barriers)
                .subspan(pass.base_texture_barrier, pass.num_texture_barriers);
        get_command_recorder().pipeline_barrier(memory_barriers,
                                                texture_barriers);
      }

      pass.data.visit(OverloadSet{
          [&](const RgHostPass &host_pass) {
            if (host_pass.cb) {
              host_pass.cb(*m_renderer, rt);
            }
          },
          [&](const RgGraphicsPass &graphics_pass) {
            if (!graphics_pass.cb) {
              return;
            }

            glm::uvec2 viewport = {-1, -1};

            auto color_attachments =
                Span(m_color_attachments)
                    .subspan(graphics_pass.base_color_attachment,
                             graphics_pass.num_color_attachments) |
                map([&](const Optional<RgColorAttachment> &att)
                        -> Optional<ColorAttachment> {
                  return att.map(
                      [&](const RgColorAttachment &att) -> ColorAttachment {
                        TextureView view = m_renderer->get_texture_view(
                            m_textures[m_physical_textures[att.texture]]);
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
                      m_depth_stencil_attachments[index];
                  TextureView view = m_renderer->get_texture_view(
                      m_textures[m_physical_textures[att.texture]]);
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
          [&](const RgComputePass &compute_pass) {
            if (compute_pass.cb) {
              ComputePass comp = get_command_recorder().compute_pass();
              compute_pass.cb(*m_renderer, rt, comp);
            }
          },
          [&](const RgGenericPass &pass) {
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

    auto get_vk_semaphore =
        [&](RgSemaphoreSignalId id) -> VkSemaphoreSubmitInfo {
      const RgSemaphoreSignal &signal = m_semaphore_signals[id];
      return {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore =
              m_renderer->get_semaphore(m_semaphores[signal.semaphore]).handle,
          .value = signal.value,
          .stageMask = signal.stage_mask,
      };
    };

    batch_wait_semaphores.append(pass.wait_semaphores | map(get_vk_semaphore));

    batch_signal_semaphores.append(pass.signal_semaphores |
                                   map(get_vk_semaphore));

    if (not pass.signal_semaphores.empty()) {
      submit_batch();
    }
  }

  submit_batch();
}

void RenderGraph::place_barriers() {
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

  Vector<RgBufferUsage> buffer_after_write_hazard_src_states(m_buffers.size());
  Vector<VkPipelineStageFlags2> buffer_after_read_hazard_src_states(
      m_buffers.size());

  struct TextureUsageWithoutLayout {
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  };

  Vector<TextureUsageWithoutLayout> texture_after_write_hazard_src_states(
      m_textures.size());
  Vector<VkPipelineStageFlags2> texture_after_read_hazard_src_states(
      m_textures.size());
  Vector<VkImageLayout> texture_layouts(m_textures.size());

  for (auto i : range<usize>(1, m_textures.size())) {
    const RgTextureUsage &state = m_texture_usages[i];
    if (state.access_mask & WRITE_ONLY_ACCESS_MASK) {
      texture_after_write_hazard_src_states[i] = {
          .stage_mask = state.stage_mask,
          .access_mask = state.access_mask & WRITE_ONLY_ACCESS_MASK,
      };
    } else {
      texture_after_read_hazard_src_states[i] = state.access_mask;
    }
    texture_layouts[i] =
        m_external_textures[i] ? state.layout : VK_IMAGE_LAYOUT_UNDEFINED;
  }

  m_memory_barriers.clear();
  m_texture_barriers.clear();

  for (RgPassRuntimeInfo &pass : m_passes) {
    usize old_memory_barrier_count = m_memory_barriers.size();
    usize old_texture_barrier_count = m_texture_barriers.size();

    // TODO: merge separate barriers together if it doesn't change how
    // synchronization happens
    auto maybe_place_barrier_for_buffer = [&](RgBufferUseId use_id) {
      const RgBufferUse &use = m_buffer_uses[use_id];
      RgPhysicalBufferId physical_buffer = m_physical_buffers[use.buffer];

      VkPipelineStageFlags2 dst_stage_mask = use.usage.stage_mask;
      VkAccessFlags2 dst_access_mask = use.usage.access_mask;

      // Don't need a barrier for host-only accesses
      bool is_host_only_access = dst_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_host_only_access) {
        ren_assert(dst_access_mask == VK_ACCESS_2_NONE);
        return;
      }

      VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
      VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

      if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
        RgBufferUsage &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        // Reset the source stage mask that the next WAR hazard will use
        src_stage_mask =
            std::exchange(buffer_after_read_hazard_src_states[physical_buffer],
                          VK_PIPELINE_STAGE_2_NONE);
        // If this is a WAR hazard, need to wait for all previous
        // reads to finish. The previous write's memory has already been made
        // available by previous RAW barriers, so it only needs to be made
        // visible
        // FIXME: According to the Vulkan spec, WAR hazards require only an
        // execution barrier
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // No reads were performed between this write and the previous one so
          // this is a WAW hazard. Need to wait for the previous write to finish
          // and make its memory available and visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // hazards will use
        after_write_state.stage_mask = dst_stage_mask;
        // Read accesses are redundant for source access mask
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;
      } else {
        // This is a RAW hazard. Need to wait for the previous write to finish
        // and make it's memory available and visible
        // TODO/FIXME: all RAW barriers should be merged, since if they are
        // issued separately they might cause the cache to be flushed multiple
        // times
        const RgBufferUsage &after_write_state =
            buffer_after_write_hazard_src_states[physical_buffer];
        src_stage_mask = after_write_state.stage_mask;
        src_access_mask = after_write_state.access_mask;
        // Update the source stage mask that the next WAR hazard will use
        buffer_after_read_hazard_src_states[physical_buffer] |= dst_stage_mask;
      }

      // First barrier isn't required and can be skipped
      bool is_first_access = src_stage_mask == VK_PIPELINE_STAGE_2_NONE;
      if (is_first_access) {
        assert(src_access_mask == VK_PIPELINE_STAGE_2_NONE);
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
      RgPhysicalTextureId physical_texture = m_physical_textures[use.texture];

      VkPipelineStageFlags2 dst_stage_mask = use.usage.stage_mask;
      VkAccessFlags2 dst_access_mask = use.usage.access_mask;
      VkImageLayout dst_layout = use.usage.layout;
      assert(dst_layout);
      if (dst_layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        assert(dst_stage_mask);
        assert(dst_access_mask);
      }

      VkImageLayout &src_layout = texture_layouts[physical_texture];

      if (dst_layout == src_layout) {
        // Only a memory barrier is required if layout doesn't change
        // NOTE: this code is copy-pasted from above

        VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;

        if (dst_access_mask & WRITE_ONLY_ACCESS_MASK) {
          TextureUsageWithoutLayout &after_write_state =
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
          const TextureUsageWithoutLayout &after_write_state =
              texture_after_write_hazard_src_states[physical_texture];
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
          texture_after_read_hazard_src_states[physical_texture] |=
              dst_stage_mask;
        }

        assert(src_stage_mask != VK_PIPELINE_STAGE_2_NONE);

        m_memory_barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
        });
      } else {
        // Need an image barrier to change layout.
        // Layout transitions are read-write operations, so only to take care of
        // WAR and WAW hazards in this case

        TextureUsageWithoutLayout &after_write_state =
            texture_after_write_hazard_src_states[physical_texture];
        // If this is a WAR hazard, must wait for all previous reads to finish
        // and make the layout transition's memory available.
        // Also reset the source stage mask that the next WAR barrier will use
        VkPipelineStageFlags2 src_stage_mask = std::exchange(
            texture_after_read_hazard_src_states[physical_texture],
            VK_PIPELINE_STAGE_2_NONE);
        VkAccessFlagBits2 src_access_mask = VK_ACCESS_2_NONE;
        if (src_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
          // If there were no reads between this write and the previous one,
          // need to wait for the previous write to finish and make it's memory
          // available and the layout transition's memory visible
          src_stage_mask = after_write_state.stage_mask;
          src_access_mask = after_write_state.access_mask;
        }
        // Update the source stage and access masks that further RAW and WAW
        // barriers will use
        after_write_state.stage_mask = dst_stage_mask;
        after_write_state.access_mask =
            dst_access_mask & WRITE_ONLY_ACCESS_MASK;

        const Texture &texture =
            m_renderer->get_texture(m_textures[physical_texture]);

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

    usize new_memory_barrier_count = m_memory_barriers.size();
    usize new_texture_barrier_count = m_texture_barriers.size();

    pass.base_memory_barrier = old_memory_barrier_count;
    pass.num_memory_barriers =
        new_memory_barrier_count - old_memory_barrier_count;
    pass.base_texture_barrier = old_texture_barrier_count;
    pass.num_texture_barriers =
        new_texture_barrier_count - old_texture_barrier_count;
  }

  for (auto i : range<usize>(1, m_textures.size())) {
    VkPipelineStageFlags2 stage_mask = texture_after_read_hazard_src_states[i];
    VkAccessFlags2 access_mask = 0;
    if (!stage_mask) {
      const TextureUsageWithoutLayout &state =
          texture_after_write_hazard_src_states[i];
      stage_mask = state.stage_mask;
      access_mask = state.access_mask;
    }
    m_texture_usages[i] = {
        .stage_mask = stage_mask,
        .access_mask = access_mask,
        .layout = texture_layouts[i],
    };
  }
}

} // namespace ren
