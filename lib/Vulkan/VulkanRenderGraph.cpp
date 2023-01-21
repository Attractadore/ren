#include "Vulkan/VulkanRenderGraph.hpp"
#include "Formats.inl"
#include "Support/Views.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <range/v3/range.hpp>
#include <range/v3/view.hpp>

namespace ren {
VulkanRenderGraph::Builder::Builder(VulkanDevice &device)
    : RenderGraph::Builder(&device) {}

void VulkanRenderGraph::Builder::addPresentNodes() {
  auto *vk_swapchain = static_cast<VulkanSwapchain *>(m_swapchain);

  auto acquire = addNode();
  acquire.setDesc("Vulkan: Acquire swapchain image");
  m_swapchain_image = acquire.addExternalTextureOutput(
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);
  setDesc(m_swapchain_image, "Vulkan: swapchain image");
  m_acquire_semaphore = create_semaphore();
  set_desc(m_acquire_semaphore, "Vulkan: swapchain image acquire semaphore");

  auto blit = addNode();
  blit.setDesc("Vulkan: Blit final image to swapchain");
  blit.addReadInput(m_final_image, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_BLIT_BIT);
  auto blitted_swapchain_image =
      blit.addWriteInput(m_swapchain_image, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_BLIT_BIT);
  setDesc(blitted_swapchain_image, "Vulkan: blitted swapchain image");
  blit.wait_semaphore(m_acquire_semaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
  blit.setCallback([=, final_image = m_final_image,
                    swapchain_image = m_swapchain_image,
                    acquire_semaphore = m_acquire_semaphore](CommandBuffer &cmd,
                                                             RenderGraph &rg) {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    vk_cmd->blit(rg.getTexture(final_image), rg.getTexture(swapchain_image));
  });

  auto present = addNode();
  present.setDesc(
      "Vulkan: Transition swapchain image to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR");
  present.addReadInput(blitted_swapchain_image, {},
                       VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
  m_present_semaphore = create_semaphore();
  set_desc(m_present_semaphore, "Vulkan: swapchain image present semaphore");
  present.signal_semaphore(m_present_semaphore, VK_PIPELINE_STAGE_2_NONE);
}

auto VulkanRenderGraph::Builder::create_render_graph(RenderGraph::Config config)
    -> std::unique_ptr<RenderGraph> {
  return std::make_unique<VulkanRenderGraph>(
      std::move(config), Config{.device = static_cast<VulkanDevice *>(m_device),
                                .swapchain_image = m_swapchain_image,
                                .acquire_semaphore = m_acquire_semaphore,
                                .present_semaphore = m_present_semaphore});
}

namespace {
VkImageLayout
getImageLayoutFromAccessesAndStages(VkAccessFlags2 accesses,
                                    VkPipelineStageFlags2 stages) {
  if (accesses & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  } else if (accesses & VK_ACCESS_2_TRANSFER_READ_BIT) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  } else if (accesses & VK_ACCESS_2_TRANSFER_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  } else if (stages & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT) {
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  } else if (accesses & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) {
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
  } else if (accesses & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  return VK_IMAGE_LAYOUT_UNDEFINED;
}
} // namespace

RGCallback VulkanRenderGraph::Builder::generateBarrierGroup(
    std::span<const BarrierConfig> configs) {
  auto textures = configs |
                  ranges::views::transform([](const BarrierConfig &config) {
                    return config.texture;
                  }) |
                  ranges::to<SmallVector<RGTextureID, 8>>;
  auto barriers = configs |
                  ranges::views::transform([](const BarrierConfig &config) {
                    return VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = config.src_stages,
                        .srcAccessMask = config.src_accesses,
                        .dstStageMask = config.dst_stages,
                        .dstAccessMask = config.dst_accesses,
                        .oldLayout = getImageLayoutFromAccessesAndStages(
                            config.src_accesses, config.src_stages),
                        .newLayout = getImageLayoutFromAccessesAndStages(
                            config.dst_accesses, config.dst_stages),
                    };
                  }) |
                  ranges::to<SmallVector<VkImageMemoryBarrier2, 8>>;

  return [textures = std::move(textures), barriers = std::move(barriers)](
             CommandBuffer &cmd, RenderGraph &rg) mutable {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    auto vk_device = vk_cmd->getDevice();
    auto vk_cmd_buffer = vk_cmd->get();

    for (auto &&[tex, barrier] : ranges::views::zip(textures, barriers)) {
      auto &&texture = rg.getTexture(tex);
      barrier.image = texture.handle.get();
      barrier.subresourceRange = {
          .aspectMask = getVkImageAspectFlags(texture.desc.format),
          .levelCount = texture.desc.mip_levels,
          .layerCount = texture.desc.array_layers,
      };
    }

    VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    };

    vk_device->CmdPipelineBarrier2(vk_cmd_buffer, &dependency_info);
  };
}

void VulkanRenderGraph::execute(CommandAllocator &cmd_allocator) {
  auto &vk_cmd_allocator =
      *static_cast<VulkanCommandAllocator *>(&cmd_allocator);
  auto &vk_swapchain = *static_cast<VulkanSwapchain *>(m_swapchain);

  auto acquire_semaphore = m_device->createBinarySemaphore();
  auto present_semaphore = m_device->createBinarySemaphore();
  vk_swapchain.acquireImage(acquire_semaphore.handle.get());
  setTexture(m_swapchain_image, vk_swapchain.getTexture());
  set_semaphore(m_acquire_semaphore, acquire_semaphore.handle.get());
  set_semaphore(m_present_semaphore, present_semaphore.handle.get());

  SmallVector<VulkanSubmit, 16> submits;
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffer_infos;
  SmallVector<unsigned, 16> cmd_buffer_counts;

  for (auto &batch : m_batches) {
    SmallVector<VulkanCommandBuffer *, 8> cmds;
    cmds.reserve(batch.barrier_cbs.size());

    for (auto &&[barrier_cb, pass_cb] :
         ranges::views::zip(batch.barrier_cbs, batch.pass_cbs)) {
      auto *cmd = vk_cmd_allocator.allocateVulkanCommandBuffer();
      if (barrier_cb) {
        barrier_cb(*cmd, *this);
      }
      if (pass_cb) {
        pass_cb(*cmd, *this);
      }
      cmd->close();
      cmd_buffer_infos.push_back(
          {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
           .commandBuffer = cmd->get()});
      cmds.push_back(cmd);
    }

    for (auto &semaphore :
         concat(batch.wait_semaphores, batch.signal_semaphores)) {
      semaphore.semaphore = get_semaphore(static_cast<RGSemaphoreID>(
          reinterpret_cast<uintptr_t>(semaphore.semaphore)));
    }

    submits.push_back({.wait_semaphores = batch.wait_semaphores,
                       .signal_semaphores = batch.signal_semaphores});

    cmd_buffer_counts.push_back(cmds.size());
  }

  auto *p_cmd_buffer_infos = cmd_buffer_infos.data();
  for (size_t i = 0; i < submits.size(); ++i) {
    unsigned cmd_cnt = cmd_buffer_counts[i];
    submits[i].command_buffers = {p_cmd_buffer_infos, cmd_cnt};
    p_cmd_buffer_infos += cmd_cnt;
  }

  m_device->graphicsQueueSubmit(submits);

  vk_swapchain.presentImage(present_semaphore.handle.get());
}
} // namespace ren
