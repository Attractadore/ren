#pragma once
#include "Renderer.hpp"
#include "Support/Vector.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  template <typename T>
  static constexpr bool IsArenaResource = (std::same_as<T, Ts> or ...);

public:
  explicit ResourceArenaImpl(Renderer &renderer) { m_renderer = &renderer; }
  ResourceArenaImpl(const ResourceArenaImpl &) = delete;
  ResourceArenaImpl(ResourceArenaImpl &&) = default;
  ~ResourceArenaImpl() { clear(); }

  ResourceArenaImpl &operator=(const ResourceArenaImpl &) = delete;

  ResourceArenaImpl &operator=(ResourceArenaImpl &&other) {
    clear();
    m_renderer = other.m_renderer;
    m_resources = std::move(other.m_resources);
  }

  auto create_buffer(const BufferCreateInfo &&create_info) -> BufferView
    requires IsArenaResource<Buffer>
  {
    usize size = create_info.size;
    return {
        .buffer = insert(m_renderer->create_buffer(std::move(create_info))),
        .size = size,
    };
  }

  auto create_texture(const TextureCreateInfo &&create_info) -> Handle<Texture>
    requires IsArenaResource<Texture>
  {
    return insert(m_renderer->create_texture(std::move(create_info)));
  }

  auto create_sampler(const SamplerCreateInfo &&create_info) -> Handle<Sampler>
    requires IsArenaResource<Sampler>
  {
    return insert(m_renderer->create_sampler(std::move(create_info)));
  }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Handle<Semaphore>
    requires IsArenaResource<Semaphore>
  {
    return insert(m_renderer->create_semaphore(std::move(create_info)));
  }

  auto create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info)
    requires IsArenaResource<DescriptorPool>
  {
    return insert(m_renderer->create_descriptor_pool(std::move(create_info)));
  }

  auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info)
    requires IsArenaResource<DescriptorSetLayout>
  {
    return insert(
        m_renderer->create_descriptor_set_layout(std::move(create_info)));
  }

  auto create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Handle<PipelineLayout>
    requires IsArenaResource<PipelineLayout>
  {
    return insert(m_renderer->create_pipeline_layout(std::move(create_info)));
  }

  auto create_graphics_pipeline(const GraphicsPipelineCreateInfo &&create_info)
      -> Handle<GraphicsPipeline>
    requires IsArenaResource<GraphicsPipeline>
  {
    return insert(m_renderer->create_graphics_pipeline(std::move(create_info)));
  }

  auto create_compute_pipeline(const ComputePipelineCreateInfo &&create_info)
      -> Handle<ComputePipeline>
    requires IsArenaResource<ComputePipeline>
  {
    return insert(m_renderer->create_compute_pipeline(std::move(create_info)));
  }

  void clear() { (clear<Ts>(), ...); }

private:
  template <typename T> auto get_type_arena() -> Vector<Handle<T>> & {
    return std::get<Vector<Handle<T>>>(m_resources);
  }

  template <typename T> auto insert(Handle<T> handle) -> Handle<T> {
    return get_type_arena<T>().emplace_back(std::move(handle));
  }

  template <typename T> void clear() {
    Vector<Handle<T>> &arena = get_type_arena<T>();
    for (Handle<T> handle : arena) {
      m_renderer->destroy(handle);
    }
    arena.clear();
  }

private:
  Renderer *m_renderer = nullptr;
  std::tuple<Vector<Handle<Ts>>...> m_resources;
};

using ResourceArenaBase =
    ResourceArenaImpl<Buffer, ComputePipeline, DescriptorPool,
                      DescriptorSetLayout, GraphicsPipeline, PipelineLayout,
                      Sampler, Semaphore, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
