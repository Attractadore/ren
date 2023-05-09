#pragma once
#include "ResourceArena.hpp"
#include "ResourceUploader.hpp"
#include "Support/Views.hpp"

namespace ren {

template <ranges::sized_range R>
void ResourceUploader::stage_buffer(Device &device, ResourceArena &arena,
                                    R &&data, Handle<Buffer> buffer,
                                    size_t offset) {
  using T = ranges::range_value_t<R>;

  if (auto *ptr = device.get_buffer(buffer).map<T>(offset)) {
    ranges::copy(data, ptr);
    return;
  }

  auto staging_buffer = arena.create_buffer(
      {
          REN_SET_DEBUG_NAME("Staging buffer"),
          .heap = BufferHeap::Upload,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          .size = size_bytes(data),
      },
      device);

  auto *ptr = device.get_buffer(staging_buffer).template map<T>();
  ranges::copy(data, ptr);

  m_buffer_srcs.push_back(staging_buffer);
  m_buffer_dsts.push_back(buffer);
  m_buffer_dst_offsets.push_back(offset);
}

} // namespace ren
