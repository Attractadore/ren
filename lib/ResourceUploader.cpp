#include "ResourceUploader.hpp"
#include "CommandRecorder.hpp"
#include "Renderer.hpp"
#include "core/Views.hpp"

#include <ktx.h>

namespace ren {

void ResourceUploader::stage_buffer(Renderer &renderer,
                                    UploadBumpAllocator &allocator,
                                    Span<const std::byte> data,
                                    const BufferView &buffer) {
  usize size = data.size_bytes();
  ren_assert(size <= buffer.size_bytes());
  auto [ptr, _, staging_buffer] = allocator.allocate(size);
  std::memcpy(ptr, data.data(), size);
  m_buffer_copies.push_back({
      .src = staging_buffer,
      .dst = buffer,
  });
}

void ResourceUploader::stage_texture(Renderer &renderer,
                                     UploadBumpAllocator &allocator,
                                     ktxTexture *ktx_texture,
                                     Handle<Texture> texture) {
  auto staging = allocator.allocate(ktx_texture->dataSize);
  ktxTexture_LoadImageData(ktx_texture, (u8 *)staging.host_ptr,
                           ktx_texture->dataSize);
  TextureCopy &copy = m_texture_copies.emplace_back();
  copy = {
      .src = staging.slice,
      .dst = texture,
  };
  copy.mip_offsets.resize(ktx_texture->numLevels);
  for (u32 mip : range(ktx_texture->numLevels)) {
    ktxTexture_GetImageOffset(ktx_texture, mip, 0, 0, &copy.mip_offsets[mip]);
  }
}

void ResourceUploader::stage_texture(Renderer &renderer,
                                     UploadBumpAllocator &allocator,
                                     Span<const std::byte> data,
                                     Handle<Texture> texture) {
  auto staging = allocator.allocate(data.size());
  std::memcpy(staging.host_ptr, data.data(), data.size());
  TextureCopy &copy = m_texture_copies.emplace_back();
  copy = {
      .src = staging.slice,
      .dst = texture,
      .mip_offsets = {0},
  };
}

auto ResourceUploader::upload(Renderer &renderer, Handle<CommandPool> pool)
    -> Result<void, Error> {
  if (m_buffer_copies.empty() and m_texture_copies.empty()) {
    return {};
  }

  CommandRecorder cmd;
  ren_try_to(cmd.begin(renderer, pool));

  if (not m_buffer_copies.empty()) {
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

  if (not m_texture_copies.empty()) {
    auto _ = cmd.debug_region("upload-textures");
    SmallVector<TextureBarrier, 16> barriers(m_texture_copies.size());
    for (usize i : range(m_texture_copies.size())) {
      barriers[i] = {
          .resource = {m_texture_copies[i].dst},
          .dst_stage_mask = rhi::PipelineStage::Transfer,
          .dst_access_mask = rhi::Access::TransferWrite,
          .dst_layout = rhi::ImageLayout::TransferDst,
      };
    }
    cmd.pipeline_barrier({}, barriers);
    for (usize i : range(m_texture_copies.size())) {
      const TextureCopy &copy = m_texture_copies[i];
      const Texture &dst = renderer.get_texture(copy.dst);
      StaticVector<VkBufferImageCopy, MAX_SRV_MIPS> mip_copies(
          dst.num_mip_levels);
      for (u32 mip : range(dst.num_mip_levels)) {
        glm::uvec3 size = get_mip_size(dst.size, mip);
        mip_copies[mip] = {
            .bufferOffset = copy.src.offset + copy.mip_offsets[mip],
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip,
                    .layerCount = dst.num_array_layers,
                },
            .imageExtent = {size.x, size.y, size.z},
        };
      }
      cmd.copy_buffer_to_texture(copy.src.buffer, copy.dst, mip_copies);
      barriers[i] = {
          .resource = {copy.dst},
          .src_stage_mask = rhi::PipelineStage::Transfer,
          .src_access_mask = rhi::Access::TransferWrite,
          .src_layout = rhi::ImageLayout::TransferDst,
          .dst_stage_mask = rhi::PipelineStage::All,
          .dst_access_mask = rhi::Access::ShaderImageRead,
          .dst_layout = rhi::ImageLayout::ShaderResource,
      };
    }
    cmd.pipeline_barrier({}, barriers);
    m_texture_copies.clear();
  }

  ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());

  ren_try_to(renderer.submit(rhi::QueueFamily::Graphics, {cmd_buffer}));

  return {};
}

} // namespace ren
