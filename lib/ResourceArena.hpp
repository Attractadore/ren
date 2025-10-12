#pragma once
#include "Renderer.hpp"
#include "core/Vector.hpp"

namespace ren {

namespace detail {

template <typename... Ts> class ResourceArenaImpl {
  template <typename T>
  static constexpr bool IsArenaResource = (std::same_as<T, Ts> or ...);

public:
  ResourceArenaImpl() = default;
  void init(Renderer *renderer) { m_renderer = renderer; }
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

  auto create_semaphore(const SemaphoreCreateInfo &&create_info)
      -> Result<Handle<Semaphore>, Error>
    requires IsArenaResource<Semaphore>
  {
    return insert(m_renderer->create_semaphore(std::move(create_info)));
  }

  auto create_event() -> Handle<Event>
    requires IsArenaResource<Event>
  {
    return insert(m_renderer->create_event());
  }

  auto create_graphics_pipeline(Arena scratch,
                                const GraphicsPipelineCreateInfo &&create_info)
      -> Result<Handle<GraphicsPipeline>, Error>
    requires IsArenaResource<GraphicsPipeline>
  {
    return insert(
        m_renderer->create_graphics_pipeline(scratch, std::move(create_info)));
  }

  auto create_compute_pipeline(Arena scratch,
                               const ComputePipelineCreateInfo &&create_info)
      -> Result<Handle<ComputePipeline>, Error>
    requires IsArenaResource<ComputePipeline>
  {
    return insert(
        m_renderer->create_compute_pipeline(scratch, std::move(create_info)));
  }

  auto create_command_pool(NotNull<Arena *> arena,
                           const CommandPoolCreateInfo &create_info)
    requires IsArenaResource<CommandPool>
  {
    return insert(
        m_renderer->create_command_pool(arena, std::move(create_info)));
  }

  template <typename H>
    requires IsArenaResource<H>
  void destroy(Handle<H> handle) {
    m_renderer->destroy(handle);
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
    ResourceArenaImpl<Buffer, ComputePipeline, GraphicsPipeline, Semaphore,
                      Event, Texture, CommandPool>;

} // namespace detail

class ResourceArena : public detail::ResourceArenaBase {
  using detail::ResourceArenaBase::ResourceArenaBase;
};

} // namespace ren
