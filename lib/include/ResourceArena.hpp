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

  void destroy(Device &device, Handle<Buffer> handle) {
    device.destroy_buffer(handle);
  }

  void destroy(Device &device, Handle<Texture> handle) {
    device.destroy_texture(handle);
  }

  template <typename T> void clear(Device &device, Vector<Handle<T>> &handles) {
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
    get_type_arena<Buffer>().push_back(buffer);
    return buffer;
  }

  void destroy_buffer(Device &device, Handle<Buffer> buffer) {
    destroy(device, buffer);
  }

  auto create_texture(const TextureCreateInfo &&create_info, Device &device)
      -> Handle<Texture> {
    auto texture = device.create_texture(std::move(create_info));
    get_type_arena<Texture>().push_back(texture);
    return texture;
  }

  void destroy_texture(Device &device, Handle<Texture> texture) {
    destroy(device, texture);
  }

  void clear(Device &device) { (clear(device, get_type_arena<Ts>()), ...); }
};

using ResourceArenaBase = ResourceArenaImpl<Buffer, Texture>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
public:
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
