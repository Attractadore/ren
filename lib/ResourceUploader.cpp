#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

static void generate_mipmaps(CommandBuffer &cmd, const TextureRef &texture) {
  int src_width = texture.desc.width;
  int src_height = texture.desc.height;
  int src_depth = texture.desc.depth;
  for (unsigned dst_level = 1; dst_level < texture.desc.mip_levels;
       ++dst_level) {
    auto src_level = dst_level - 1;
    auto dst_width = std::max(src_width / 2, 1);
    auto dst_height = std::max(src_height / 2, 1);
    auto dst_depth = std::max(src_depth / 2, 1);
    VkImageBlit region = {
        .srcSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = src_level,
                .layerCount = 1,
            },
        .srcOffsets = {{}, {src_width, src_height, src_depth}},
        .dstSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = dst_level,
                .layerCount = 1,
            },
        .dstOffsets = {{}, {dst_width, dst_height, dst_depth}},
    };
    cmd.blit(texture, texture, region);
    src_width = dst_width;
    src_height = dst_height;
    src_depth = dst_depth;

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = texture.handle,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = dst_level,
                .levelCount = 1,
                .layerCount = texture.desc.array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }
}

static void upload_texture(CommandBuffer &cmd, const BufferRef &src,
                           const TextureRef &dst) {
  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = dst.handle,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = dst.desc.mip_levels,
                .layerCount = dst.desc.array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }

  VkBufferImageCopy region = {
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = dst.desc.array_layers,
          },
      .imageExtent = {dst.desc.width, dst.desc.height, 1},
  };
  cmd.copy_buffer_to_image(src, dst, region);

  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = dst.handle,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = dst.desc.array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }

  generate_mipmaps(cmd, dst);
}

void ResourceUploader::stage_texture(Device &device,
                                     std::span<const std::byte> data,
                                     const TextureRef &texture) {
  auto staging_buffer = device.create_buffer({
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .heap = BufferHeap::Upload,
      .size = unsigned(data.size_bytes()),
  });

  auto *ptr = staging_buffer.map<std::byte>();
  ranges::copy(data, ptr);

  m_texture_srcs.push_back(staging_buffer);
  m_texture_dsts.push_back(texture);
}

auto ResourceUploader::record_upload(CommandAllocator &cmd_allocator)
    -> Optional<CommandBuffer> {
  if (m_buffer_srcs.empty() and m_texture_srcs.empty()) {
    return None;
  }

  auto cmd = cmd_allocator.allocate();
  cmd.begin();

  for (auto &&[src, dst] : zip(m_buffer_srcs, m_buffer_dsts)) {
    VkBufferCopy region = {
        .srcOffset = src.desc.offset,
        .dstOffset = dst.desc.offset,
        .size = src.desc.size,
    };
    cmd.copy_buffer(src, dst, region);
  }

  m_buffer_srcs.clear();
  m_buffer_dsts.clear();

  for (auto &&[src, dst] : zip(m_texture_srcs, m_texture_dsts)) {
    upload_texture(cmd, src, dst);
  }

  m_texture_srcs.clear();
  m_texture_dsts.clear();

  cmd.end();

  return cmd;
}

} // namespace ren
