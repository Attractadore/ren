#include "ResourceUploader.hpp"
#include "CommandRecorder.hpp"
#include "Renderer.hpp"

#include <ktx.h>

namespace ren {

void ResourceUploader::stage_buffer(NotNull<Arena *> arena, Renderer &renderer,
                                    UploadBumpAllocator &allocator,
                                    Span<const std::byte> data,
                                    const BufferView &buffer) {
  usize size = data.size_bytes();
  ren_assert(size <= buffer.size_bytes());
  auto [ptr, _, staging_buffer] = allocator.allocate(size);
  std::memcpy(ptr, data.data(), size);
  m_buffer_copies.push(arena, {
                                  .src = staging_buffer,
                                  .dst = buffer,
                              });
}

auto ResourceUploader::create_texture(NotNull<Arena *> arena,
                                      ResourceArena &rcs_arena,
                                      UploadBumpAllocator &allocator,
                                      ktxTexture2 *ktx_texture2)
    -> Result<Handle<Texture>, Error> {
  TinyImageFormat format = TinyImageFormat_FromVkFormat(
      (TinyImageFormat_VkFormat)ktx_texture2->vkFormat);
  if (!format) {
    return std::unexpected(Error::InvalidFormat);
  }
  ren_try(auto texture, rcs_arena.create_texture({
                            .format = format,
                            .usage = rhi::ImageUsage::ShaderResource |
                                     rhi::ImageUsage::TransferSrc |
                                     rhi::ImageUsage::TransferDst,
                            .width = ktx_texture2->baseWidth,
                            .height = ktx_texture2->baseHeight,
                            .depth = ktx_texture2->baseDepth,
                            .cube_map = ktx_texture2->numFaces > 1,
                            .num_mips = ktx_texture2->numLevels,
                        }));
  stage_texture(arena, allocator, ktxTexture(ktx_texture2), texture);
  return texture;
}

void ResourceUploader::stage_texture(NotNull<Arena *> arena,
                                     UploadBumpAllocator &allocator,
                                     ktxTexture *ktx_texture,
                                     Handle<Texture> texture) {
  auto staging = allocator.allocate(ktx_texture->dataSize);
  if (!ktx_texture->pData) {
    ktx_error_code_e err = ktxTexture_LoadImageData(
        ktx_texture, (u8 *)staging.host_ptr, ktx_texture->dataSize);
    ren_assert(!err);
  } else {
    std::memcpy(staging.host_ptr, ktx_texture->pData, ktx_texture->dataSize);
  }
  TextureCopy copy = {
      .src = staging.slice,
      .dst = texture,
  };
  for (u32 mip : range(ktx_texture->numLevels)) {
    ktxTexture_GetImageOffset(ktx_texture, mip, 0, 0, &copy.mip_offsets[mip]);
  }
  m_texture_copies.push(arena, copy);
}

void ResourceUploader::stage_texture(NotNull<Arena *> arena,
                                     UploadBumpAllocator &allocator,
                                     Span<const std::byte> data,
                                     Handle<Texture> texture) {
  auto staging = allocator.allocate(data.size());
  std::memcpy(staging.host_ptr, data.data(), data.size());
  TextureCopy copy = {
      .src = staging.slice,
      .dst = texture,
      .mip_offsets = {0},
  };
  m_texture_copies.push(arena, copy);
}

auto ResourceUploader::upload(Renderer &renderer, Handle<CommandPool> pool)
    -> Result<void, Error> {
  if (m_buffer_copies.m_size == 0 and m_texture_copies.m_size) {
    return {};
  }

  CommandRecorder cmd;
  ren_try_to(cmd.begin(renderer, pool));

  if (m_buffer_copies.m_size > 0) {
    auto _ = cmd.debug_region("upload-buffers");
    for (const auto &[src, dst] : m_buffer_copies) {
      cmd.copy_buffer(src, dst);
    }
    m_buffer_copies.clear();
    cmd.memory_barrier({
        .src_stage_mask = rhi::PipelineStage::Transfer,
        .src_access_mask = rhi::Access::TransferWrite,
        .dst_stage_mask = rhi::PipelineStage::All,
        .dst_access_mask = rhi::Access::MemoryRead,
    });
  }

  if (m_texture_copies.m_size > 0) {
    ScratchArena scratch;
    auto _ = cmd.debug_region("upload-textures");
    auto *barriers = scratch->allocate<TextureBarrier>(m_texture_copies.m_size);
    for (usize i : range(m_texture_copies.m_size)) {
      barriers[i] = {
          .resource = {m_texture_copies[i].dst},
          .dst_stage_mask = rhi::PipelineStage::Transfer,
          .dst_access_mask = rhi::Access::TransferWrite,
          .dst_layout = rhi::ImageLayout::TransferDst,
      };
    }
    cmd.pipeline_barrier({}, Span(barriers, m_texture_copies.m_size));
    for (usize i : range(m_texture_copies.m_size)) {
      const TextureCopy &copy = m_texture_copies[i];
      const Texture &dst = renderer.get_texture(copy.dst);
      for (u32 mip : range(dst.num_mips)) {
        cmd.copy_buffer_to_texture(copy.src.slice(copy.mip_offsets[mip]),
                                   copy.dst, mip, 1);
      }
      barriers[i] = {
          .resource = {copy.dst},
          .src_stage_mask = rhi::PipelineStage::Transfer,
          .src_access_mask = rhi::Access::TransferWrite,
          .src_layout = rhi::ImageLayout::TransferDst,
          .dst_stage_mask = rhi::PipelineStage::All,
          .dst_access_mask = rhi::Access::ShaderImageRead,
          .dst_layout = rhi::ImageLayout::General,
      };
    }
    cmd.pipeline_barrier({}, Span(barriers, m_texture_copies.m_size));
    m_texture_copies.clear();
  }

  ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());

  ren_try_to(renderer.submit(rhi::QueueFamily::Graphics, {cmd_buffer}));

  return {};
}

} // namespace ren
