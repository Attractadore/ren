#pragma once
#include "CommandBuffer.hpp"
#include "Support/Optional.hpp"
#include "Support/Vector.hpp"

namespace ren {

class CommandAllocator;
class ResourceArena;

class ResourceUploader {
  Vector<BufferHandleView> m_buffer_srcs;
  Vector<BufferHandleView> m_buffer_dsts;

  Vector<BufferHandleView> m_texture_srcs;
  Vector<Handle<Texture>> m_texture_dsts;

public:
  template <ranges::sized_range R>
  void stage_buffer(Device &device, ResourceArena &alloc, R &&data,
                    BufferHandleView buffer);

  void stage_texture(Device &device, ResourceArena &alloc,
                     std::span<const std::byte> data, Handle<Texture> texture);

  [[nodiscard]] auto record_upload(const Device &device,
                                   CommandAllocator &cmd_allocator)
      -> Optional<CommandBuffer>;
};

} // namespace ren
