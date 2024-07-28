#pragma once
#include "Buffer.hpp"
#include "BumpAllocator.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"

#include <cstring>

namespace ren {

class Renderer;
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
  void stage_buffer(Renderer &renderer, UploadBumpAllocator &allocator,
                    Span<T> data, const BufferView &buffer) {
    stage_buffer(renderer, allocator, data.as_bytes(), buffer);
  }

  void stage_buffer(Renderer &renderer, UploadBumpAllocator &allocator,
                    Span<const std::byte> data, const BufferView &buffer);

  void stage_texture(Renderer &renderer, UploadBumpAllocator &allocator,
                     Span<const std::byte> data, Handle<Texture> texture);

  void upload(Renderer &renderer, CommandAllocator &cmd_allocator);
};

} // namespace ren
