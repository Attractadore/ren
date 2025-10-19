#pragma once
#include "Buffer.hpp"
#include "BumpAllocator.hpp"
#include "ren/core/Span.hpp"

struct ktxTexture;
struct ktxTexture2;

namespace ren {

class Renderer;

struct Texture;

struct ResourceUploader {
  struct BufferCopy {
    BufferView src;
    BufferView dst;
  };
  DynamicArray<BufferCopy> m_buffer_copies;

  struct TextureCopy {
    BufferView src;
    Handle<Texture> dst;
    usize mip_offsets[MAX_SRV_MIPS] = {};
  };
  DynamicArray<TextureCopy> m_texture_copies;

public:
  template <typename T>
  void stage_buffer(NotNull<Arena *> arena, Renderer &renderer,
                    UploadBumpAllocator &allocator, Span<T> data,
                    const BufferSlice<std::remove_const_t<T>> &slice) {
    stage_buffer(arena, renderer, allocator, data.as_bytes(),
                 BufferView(slice));
  }

  void stage_buffer(NotNull<Arena *> arena, Renderer &renderer,
                    UploadBumpAllocator &allocator, Span<const std::byte> data,
                    const BufferView &buffer);

  auto create_texture(NotNull<Arena *> arena, ResourceArena &rcs_arena,
                      UploadBumpAllocator &allocator, ktxTexture2 *ktx_texture)
      -> Result<Handle<Texture>, Error>;

  void stage_texture(NotNull<Arena *> arena, UploadBumpAllocator &allocator,
                     ktxTexture *ktx_texture, Handle<Texture> texture);

  void stage_texture(NotNull<Arena *> arena, UploadBumpAllocator &allocator,
                     Span<const std::byte> data, Handle<Texture> texture);

  auto upload(Renderer &renderer, Handle<CommandPool> cmd_pool)
      -> Result<void, Error>;
};

} // namespace ren
