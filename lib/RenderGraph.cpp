#include "RenderGraph.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Support/Views.hpp"
#include "Swapchain.hpp"

#include <fmt/format.h>

namespace ren {

RgPersistent::RgPersistent(Renderer &renderer,
                           TextureIdAllocator &texture_descriptor_allocator)
    : m_texture_arena(renderer),
      m_texture_descriptor_allocator(texture_descriptor_allocator) {}

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
        m_temporal_texture_init_info[physical_texture_id] = {
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
          .parent = id,
#if REN_RG_DEBUG
          .name = std::move(init_name),
#endif
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
            .parent = id,
#if REN_RG_DEBUG
            .name = std::move(name),
#endif
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
  m_num_frame_physical_textures = 0;
  m_temporal_texture_init_info.clear();
  m_textures.clear();
  m_frame_textures.clear();
  m_texture_descriptor_allocator.clear();
  m_semaphores.clear();
}

void RgPersistent::rotate_textures() {
  ren_assert(m_physical_textures.size() % RG_MAX_TEMPORAL_LAYERS == 0);
  for (usize i = 0; i < m_physical_textures.size();
       i += RG_MAX_TEMPORAL_LAYERS) {
    RgPhysicalTexture &physical_texture = m_physical_textures[i];
    RgDebugName name = std::move(physical_texture.name);
    VkImageUsageFlags usage = physical_texture.usage;
    Handle<Texture> handle = physical_texture.handle;
    StorageTextureId storage_descriptor = physical_texture.storage_descriptor;
    RgTextureUsage state = physical_texture.state;
    usize last = i + 1;
    for (; last < i + RG_MAX_TEMPORAL_LAYERS; ++last) {
      RgPhysicalTexture &prev_physical_texture = m_physical_textures[last - 1];
      RgPhysicalTexture &cur_physical_texture = m_physical_textures[last];
      if (!cur_physical_texture.handle) {
        break;
      }
      prev_physical_texture.name = std::move(physical_texture.name);
      prev_physical_texture.usage = physical_texture.usage;
      prev_physical_texture.handle = physical_texture.handle;
      prev_physical_texture.storage_descriptor =
          physical_texture.storage_descriptor;
      prev_physical_texture.state = physical_texture.state;
    }
    RgPhysicalTexture &last_physical_texture = m_physical_textures[last - 1];
    last_physical_texture.name = std::move(physical_texture.name);
    last_physical_texture.usage = usage;
    last_physical_texture.handle = handle;
    last_physical_texture.storage_descriptor = storage_descriptor;
    last_physical_texture.state = state;
  }
}

} // namespace ren

namespace ren {

auto RgRuntime::get_buffer(RgBufferToken buffer) const -> const BufferView & {
  ren_assert(buffer);
  return m_rg->m_data->m_buffers[buffer];
}

auto RgRuntime::get_texture(RgTextureToken texture) const -> Handle<Texture> {
  ren_assert(texture);
  return m_rg->m_data->m_textures[texture];
}

auto RgRuntime::get_storage_texture_descriptor(RgTextureToken texture) const
    -> StorageTextureId {
  ren_assert(texture);
  StorageTextureId descriptor =
      m_rg->m_data->m_texture_storage_descriptors[texture];
  ren_assert(descriptor);
  return descriptor;
}

auto RgRuntime::get_texture_set() const -> VkDescriptorSet {
  return m_rg->m_texture_set;
}

auto RgRuntime::get_allocator() const -> UploadBumpAllocator & {
  return *m_rg->m_upload_allocator;
}

void RenderGraph::execute(CommandAllocator &cmd_alloc) {
  RgRuntime rt;
  rt.m_rg = this;

  Vector<VkCommandBufferSubmitInfo> batch_cmd_buffers;
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

  for (const RgPassRuntimeInfo &pass : m_data->m_passes) {
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

    if (pass.num_signal_semaphores > 0) {
      batch_signal_semaphores =
          Span(m_data->m_semaphore_submit_info)
              .subspan(pass.base_signal_semaphore, pass.num_signal_semaphores);
      submit_batch();
    }
  }

  submit_batch();
}

} // namespace ren
