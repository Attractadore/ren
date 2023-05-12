#pragma once
#include "Buffer.hpp"
#include "Device.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  std::tuple<Vector<Handle<Ts>>...> m_resources;

private:
  template <typename T> auto get_type_arena() -> Vector<Handle<T>> & {
    return std::get<Vector<Handle<T>>>(m_resources);
  }

  template <typename T> void push_back(Handle<T> handle) {
    get_type_arena<T>().push_back(handle);
  }

  void destroy(Device &device, Handle<Buffer> buffer) {
    destroy_buffer(device, buffer);
  }

  void destroy(Device &device, Handle<Texture> texture) {
    destroy_texture(device, texture);
  }

  void destroy(Device &device, Handle<Sampler> sampler) {
    destroy_sampler(device, sampler);
  }

  void destroy(Device &device, Handle<Semaphore> semaphore) {
    destroy_semaphore(device, semaphore);
  }

  void destroy(Device &device, Handle<DescriptorPool> pool) {
    destroy_descriptor_pool(device, pool);
  }

  void destroy(Device &device, Handle<DescriptorSetLayout> layout) {
    destroy_descriptor_set_layout(device, layout);
  }

  template <typename T> void clear(Device &device) {
    auto &handles = get_type_arena<T>();
    for (auto handle : handles) {
      destroy(device, handle);
    }
    handles.clear();
  }

public:
  ResourceArenaImpl() = default;
  ResourceArenaImpl(ResourceArenaImpl &&) = default;
  ~ResourceArenaImpl() { (assert(get_type_arena<Ts>().empty()), ...); }

  ResourceArenaImpl &operator=(ResourceArenaImpl &&);

  auto create_buffer(const BufferCreateInfo &&create_info, Device &device)
      -> BufferHandleView {
    auto buffer = device.create_buffer(std::move(create_info));
    push_back(buffer.buffer);
    return buffer;
  }

  void destroy_buffer(Device &device, Handle<Buffer> buffer) {
    device.destroy_buffer(buffer);
  }

  auto create_texture(const TextureCreateInfo &&create_info, Device &device)
      -> TextureHandleView {
    auto texture = device.create_texture(std::move(create_info));
    push_back(texture.texture);
    return texture;
  }

  void destroy_texture(Device &device, Handle<Texture> texture) {
    device.destroy_texture(texture);
  }

  auto create_sampler(const SamplerCreateInfo &&create_info, Device &device)
      -> Handle<Sampler> {
    auto sampler = device.create_sampler(std::move(create_info));
    push_back(sampler);
    return sampler;
  }

  void destroy_sampler(Device &device, Handle<Sampler> sampler) {
    device.destroy_sampler(sampler);
  }

  auto create_semaphore(const SemaphoreCreateInfo &&create_info, Device &device)
      -> Handle<Semaphore> {
    auto semaphore = device.create_semaphore(std::move(create_info));
    push_back(semaphore);
    return semaphore;
  }

  void destroy_semaphore(Device &device, Handle<Semaphore> semaphore) {
    device.destroy_semaphore(semaphore);
  }

  auto create_descriptor_pool(const DescriptorPoolCreateInfo &&create_info,
                              Device &device) {
    auto pool = device.create_descriptor_pool(std::move(create_info));
    push_back(pool);
    return pool;
  }

  auto create_descriptor_set_layout(
      const DescriptorSetLayoutCreateInfo &&create_info, Device &device) {
    auto layout = device.create_descriptor_set_layout(std::move(create_info));
    push_back(layout);
    return layout;
  }

  void destroy_descriptor_pool(Device &device, Handle<DescriptorPool> pool) {
    device.destroy_descriptor_pool(pool);
  }

  void destroy_descriptor_set_layout(Device &device,
                                     Handle<DescriptorSetLayout> layout) {
    device.destroy_descriptor_set_layout(layout);
  }

  void clear(Device &device) { (clear<Ts>(device), ...); }
};

using ResourceArenaBase =
    ResourceArenaImpl<Buffer, DescriptorPool, DescriptorSetLayout, Sampler,
                      Semaphore, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
public:
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
