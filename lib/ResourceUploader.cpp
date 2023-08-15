#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "ResourceArena.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

namespace {

void generate_mipmaps(const Device &device, CommandRecorder &cmd,
                      Handle<Texture> handle) {
  const auto &texture = device.get_texture(handle);
  auto src_size = texture.size;
  for (unsigned dst_level = 1; dst_level < texture.num_mip_levels;
       ++dst_level) {
    auto src_level = dst_level - 1;

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = texture.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = src_level,
                .levelCount = 1,
                .layerCount = texture.num_array_layers,
            },
    };
    cmd.pipeline_barrier({}, {barrier});

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
    cmd.blit(handle, handle, {region}, VK_FILTER_LINEAR);
    src_size = dst_size;
  }
}

void upload_texture(const Device &device, CommandRecorder &cmd,
                    const BufferView &src, Handle<Texture> dst) {
  const auto &texture = device.get_texture(dst);

  {
    // Transfer all mip levels to TRANSFER_DST for upload + mipmap generation
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = texture.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = texture.num_mip_levels,
                .layerCount = texture.num_array_layers,
            },
    };
    cmd.pipeline_barrier({}, {barrier});
  }

  // Copy data to first mip level
  VkBufferImageCopy region = {
      .bufferOffset = src.offset,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = texture.num_array_layers,
          },
  };
  std::memcpy(&region.imageExtent, &texture.size, sizeof(texture.size));
  cmd.copy_buffer_to_image(src.buffer, dst, {region});

  generate_mipmaps(device, cmd, dst);

  // Transfer last mip level from TRANSFER_DST to READ_ONLY after copy or mipmap
  // generation
  StaticVector<VkImageMemoryBarrier2, 2> barriers = {{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
      .image = texture.image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = texture.num_mip_levels - 1,
              .levelCount = 1,
              .layerCount = texture.num_array_layers,
          },
  }};
  if (texture.num_mip_levels > 1) {
    // Transfer every mip level except the last one from TRANSFER_SRC to
    // READ_ONLY after mipmap generation
    barriers.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        .image = texture.image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = texture.num_mip_levels - 1,
                .layerCount = texture.num_array_layers,
            },
    });
  }
  cmd.pipeline_barrier({}, barriers);
}

} // namespace

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

  m_texture_copies.push_back(TextureCopy{
      .src = staging_buffer,
      .dst = texture,
  });
}

void ResourceUploader::upload(Device &device, CommandAllocator &cmd_allocator) {
  if (m_buffer_copies.empty() and m_texture_copies.empty()) {
    return;
  }

  auto cmd_buffer = cmd_allocator.allocate();
  {
    CommandRecorder cmd(device, cmd_buffer);

    if (not m_buffer_copies.empty()) {
      auto _ = cmd.debug_region("Upload buffers");
      for (const auto &[src, dst] : m_buffer_copies) {
        cmd.copy_buffer(src, dst);
      }
      VkMemoryBarrier2 barrier = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                          VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask =
              VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      };
      cmd.pipeline_barrier({barrier}, {});
      m_buffer_copies.clear();
    }

    if (not m_texture_copies.empty()) {
      auto _ = cmd.debug_region("Upload textures");
      for (const auto &[src, dst] : m_texture_copies) {
        upload_texture(device, cmd, src, dst);
      }
      m_texture_copies.clear();
    }
  }

  device.graphicsQueueSubmit({{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd_buffer,
  }});
}

} // namespace ren
