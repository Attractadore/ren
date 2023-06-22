#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "ResourceArena.hpp"
#include "Support/Views.hpp"

namespace ren {

static void generate_mipmaps(const Device &device, CommandBuffer &cmd,
                             Handle<Texture> handle) {
  const auto &texture = device.get_texture(handle);
  auto src_size = texture.size;
  for (unsigned dst_level = 1; dst_level < texture.num_mip_levels;
       ++dst_level) {
    auto src_level = dst_level - 1;
    auto dst_size = glm::max(src_size / 2u, glm::uvec3(1));
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
    cmd.blit(handle, handle, region, VK_FILTER_LINEAR);
    src_size = dst_size;

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = texture.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = dst_level,
                .levelCount = 1,
                .layerCount = texture.num_array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }
}

static void upload_texture(const Device &device, CommandBuffer &cmd,
                           const BufferView &src, Handle<Texture> dst_handle) {
  const auto &dst = device.get_texture(dst_handle);
  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = dst.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = dst.num_mip_levels,
                .layerCount = dst.num_array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }

  VkBufferImageCopy region = {
      .bufferOffset = src.offset,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = dst.num_array_layers,
          },
  };
  std::memcpy(&region.imageExtent, &dst.size, sizeof(dst.size));
  cmd.copy_buffer_to_image(src.buffer, dst_handle, region);

  {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = dst.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = dst.num_array_layers,
            },
    };
    cmd.pipeline_barrier({}, asSpan(barrier));
  }

  generate_mipmaps(device, cmd, dst_handle);
}

void ResourceUploader::stage_texture(Device &device, ResourceArena &arena,
                                     std::span<const std::byte> data,
                                     Handle<Texture> texture) {
  auto staging_buffer = device.get_buffer_view(arena.create_buffer({
      .name = "Staging buffer",
      .heap = BufferHeap::Upload,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .size = data.size_bytes(),
  }));

  auto *ptr = device.map_buffer(staging_buffer);
  ranges::copy(data, ptr);

  m_texture_srcs.push_back(staging_buffer);
  m_texture_dsts.push_back(texture);
}

auto ResourceUploader::record_upload(const Device &device,
                                     CommandAllocator &cmd_allocator)
    -> Optional<CommandBuffer> {
  if (m_buffer_srcs.empty() and m_texture_srcs.empty()) {
    return None;
  }

  auto cmd = cmd_allocator.allocate();
  cmd.begin();

  cmd.begin_debug_region("Upload buffers");
  for (auto &&[src, dst] : zip(m_buffer_srcs, m_buffer_dsts)) {
    cmd.copy_buffer(src, dst);
  }
  VkMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
      .dstAccessMask =
          VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
  };
  cmd.pipeline_barrier(asSpan(barrier), {});
  cmd.end_debug_region();

  m_buffer_srcs.clear();
  m_buffer_dsts.clear();

  cmd.begin_debug_region("Upload textures");
  for (auto &&[src, dst] : zip(m_texture_srcs, m_texture_dsts)) {
    upload_texture(device, cmd, src, dst);
  }
  cmd.end_debug_region();

  m_texture_srcs.clear();
  m_texture_dsts.clear();

  cmd.end();

  return cmd;
}

} // namespace ren
