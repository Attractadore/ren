#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "Support/Views.hpp"

namespace ren {

ResourceUploader::ResourceUploader(Device &device) : m_device(&device) {}

void ResourceUploader::begin_frame() {
  if (m_ring_buffer) {
    m_ring_buffer->begin_frame();
  }
}

void ResourceUploader::end_frame() {
  if (m_ring_buffer) {
    m_ring_buffer->end_frame();
  }
}

RingBuffer ResourceUploader::create_ring_buffer(unsigned size) {
  return RingBuffer(m_device->create_buffer({
      .usage = BufferUsage::TransferSRC,
      .location = BufferLocation::Host,
      .size = size,
  }));
}

void ResourceUploader::upload_data() {
  if (m_buffer_copies.empty()) {
    if (m_ring_buffer) {
      m_ring_buffer->end_frame();
      m_ring_buffer = None;
    }
    return;
  }

  auto *cmd = m_device->getCommandAllocator().allocateCommandBuffer();

  auto same_src_and_dsts = ranges::views::chunk_by(
      m_buffer_copies, [](const BufferCopy &lhs, const BufferCopy &rhs) {
        return lhs.src.handle == rhs.src.handle and
               lhs.dst.handle == rhs.dst.handle;
      });

  SmallVector<CopyRegion, 8> regions;
  for (auto &&copy_range : same_src_and_dsts) {
    regions.assign(map(copy_range, [](const BufferCopy &buffer_copy) {
      return buffer_copy.region;
    }));
    cmd->copy_buffer(copy_range.front().src, copy_range.front().dst, regions);
  }

  m_buffer_copies.clear();

  // TODO: submit command buffer and wait for signal in render graph
}

} // namespace ren
