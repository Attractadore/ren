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
  Vector<BufferView> m_buffer_srcs;
  Vector<BufferView> m_buffer_dsts;

  Vector<BufferView> m_texture_srcs;
  Vector<Handle<Texture>> m_texture_dsts;

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
        REN_SET_DEBUG_NAME("Staging buffer"),
        .heap = BufferHeap::Upload,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = size,
    }));

    auto *ptr = device.map_buffer<T>(staging_buffer);
    ranges::copy(data, ptr);

    m_buffer_srcs.push_back(staging_buffer);
    m_buffer_dsts.push_back(buffer.subbuffer(0, size));
  }

  void stage_texture(Device &device, ResourceArena &alloc,
                     std::span<const std::byte> data, Handle<Texture> texture);

  [[nodiscard]] auto record_upload(const Device &device,
                                   CommandAllocator &cmd_allocator)
      -> Optional<CommandBuffer>;
};

} // namespace ren
