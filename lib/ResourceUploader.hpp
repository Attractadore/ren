#pragma once
#include "Buffer.hpp"
#include "BumpAllocator.hpp"
#include "core/Span.hpp"
#include "core/Vector.hpp"

struct ktxTexture;
struct ktxTexture2;

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
    usize mip_offsets[MAX_SRV_MIPS] = {};
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

  auto create_texture(ResourceArena &arena, UploadBumpAllocator &allocator,
                      ktxTexture2 *ktx_texture)
      -> Result<Handle<Texture>, Error>;

  void stage_texture(UploadBumpAllocator &allocator, ktxTexture *ktx_texture,
                     Handle<Texture> texture);

  void stage_texture(UploadBumpAllocator &allocator, Span<const std::byte> data,
                     Handle<Texture> texture);

  auto upload(Arena scratch, Renderer &renderer, Handle<CommandPool> cmd_pool)
      -> Result<void, Error>;
};

} // namespace ren
