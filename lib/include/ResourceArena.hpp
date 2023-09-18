#pragma once
#include "Buffer.hpp"
#include "Device.hpp"
#include "Support/HashSet.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  Device *m_device;
  std::tuple<HashSet<Handle<Ts>>...> m_resources;

private:
  template <typename T> auto get_type_arena() -> HashSet<Handle<T>> & {
    return std::get<HashSet<Handle<T>>>(m_resources);
  }

  template <typename T> void insert(Handle<T> handle) {
    get_type_arena<T>().insert(handle);
  }

  void destroy_handle(Handle<Buffer> buffer) {
    m_device->destroy_buffer(buffer);
  }

  void destroy_handle(Handle<Texture> texture) {
    m_device->destroy_texture(texture);
  }

  void destroy_handle(Handle<Sampler> sampler) {
    m_device->destroy_sampler(sampler);
  }

  void destroy_handle(Handle<Semaphore> semaphore) {
    m_device->destroy_semaphore(semaphore);
  }

  void destroy_handle(Handle<DescriptorPool> pool) {
    m_device->destroy_descriptor_pool(pool);
  }

  void destroy_handle(Handle<DescriptorSetLayout> layout) {
    m_device->destroy_descriptor_set_layout(layout);
  }

  void destroy_handle(Handle<PipelineLayout> layout) {
    m_device->destroy_pipeline_layout(layout);
  }

  void destroy_handle(Handle<GraphicsPipeline> pipeline) {
    m_device->destroy_graphics_pipeline(pipeline);
  }

  void destroy_handle(Handle<ComputePipeline> pipeline) {
    m_device->destroy_compute_pipeline(pipeline);
  }

  template <typename T> void clear() {
    auto &handles = get_type_arena<T>();
    for (auto handle : handles) {
      destroy_handle(handle);
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

  auto create_buffer(const BufferCreateInfo &&create_info) -> BufferView {
    BufferView view = m_device->create_buffer(std::move(create_info));
    insert(view.buffer);
    return view;
  }

  void destroy_buffer(Handle<Buffer> buffer) { destroy(buffer); }

  auto create_texture(const TextureCreateInfo &&create_info)
      -> Handle<Texture> {
    auto texture = m_device->create_texture(std::move(create_info));
    insert(texture);
    return texture;
  }

  void destroy_texture(Handle<Texture> texture) { destroy(texture); }

  auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Handle<Sampler> {
    auto sampler = m_device->create_sampler(std::move(create_info));
    insert(sampler);
    return sampler;
  }

  void destroy_sampler(Handle<Sampler> sampler) { destroy(sampler); }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Handle<Semaphore> {
    auto semaphore = m_device->create_semaphore(std::move(create_info));
    insert(semaphore);
    return semaphore;
  }

  void destroy_semaphore(Handle<Semaphore> semaphore) { destroy(semaphore); }

  auto create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info) {
    auto pool = m_device->create_descriptor_pool(std::move(create_info));
    insert(pool);
    return pool;
  }

  void destroy_descriptor_pool(Handle<DescriptorPool> pool) { destroy(pool); }

  auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info) {
    auto layout =
        m_device->create_descriptor_set_layout(std::move(create_info));
    insert(layout);
    return layout;
  }

  void destroy_descriptor_set_layout(Handle<DescriptorSetLayout> layout) {
    destroy(layout);
  }

  auto create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Handle<PipelineLayout> {
    auto layout = m_device->create_pipeline_layout(std::move(create_info));
    insert(layout);
    return layout;
  }

  void destroy_pipeline_layout(Handle<PipelineLayout> layout) {
    destroy(layout);
  }

  auto create_graphics_pipeline(const GraphicsPipelineCreateInfo &&create_info)
      -> Handle<GraphicsPipeline> {
    auto pipeline = m_device->create_graphics_pipeline(std::move(create_info));
    insert(pipeline);
    return pipeline;
  }

  void destroy_graphics_pipeline(Handle<GraphicsPipeline> pipeline) {
    destroy(pipeline);
  }

  auto create_compute_pipeline(const ComputePipelineCreateInfo &&create_info)
      -> Handle<ComputePipeline> {
    auto pipeline = m_device->create_compute_pipeline(std::move(create_info));
    insert(pipeline);
    return pipeline;
  }

  void destroy_compute_pipeline(Handle<ComputePipeline> pipeline) {
    destroy(pipeline);
  }

  template <typename T> void destroy(Handle<T> handle) {
    destroy_handle(handle);
    get_type_arena<T>().erase(handle);
  }

  void clear() { (clear<Ts>(), ...); }
};

using ResourceArenaBase =
    ResourceArenaImpl<Buffer, ComputePipeline, DescriptorPool,
                      DescriptorSetLayout, GraphicsPipeline, PipelineLayout,
                      Sampler, Semaphore, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
public:
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
