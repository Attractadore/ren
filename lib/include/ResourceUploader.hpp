#pragma once
#include "CommandBuffer.hpp"
#include "RingBuffer.hpp"
#include "Support/Optional.hpp"
#include "Support/Vector.hpp"

namespace ren {

class CommandAllocator;

class ResourceUploader {
  Device *m_device;

  Optional<RingBuffer> m_ring_buffer;

  struct BufferCopy {
    BufferRef src;
    BufferRef dst;
    VkBufferCopy region;
  };

  Vector<BufferCopy> m_buffer_copies;

private:
  RingBuffer create_ring_buffer(unsigned size);

public:
  ResourceUploader(Device &device);

  void begin_frame();
  void end_frame();

  template <ranges::sized_range R>
  void stage_data(R &&data, const BufferRef &buffer);

  void upload_data(CommandAllocator &cmd_allocator);
};

} // namespace ren
