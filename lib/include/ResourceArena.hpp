#pragma once
#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  Device *m_device;
  std::tuple<Vector<Handle<Ts>>...> m_resources;

private:
  template <typename T> auto get_type_arena() -> Vector<Handle<T>> & {
    return std::get<Vector<Handle<T>>>(m_resources);
  }

  template <typename T> void push_back(Handle<T> handle) {
    get_type_arena<T>().push_back(handle);
  }

  void destroy(Handle<Buffer> buffer) { destroy_buffer(buffer); }

  void destroy(Handle<Texture> texture) { destroy_texture(texture); }

  void destroy(Handle<Sampler> sampler) { destroy_sampler(sampler); }

  void destroy(Handle<Semaphore> semaphore) { destroy_semaphore(semaphore); }

  void destroy(Handle<DescriptorPool> pool) { destroy_descriptor_pool(pool); }

  void destroy(Handle<DescriptorSetLayout> layout) {
    destroy_descriptor_set_layout(layout);
  }

  void destroy(Handle<PipelineLayout> layout) {
    destroy_pipeline_layout(layout);
  }

  template <typename T> void clear() {
    auto &handles = get_type_arena<T>();
    for (auto handle : handles) {
      destroy(handle);
    }
    handles.clear();
  }

public:
  ResourceArenaImpl(Device &device) : m_device(&device) {}
  ResourceArenaImpl(ResourceArenaImpl &&) = default;
  ~ResourceArenaImpl() { clear(); }

  ResourceArenaImpl &operator=(ResourceArenaImpl &&other) noexcept {
    clear();
    m_device = other.m_device;
    m_resources = std::move(other.m_resources);
    return *this;
  }

  auto create_buffer(const BufferCreateInfo &&create_info) -> BufferHandleView {
    auto buffer = m_device->create_buffer(std::move(create_info));
    push_back(buffer.buffer);
    return buffer;
  }

  void destroy_buffer(Handle<Buffer> buffer) {
    m_device->destroy_buffer(buffer);
  }

  auto create_texture(const TextureCreateInfo &&create_info)
      -> TextureHandleView {
    auto texture = m_device->create_texture(std::move(create_info));
    push_back(texture.texture);
    return texture;
  }

  void destroy_texture(Handle<Texture> texture) {
    m_device->destroy_texture(texture);
  }

  auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Handle<Sampler> {
    auto sampler = m_device->create_sampler(std::move(create_info));
    push_back(sampler);
    return sampler;
  }

  void destroy_sampler(Handle<Sampler> sampler) {
    m_device->destroy_sampler(sampler);
  }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Handle<Semaphore> {
    auto semaphore = m_device->create_semaphore(std::move(create_info));
    push_back(semaphore);
    return semaphore;
  }

  void destroy_semaphore(Handle<Semaphore> semaphore) {
    m_device->destroy_semaphore(semaphore);
  }

  auto create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info) {
    auto pool = m_device->create_descriptor_pool(std::move(create_info));
    push_back(pool);
    return pool;
  }

  void destroy_descriptor_pool(Handle<DescriptorPool> pool) {
    m_device->destroy_descriptor_pool(pool);
  }

  auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info) {
    auto layout =
        m_device->create_descriptor_set_layout(std::move(create_info));
    push_back(layout);
    return layout;
  }

  void destroy_descriptor_set_layout(Handle<DescriptorSetLayout> layout) {
    m_device->destroy_descriptor_set_layout(layout);
  }

  auto create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Handle<PipelineLayout> {
    auto layout = m_device->create_pipeline_layout(std::move(create_info));
    push_back(layout);
    return layout;
  }

  void destroy_pipeline_layout(Handle<PipelineLayout> layout) {
    m_device->destroy_pipeline_layout(layout);
  }

  void clear() { (clear<Ts>(), ...); }
};

using ResourceArenaBase =
    ResourceArenaImpl<Buffer, DescriptorPool, DescriptorSetLayout,
                      PipelineLayout, Sampler, Semaphore, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
public:
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
