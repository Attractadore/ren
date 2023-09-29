#pragma once
#include "Renderer.hpp"
#include "Support/HashSet.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  std::tuple<HashSet<Handle<Ts>>...> m_resources;

private:
  template <typename T> auto get_type_arena() -> HashSet<Handle<T>> & {
    return std::get<HashSet<Handle<T>>>(m_resources);
  }

  template <typename T> void insert(Handle<T> handle) {
    get_type_arena<T>().insert(handle);
  }

  void destroy_handle(Handle<Buffer> buffer) {
    g_renderer->destroy_buffer(buffer);
  }

  void destroy_handle(Handle<Texture> texture) {
    g_renderer->destroy_texture(texture);
  }

  void destroy_handle(Handle<Sampler> sampler) {
    g_renderer->destroy_sampler(sampler);
  }

  void destroy_handle(Handle<Semaphore> semaphore) {
    g_renderer->destroy_semaphore(semaphore);
  }

  void destroy_handle(Handle<DescriptorPool> pool) {
    g_renderer->destroy_descriptor_pool(pool);
  }

  void destroy_handle(Handle<DescriptorSetLayout> layout) {
    g_renderer->destroy_descriptor_set_layout(layout);
  }

  void destroy_handle(Handle<PipelineLayout> layout) {
    g_renderer->destroy_pipeline_layout(layout);
  }

  void destroy_handle(Handle<GraphicsPipeline> pipeline) {
    g_renderer->destroy_graphics_pipeline(pipeline);
  }

  void destroy_handle(Handle<ComputePipeline> pipeline) {
    g_renderer->destroy_compute_pipeline(pipeline);
  }

  template <typename T> void clear() {
    auto &handles = get_type_arena<T>();
    for (auto handle : handles) {
      destroy_handle(handle);
    }
    handles.clear();
  }

public:
  ResourceArenaImpl() = default;
  ResourceArenaImpl(ResourceArenaImpl &&) = default;
  ~ResourceArenaImpl() { clear(); }

  ResourceArenaImpl &operator=(ResourceArenaImpl &&other) noexcept {
    clear();
    m_resources = std::move(other.m_resources);
    return *this;
  }

  auto create_buffer(const BufferCreateInfo &&create_info) -> BufferView {
    BufferView view = g_renderer->create_buffer(std::move(create_info));
    insert(view.buffer);
    return view;
  }

  void destroy_buffer(Handle<Buffer> buffer) { destroy(buffer); }

  auto create_texture(const TextureCreateInfo &&create_info)
      -> Handle<Texture> {
    auto texture = g_renderer->create_texture(std::move(create_info));
    insert(texture);
    return texture;
  }

  void destroy_texture(Handle<Texture> texture) { destroy(texture); }

  auto create_sampler(const SamplerCreateInfo &&create_info)
      -> Handle<Sampler> {
    auto sampler = g_renderer->create_sampler(std::move(create_info));
    insert(sampler);
    return sampler;
  }

  void destroy_sampler(Handle<Sampler> sampler) { destroy(sampler); }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Handle<Semaphore> {
    auto semaphore = g_renderer->create_semaphore(std::move(create_info));
    insert(semaphore);
    return semaphore;
  }

  void destroy_semaphore(Handle<Semaphore> semaphore) { destroy(semaphore); }

  auto create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info) {
    auto pool = g_renderer->create_descriptor_pool(std::move(create_info));
    insert(pool);
    return pool;
  }

  void destroy_descriptor_pool(Handle<DescriptorPool> pool) { destroy(pool); }

  auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info) {
    auto layout =
        g_renderer->create_descriptor_set_layout(std::move(create_info));
    insert(layout);
    return layout;
  }

  void destroy_descriptor_set_layout(Handle<DescriptorSetLayout> layout) {
    destroy(layout);
  }

  auto create_pipeline_layout(const PipelineLayoutCreateInfo &&create_info)
      -> Handle<PipelineLayout> {
    auto layout = g_renderer->create_pipeline_layout(std::move(create_info));
    insert(layout);
    return layout;
  }

  void destroy_pipeline_layout(Handle<PipelineLayout> layout) {
    destroy(layout);
  }

  auto create_graphics_pipeline(const GraphicsPipelineCreateInfo &&create_info)
      -> Handle<GraphicsPipeline> {
    auto pipeline =
        g_renderer->create_graphics_pipeline(std::move(create_info));
    insert(pipeline);
    return pipeline;
  }

  void destroy_graphics_pipeline(Handle<GraphicsPipeline> pipeline) {
    destroy(pipeline);
  }

  auto create_compute_pipeline(const ComputePipelineCreateInfo &&create_info)
      -> Handle<ComputePipeline> {
    auto pipeline = g_renderer->create_compute_pipeline(std::move(create_info));
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
