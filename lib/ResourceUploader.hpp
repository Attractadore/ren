#pragma once
#include "Buffer.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"

#include <cstring>

namespace ren {

class Renderer;
class ResourceArena;
class CommandAllocator;

struct Texture;

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
  template <typename T>
  void stage_buffer(Renderer &renderer, ResourceArena &arena, Span<T> data,
                    const BufferView &buffer) {
    stage_buffer(renderer, arena, data.template reinterpret<const std::byte>(),
                 buffer);
  }

  void stage_buffer(Renderer &renderer, ResourceArena &arena,
                    Span<const std::byte> data, const BufferView &buffer);

  void stage_texture(Renderer &renderer, ResourceArena &alloc,
                     Span<const std::byte> data, Handle<Texture> texture);

  void upload(Renderer &renderer, CommandAllocator &cmd_allocator);
};

} // namespace ren
