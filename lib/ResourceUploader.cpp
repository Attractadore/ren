#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

ResourceUploader::ResourceUploader(Device &device) : m_device(&device) {}

void ResourceUploader::next_frame() {
  if (m_ring_buffer) {
    m_ring_buffer->next_frame();
  }
}

RingBuffer ResourceUploader::create_ring_buffer(unsigned size) {
  return RingBuffer(m_device->create_buffer({
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .heap = BufferHeap::Upload,
      .size = size,
  }));
}

void ResourceUploader::upload_data(CommandAllocator &cmd_allocator) {
  if (m_buffer_copies.empty()) {
    m_ring_buffer = None;
    return;
  }

  auto cmd = cmd_allocator.allocate();
  cmd.begin();

  auto same_src_and_dsts = ranges::views::chunk_by(
      m_buffer_copies, [](const BufferCopy &lhs, const BufferCopy &rhs) {
        return lhs.src.handle == rhs.src.handle and
               lhs.dst.handle == rhs.dst.handle;
      });

  SmallVector<VkBufferCopy, 8> regions;
  for (auto &&copy_range : same_src_and_dsts) {
    regions.assign(map(copy_range, [](const BufferCopy &buffer_copy) {
      return buffer_copy.region;
    }));
    cmd.copy_buffer(copy_range.front().src, copy_range.front().dst, regions);
  }

  cmd.end();

  m_buffer_copies.clear();

  // TODO: submit command buffer and wait for signal in render graph
  todo();
}

} // namespace ren
