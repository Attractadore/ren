#pragma once
#include "Buffer.hpp"
#include "ren/core/Arena.hpp"
#include "ren/core/NotNull.hpp"

namespace ren {

struct Renderer;
struct Texture;
struct Semaphore;
struct Event;
struct GraphicsPipeline;
struct ComputePipeline;
struct CommandPool;

struct TextureCreateInfo;
struct SemaphoreCreateInfo;
struct SemaphoreCreateInfo;
struct GraphicsPipelineCreateInfo;
struct ComputePipelineCreateInfo;
struct CommandPoolCreateInfo;

struct ResourceArena {
  Arena *m_arena = nullptr;
  Renderer *m_renderer = nullptr;
  DynamicArray<Handle<Buffer>> m_buffers;
  DynamicArray<Handle<Texture>> m_textures;
  DynamicArray<Handle<Semaphore>> m_semaphores;
  DynamicArray<Handle<Event>> m_events;
  DynamicArray<Handle<GraphicsPipeline>> m_graphics_pipelines;
  DynamicArray<Handle<ComputePipeline>> m_compute_pipelines;
  DynamicArray<Handle<CommandPool>> m_cmd_pools;

public:
  [[nodiscard]] static ResourceArena init(NotNull<Arena *> arena,
                                          NotNull<Renderer *> renderer);

  template <typename T = std::byte>
  auto create_buffer(BufferCreateInfo create_info)
      -> Result<BufferSlice<T>, Error> {
    create_info.size = create_info.count * sizeof(T);
    ren_try(BufferView view, create_buffer(create_info));
    return BufferSlice<T>(view);
  }

  auto create_buffer(const BufferCreateInfo &create_info)
      -> Result<BufferView, Error>;

  auto create_texture(const TextureCreateInfo &create_info)
      -> Result<Handle<Texture>, Error>;

  auto create_semaphore(const SemaphoreCreateInfo &create_info)
      -> Result<Handle<Semaphore>, Error>;

  Handle<Event> create_event();

  auto create_graphics_pipeline(const GraphicsPipelineCreateInfo &create_info)
      -> Result<Handle<GraphicsPipeline>, Error>;

  auto create_compute_pipeline(const ComputePipelineCreateInfo &create_info)
      -> Result<Handle<ComputePipeline>, Error>;

  Result<Handle<CommandPool>, Error>
  create_command_pool(const CommandPoolCreateInfo &create_info);

  void clear();
};

} // namespace ren
