#include "ResourceUploader.hpp"
#include "CommandRecorder.hpp"
#include "Renderer.hpp"

namespace ren {

namespace {

void generate_mipmaps(Renderer &renderer, CommandRecorder &cmd,
                      Handle<Texture> handle) {
  const auto &texture = renderer.get_texture(handle);
  auto src_size = glm::max(texture.size, {1, 1, 1});
  for (unsigned dst_level = 1; dst_level < texture.num_mip_levels;
       ++dst_level) {
    auto src_level = dst_level - 1;

    cmd.texture_barrier({
        .resource =
            {
                .handle = handle,
                .first_mip_level = src_level,
                .num_mip_levels = 1,
            },
        .src_stage_mask = rhi::PipelineStage::Transfer,
        .src_access_mask = rhi::Access::TransferWrite,
        .src_layout = rhi::ImageLayout::TransferDst,
        .dst_stage_mask = rhi::PipelineStage::Transfer,
        .dst_access_mask = rhi::Access::TransferRead,
        .dst_layout = rhi::ImageLayout::TransferSrc,
    });

    auto dst_size = glm::max(src_size / 2u, {1, 1, 1});
    VkImageBlit region = {
        .srcSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = src_level,
                .layerCount = texture.num_array_layers,
            },
        .dstSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = dst_level,
                .layerCount = texture.num_array_layers,
            },
    };
    std::memcpy(&region.srcOffsets[1], &src_size, sizeof(src_size));
    std::memcpy(&region.dstOffsets[1], &dst_size, sizeof(dst_size));
    cmd.blit(handle, handle, {region}, VK_FILTER_LINEAR);
    src_size = dst_size;
  }
}

void upload_texture(Renderer &renderer, CommandRecorder &cmd,
                    const BufferView &src, Handle<Texture> dst) {
  // Transfer all mip levels to TRANSFER_DST for upload + mipmap generation
  cmd.texture_barrier({
      .resource = {dst},
      .dst_stage_mask = rhi::PipelineStage::Transfer,
      .dst_access_mask = rhi::Access::TransferWrite,
      .dst_layout = rhi::ImageLayout::TransferDst,
  });

  cmd.copy_buffer_to_texture(src, dst);

  generate_mipmaps(renderer, cmd, dst);

  // Transfer last mip level from transfer destination to transfer source after
  // copy or mipmap generation.
  cmd.texture_barrier({
      .resource =
          {
              .handle = dst,
              .first_mip_level = renderer.get_texture(dst).num_mip_levels - 1,
              .num_mip_levels = 1,
          },
      .src_stage_mask = rhi::PipelineStage::Transfer,
      .src_access_mask = rhi::Access::TransferWrite,
      .src_layout = rhi::ImageLayout::TransferDst,
      .dst_stage_mask = rhi::PipelineStage::Transfer,
      .dst_access_mask = rhi::Access::TransferRead,
      .dst_layout = rhi::ImageLayout::TransferSrc,
  });

  // Transfer from transfer source to shader resource after mipmap generation.
  cmd.texture_barrier({
      .resource = {dst},
      .src_stage_mask = rhi::PipelineStage::Transfer,
      .src_access_mask = rhi::Access::TransferRead,
      .src_layout = rhi::ImageLayout::TransferSrc,
      .dst_stage_mask = rhi::PipelineStage::FragmentShader,
      .dst_access_mask = rhi::Access::ShaderImageRead,
      .dst_layout = rhi::ImageLayout::ShaderResource,
  });
}

} // namespace

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
                                     Span<const std::byte> data,
                                     Handle<Texture> texture) {
  usize size = data.size_bytes();
  auto [ptr, _, staging_buffer] = allocator.allocate(size);
  std::memcpy(ptr, data.data(), size);
  m_texture_copies.push_back({
      .src = staging_buffer,
      .dst = texture,
  });
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
    for (const auto &[src, dst] : m_texture_copies) {
      upload_texture(renderer, cmd, src, dst);
    }
    m_texture_copies.clear();
  }

  ren_try(rhi::CommandBuffer cmd_buffer, cmd.end());

  ren_try_to(renderer.submit(rhi::QueueFamily::Graphics, {cmd_buffer}));

  return {};
}

} // namespace ren
