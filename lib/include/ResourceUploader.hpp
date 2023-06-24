#pragma once
#include "CommandBuffer.hpp"
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
  template <ranges::sized_range R>
  void stage_buffer(Device &device, ResourceArena &arena, R &&data,
                    const BufferView &buffer) {
    using T = ranges::range_value_t<R>;

    if (auto *ptr = device.map_buffer<T>(buffer)) {
      ranges::copy(data, ptr);
      return;
    }

    auto size = size_bytes(data);

    auto staging_buffer = device.get_buffer_view(arena.create_buffer({
        .name = "Staging buffer",
        .heap = BufferHeap::Upload,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = size,
    }));

    auto *ptr = device.map_buffer<T>(staging_buffer);
    ranges::copy(data, ptr);

    m_buffer_copies.push_back(BufferCopy{
        .src = staging_buffer,
        .dst = buffer,
    });
  }

  void stage_texture(Device &device, ResourceArena &alloc,
                     std::span<const std::byte> data, Handle<Texture> texture);

  void upload(Device &device, CommandAllocator &cmd_allocator);
};

} // namespace ren
