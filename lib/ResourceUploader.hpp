#pragma once
#include "ResourceArena.hpp"
#include "Support/Vector.hpp"
#include "Support/Views.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {

class CommandAllocator;

class ResourceUploader {
  struct BufferCopy {
    BufferView src;
    BufferView dst;
  };
  Vector<BufferCopy> m_buffer_copies;

  struct TextureCopy {
    BufferView src;
    Handle<Texture> dst;
  };
  Vector<TextureCopy> m_texture_copies;

public:
  template <typename T>
  void stage_buffer(ResourceArena &arena, Span<T> data,
                    const BufferView &buffer) {
    usize size = size_bytes(data);
    assert(size <= buffer.size);

    BufferView staging_buffer = arena.create_buffer({
        .name = "Staging buffer",
        .heap = BufferHeap::Staging,
        .size = size,
    });

    auto *ptr = g_renderer->map_buffer<std::remove_const_t<T>>(staging_buffer);
    ranges::copy(data, ptr);

    m_buffer_copies.push_back({
        .src = staging_buffer,
        .dst = buffer,
    });
  }

  void stage_texture(ResourceArena &alloc, Span<const std::byte> data,
                     Handle<Texture> texture);

  void upload(CommandAllocator &cmd_allocator);
};

} // namespace ren
