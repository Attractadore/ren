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

  struct TextureCopy {
    BufferRef src;
    TextureRef dst;
    VkBufferImageCopy region;
  };

  Vector<BufferCopy> m_buffer_copies;

  Vector<BufferRef> m_texture_srcs;
  Vector<TextureRef> m_texture_dsts;

private:
  RingBuffer create_ring_buffer(unsigned size);

public:
  ResourceUploader(Device &device);

  void next_frame();

  template <ranges::sized_range R>
  void stage_data(R &&data, const BufferRef &buffer, unsigned offset = 0);

  void stage_data(std::span<const std::byte> data, const TextureRef &texture);

  auto get_staged_textures() const -> std::span<const TextureRef> {
    return m_texture_dsts;
  }

  [[nodiscard]] auto upload_data(CommandAllocator &cmd_allocator)
      -> Optional<CommandBuffer>;
};

void transition_textures_to_sampled(Device &device, CommandBuffer &cmd,
                                    std::span<const TextureRef> textures);

} // namespace ren
