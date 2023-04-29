#pragma once
#include "Device.hpp"
#include "ResourceUploader.hpp"
#include "Support/Views.hpp"

namespace ren {

template <ranges::sized_range R>
void ResourceUploader::stage_buffer(Device &device, R &&data,
                                    const BufferRef &buffer, unsigned offset) {
  using T = ranges::range_value_t<R>;

  if (auto *ptr = buffer.map<T>(offset)) {
    ranges::copy(data, ptr);
    return;
  }

  auto staging_buffer = device.create_buffer({
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .heap = BufferHeap::Upload,
      .size = unsigned(size_bytes(data)),
  });

  auto *ptr = staging_buffer.map<T>();
  ranges::copy(data, ptr);

  m_buffer_srcs.push_back(staging_buffer);
  m_buffer_dsts.push_back(buffer.subbuffer(offset));
}

} // namespace ren
