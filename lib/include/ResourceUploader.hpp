#pragma once
#include "Device.hpp"
#include "ResourceArena.hpp"
#include "Support/Optional.hpp"
#include "Support/Vector.hpp"
#include "Support/Views.hpp"

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
  void stage_buffer(Device &device, ResourceArena &arena, Span<const T> data,
                    const BufferView &buffer) {
    usize size = size_bytes(data);
    assert(size <= buffer.size);

    BufferView staging_buffer = arena.create_buffer({
        .name = "Staging buffer",
        .heap = BufferHeap::Staging,
        .size = size,
    });

    auto *ptr = device.map_buffer<T>(staging_buffer);
    ranges::copy(data, ptr);

    m_buffer_copies.push_back({
        .src = staging_buffer,
        .dst = buffer,
    });
  }

  void stage_texture(Device &device, ResourceArena &alloc,
                     Span<const std::byte> data, Handle<Texture> texture);

  void upload(Device &device, CommandAllocator &cmd_allocator);
};

} // namespace ren
