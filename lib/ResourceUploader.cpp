#include "ResourceUploader.hpp"
#include "CommandAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

ResourceUploader::ResourceUploader(Device &device) : m_device(&device) {}

void ResourceUploader::next_frame() {
  if (m_ring_buffer) {
    m_ring_buffer->next_frame();
  }
}

RingBuffer ResourceUploader::create_ring_buffer(unsigned size) {
  return RingBuffer(m_device->create_buffer({
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .heap = BufferHeap::Upload,
      .size = size,
  }));
}

static void generate_mipmaps(Device &device, CommandBuffer &cmd,
                             const TextureRef &texture) {
  int src_width = texture.desc.width;
  int src_height = texture.desc.height;
  int src_depth = texture.desc.depth;
  for (unsigned dst_level = 1; dst_level < texture.desc.mip_levels;
       ++dst_level) {
    auto src_level = dst_level - 1;

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask =
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = texture.handle,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = src_level,
                .levelCount = 1,
                .layerCount = texture.desc.array_layers,
            },
    };
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    device.CmdPipelineBarrier2(cmd.get(), &dependency);

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
  }
}

static auto get_texture_transition_to_sampled_barriers(
    const TextureRef &texture,
    std::output_iterator<VkImageMemoryBarrier2> auto out) {
  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .image = texture.handle,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = texture.desc.array_layers,
          },
  };
  if (texture.desc.mip_levels > 1) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture.desc.mip_levels - 1;
    *out = barrier;
    ++out;
  }
  barrier.srcStageMask =
      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.subresourceRange.baseMipLevel = texture.desc.mip_levels - 1;
  barrier.subresourceRange.levelCount = 1;
  *out = barrier;
  ++out;
  return out;
}

void transition_textures_to_sampled(Device &device, CommandBuffer &cmd,
                                    std::span<const TextureRef> textures) {
  Vector<VkImageMemoryBarrier2> barriers;
  barriers.reserve(2 * textures.size());
  auto out = std::back_inserter(barriers);
  for (const auto &texture : textures) {
    get_texture_transition_to_sampled_barriers(texture, out);
  }
  VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = unsigned(barriers.size()),
      .pImageMemoryBarriers = barriers.data(),
  };
  device.CmdPipelineBarrier2(cmd.get(), &dependency);
}

void ResourceUploader::stage_data(std::span<const std::byte> data,
                                  const TextureRef &texture) {
  auto staging_buffer = m_device->create_buffer(BufferDesc{
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .heap = BufferHeap::Upload,
      .size = unsigned(data.size_bytes()),
  });

  auto *ptr = staging_buffer.map<std::byte>();
  ranges::copy(data, ptr);

  m_texture_srcs.push_back(staging_buffer);
  m_texture_dsts.push_back(texture);
}

auto ResourceUploader::upload_data(CommandAllocator &cmd_allocator)
    -> Optional<CommandBuffer> {
  if (m_buffer_copies.empty() and m_texture_srcs.empty()) {
    m_ring_buffer = None;
    return None;
  }

  auto cmd = cmd_allocator.allocate();
  cmd.begin();

  auto same_src_and_dsts = ranges::views::chunk_by(
      m_buffer_copies, [](const BufferCopy &lhs, const BufferCopy &rhs) {
        return lhs.src.handle == rhs.src.handle and
               lhs.dst.handle == rhs.dst.handle;
      });

  SmallVector<VkBufferCopy, 8> regions;
  for (auto &&copy_range : same_src_and_dsts) {
    regions.assign(map(copy_range, [](const BufferCopy &buffer_copy) {
      return buffer_copy.region;
    }));
    cmd.copy_buffer(copy_range.front().src, copy_range.front().dst, regions);
  }

  m_buffer_copies.clear();

  for (auto &&[src, dst] : zip(m_texture_srcs, m_texture_dsts)) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask =
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
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
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    m_device->CmdPipelineBarrier2(cmd.get(), &dependency);

    VkBufferImageCopy region = {
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = dst.desc.array_layers,
            },
        .imageExtent = {dst.desc.width, dst.desc.height, 1},
    };
    cmd.copy_buffer_to_image(src, dst, region);

    generate_mipmaps(*m_device, cmd, dst);
  }

  m_texture_srcs.clear();
  m_texture_dsts.clear();

  cmd.end();

  return cmd;
}

} // namespace ren
