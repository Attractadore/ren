#include "ResourceArena.hpp"
#include "Renderer.hpp"

namespace ren {

ResourceArena ResourceArena::init(NotNull<Arena *> arena,
                                  NotNull<Renderer *> renderer) {
  return {
      .m_arena = arena,
      .m_renderer = renderer,
  };
};

Result<BufferView, Error>
ResourceArena::create_buffer(const BufferCreateInfo &create_info) {
  ren_try(Handle<Buffer> buffer, m_renderer->create_buffer(create_info));
  m_buffers.push(m_arena, buffer);
  return BufferView{
      .buffer = buffer,
      .count = create_info.size,
  };
}

auto ResourceArena::create_texture(const TextureCreateInfo &create_info)
    -> Result<Handle<Texture>, Error> {
  ren_try(Handle<Texture> texture, m_renderer->create_texture(create_info));
  m_textures.push(m_arena, texture);
  return texture;
}

auto ResourceArena::create_semaphore(const SemaphoreCreateInfo &create_info)
    -> Result<Handle<Semaphore>, Error> {
  ren_try(Handle<Semaphore> semaphore,
          m_renderer->create_semaphore(create_info));
  m_semaphores.push(m_arena, semaphore);
  return semaphore;
}

auto ResourceArena::create_event() -> Handle<Event> {
  Handle<Event> event = m_renderer->create_event();
  m_events.push(m_arena, event);
  return event;
}

auto ResourceArena::create_graphics_pipeline(
    const GraphicsPipelineCreateInfo &create_info)
    -> Result<Handle<GraphicsPipeline>, Error> {
  ren_try(Handle<GraphicsPipeline> pipeline,
          m_renderer->create_graphics_pipeline(create_info));
  m_graphics_pipelines.push(m_arena, pipeline);
  return pipeline;
}

auto ResourceArena::create_compute_pipeline(
    const ComputePipelineCreateInfo &create_info)
    -> Result<Handle<ComputePipeline>, Error> {
  ren_try(Handle<ComputePipeline> pipeline,
          m_renderer->create_compute_pipeline(create_info));
  m_compute_pipelines.push(m_arena, pipeline);
  return pipeline;
}

Result<Handle<CommandPool>, Error>
ResourceArena::create_command_pool(const CommandPoolCreateInfo &create_info) {
  ren_try(Handle<CommandPool> pool,
          m_renderer->create_command_pool(create_info));
  m_cmd_pools.push(m_arena, pool);
  return pool;
}

void ResourceArena::clear() {
  for (Handle<Buffer> buffer : m_buffers) {
    m_renderer->destroy(buffer);
  }
  for (Handle<Texture> texture : m_textures) {
    m_renderer->destroy(texture);
  }
  for (Handle<Semaphore> semaphore : m_semaphores) {
    m_renderer->destroy(semaphore);
  }
  for (Handle<Event> event : m_events) {
    m_renderer->destroy(event);
  }
  for (Handle<GraphicsPipeline> pipeline : m_graphics_pipelines) {
    m_renderer->destroy(pipeline);
  }
  for (Handle<ComputePipeline> pipeline : m_compute_pipelines) {
    m_renderer->destroy(pipeline);
  }
  for (Handle<CommandPool> pool : m_cmd_pools) {
    m_renderer->destroy(pool);
  }
  m_buffers.clear();
  m_textures.clear();
  m_semaphores.clear();
  m_events.clear();
  m_graphics_pipelines.clear();
  m_compute_pipelines.clear();
  m_cmd_pools.clear();
}

} // namespace ren
