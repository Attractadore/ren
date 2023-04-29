#pragma once
#include "CommandBuffer.hpp"
#include "Support/Optional.hpp"
#include "Support/Vector.hpp"

namespace ren {

class CommandAllocator;

class ResourceUploader {
  Vector<BufferRef> m_buffer_srcs;
  Vector<BufferRef> m_buffer_dsts;

  Vector<BufferRef> m_texture_srcs;
  Vector<TextureRef> m_texture_dsts;

public:
  template <ranges::sized_range R>
  void stage_buffer(Device &device, R &&data, const BufferRef &buffer,
                    unsigned offset = 0);

  void stage_texture(Device &device, std::span<const std::byte> data,
                     const TextureRef &texture);

  [[nodiscard]] auto record_upload(CommandAllocator &cmd_allocator)
      -> Optional<CommandBuffer>;
};

} // namespace ren
