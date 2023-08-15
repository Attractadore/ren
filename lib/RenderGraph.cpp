#include "RenderGraph.hpp"
#include "Support/Views.hpp"

namespace ren {

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

auto RgRuntime::get_buffer(RgRtBuffer buffer) const -> const BufferView & {
  assert(buffer);
  todo();
}

auto RgRuntime::get_texture(RgRtTexture texture) const -> Handle<Texture> {
  assert(texture);
  todo();
}

auto RgRuntime::get_storage_texture_descriptor(RgRtTexture texture) const
    -> StorageTextureID {
  assert(texture);
  todo();
}

auto RenderGraph::is_pass_valid(StringView pass) -> bool { todo(); }

void RenderGraph::execute(CommandAllocator &cmd_alloc) { todo(); }

#if 0

void RenderGraph::execute(Device &device, CommandAllocator &cmd_allocator) {
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffers;
  SmallVector<VkSemaphoreSubmitInfo> wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> signal_semaphores;

  for (auto &batch : m_batches) {
    cmd_buffers.clear();

    for (auto index : range(batch.pass_cbs.size())) {
      const auto &pass_cb = batch.pass_cbs[index];

      auto cmd_buffer = cmd_allocator.allocate();
      {
        CommandRecorder cmd(device, cmd_buffer);
        if (pass_cb) {
#if REN_RG_DEBUG_NAMES
          const auto &pass_name = batch.pass_names[index];
          auto _ = cmd.debug_region(pass_name.c_str());
#endif
          pass_cb(device, m_runtime, cmd);
        }
      }

      cmd_buffers.push_back({
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = cmd_buffer,
      });
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

  retain_temporal_resources();
}

void RenderGraph::retain_temporal_resources() {
  m_batches.clear();

  m_swapchain = nullptr;
  m_present_semaphore = {};

  auto &buffers = m_runtime.m_buffers;
  buffers.erase_if([&](RGBufferID buffer, unsigned) {
    return not m_temporal_buffers.contains(buffer);
  });

  auto &physical_buffers = m_runtime.m_physical_buffers;
  auto old_physical_buffers = std::move(physical_buffers);
  auto old_buffer_states = std::move(m_buffer_states);
  physical_buffers.resize(m_temporal_buffers.size());
  m_buffer_states.resize(m_temporal_buffers.size());
  for (auto &&[idx, b] : buffers | enumerate) {
    auto &[buffer, physical_buffer] = b;
    auto &view = old_physical_buffers[physical_buffer];
    physical_buffers[idx] = std::exchange(view, BufferView());
    m_buffer_states[idx] = old_buffer_states[physical_buffer];
    physical_buffer = idx;
  }

  for (const auto &[physical_buffer, view] : old_physical_buffers | enumerate) {
    if (not m_external_buffers.contains(physical_buffer)) {
      m_arena.destroy_buffer(view.buffer);
    }
  }

  m_temporal_buffers.clear();
  m_external_buffers.clear();

  auto &textures = m_runtime.m_textures;
  textures.erase_if([&](RGTextureID texture, unsigned) {
    return not m_temporal_textures.contains(texture);
  });

  auto &physical_textures = m_runtime.m_physical_textures;
  auto old_physical_textures = std::move(physical_textures);
  auto old_texture_states = std::move(m_texture_states);
  physical_textures.resize(m_temporal_textures.size());
  m_texture_states.resize(m_temporal_textures.size());
  for (auto &&[idx, b] : textures | enumerate) {
    auto &[texture, physical_texture] = b;
    auto &view = old_physical_textures[physical_texture];
    physical_textures[idx] = std::exchange(view, TextureView());
    m_texture_states[idx] = old_texture_states[physical_texture];
    physical_texture = idx;
  }

  for (const auto &[physical_texture, view] :
       old_physical_textures | enumerate) {
    if (not m_external_textures.contains(physical_texture)) {
      m_arena.destroy_texture(view.texture);
    }
  }

  m_temporal_textures.clear();
  m_external_textures.clear();

#if REN_RG_DEBUG_NAMES
  for (auto &[_, name] : m_buffer_names) {
    name = fmt::format("{} (from previous frame)", name);
  }
  for (auto &[_, name] : m_texture_names) {
    name = fmt::format("{} (from previous frame)", name);
  }
#endif
}

#endif

} // namespace ren
