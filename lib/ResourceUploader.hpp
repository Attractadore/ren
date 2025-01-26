#pragma once
#include "Buffer.hpp"
#include "BumpAllocator.hpp"
#include "core/Span.hpp"
#include "core/Vector.hpp"

#include <cstring>

namespace ren {

class Renderer;

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
                    Span<T> data,
                    const BufferSlice<std::remove_const_t<T>> &slice) {
    stage_buffer(renderer, allocator, data.as_bytes(), BufferView(slice));
  }

  void stage_buffer(Renderer &renderer, UploadBumpAllocator &allocator,
                    Span<const std::byte> data, const BufferView &buffer);

  void stage_texture(Renderer &renderer, UploadBumpAllocator &allocator,
                     Span<const std::byte> data, Handle<Texture> texture);

  auto upload(Renderer &renderer, Handle<CommandPool> cmd_pool)
      -> Result<void, Error>;
};

} // namespace ren
