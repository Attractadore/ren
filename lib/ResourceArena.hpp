#pragma once
#include "Renderer.hpp"
#include "core/Vector.hpp"

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
    return *this;
  }

  template <typename T = std::byte>
  auto create_buffer(BufferCreateInfo &&create_info)
      -> Result<BufferSlice<T>, Error>
    requires IsArenaResource<Buffer>
  {
    usize count = create_info.count;
    create_info.size = create_info.count * sizeof(T);
    ren_try(Handle<Buffer> buffer,
            m_renderer->create_buffer(std::move(create_info)));
    return BufferSlice<T>{
        .buffer = insert(buffer),
        .count = count,
    };
  }

  auto create_texture(const TextureCreateInfo &&create_info)
      -> Result<Handle<Texture>, Error>
    requires IsArenaResource<Texture>
  {
    return insert(m_renderer->create_texture(std::move(create_info)));
  }

  auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Result<Handle<Sampler>, Error>
    requires IsArenaResource<Sampler>
  {
    return insert(m_renderer->create_sampler(std::move(create_info)));
  }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Result<Handle<Semaphore>, Error>
    requires IsArenaResource<Semaphore>
  {
    return insert(m_renderer->create_semaphore(std::move(create_info)));
  }

  auto create_resource_descriptor_heap(
      const ResourceDescriptorHeapCreateInfo &&create_info)
    requires IsArenaResource<ResourceDescriptorHeap>
  {
    return insert(
        m_renderer->create_resource_descriptor_heap(std::move(create_info)));
  }

  auto create_sampler_descriptor_heap(
      const SamplerDescriptorHeapCreateInfo &&create_info)
    requires IsArenaResource<SamplerDescriptorHeap>
  {
    return insert(
        m_renderer->create_sampler_descriptor_heap(std::move(create_info)));
  }

  auto create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Result<Handle<PipelineLayout>, Error>
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

  void clear() {
    usize count = (get_type_arena<Ts>().size() + ...);
    if (count > 0) {
      m_renderer->wait_idle();
      (clear<Ts>(), ...);
    }
  }

private:
  template <typename T> auto get_type_arena() -> Vector<Handle<T>> & {
    return std::get<Vector<Handle<T>>>(m_resources);
  }

  template <typename T> auto insert(Handle<T> handle) -> Handle<T> {
    return get_type_arena<T>().emplace_back(std::move(handle));
  }

  template <typename T>
  auto insert(Result<Handle<T>, Error> handle) -> Result<Handle<T>, Error> {
    return handle.transform([&](Handle<T> handle) {
      return get_type_arena<T>().emplace_back(std::move(handle));
    });
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
    ResourceArenaImpl<Buffer, ComputePipeline, ResourceDescriptorHeap,
                      SamplerDescriptorHeap, GraphicsPipeline, PipelineLayout,
                      Sampler, Semaphore, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
