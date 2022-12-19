#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanPipelineStages.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanSync.hpp"
#include "Vulkan/VulkanTexture.hpp"

#include <range/v3/range.hpp>
#include <range/v3/view.hpp>

namespace ren {
void VulkanRenderGraph::Builder::addPresentNodes() {
  auto *vk_swapchain = static_cast<VulkanSwapchain *>(m_swapchain);

  auto acquire = addNode();
  acquire.setDesc("Vulkan: Acquire swapchain image");
  m_swapchain_image = acquire.addExternalTextureOutput(EmptyFlags, EmptyFlags);
  setDesc(m_swapchain_image, "Vulkan: swapchain image");
  m_acquire_semaphore = acquire.addExternalSignalSync();
  setDesc(m_acquire_semaphore, "Vulkan: swapchain image acquire semaphore");

  auto blit = addNode();
  blit.setDesc("Vulkan: Blit final image to swapchain");
  blit.addReadInput(m_final_image, MemoryAccess::TransferRead,
                    PipelineStage::Blit);
  auto blitted_swapchain_image = blit.addWriteInput(
      m_swapchain_image, MemoryAccess::TransferWrite, PipelineStage::Blit);
  setDesc(blitted_swapchain_image, "Vulkan: blitted swapchain image");
  blit.addWaitSync(m_acquire_semaphore);
  blit.setCallback([=, final_image = m_final_image,
                    swapchain_image = m_swapchain_image,
                    acquire_semaphore = m_acquire_semaphore](CommandBuffer &cmd,
                                                             RenderGraph &rg) {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    auto &&src_tex = rg.getTexture(final_image);
    auto &&dst_tex = rg.getTexture(swapchain_image);
    vk_cmd->wait(rg.getSyncObject(acquire_semaphore), PipelineStage::Blit);
    vk_cmd->blit(src_tex, dst_tex);
  });

  auto present = addNode();
  present.setDesc(
      "Vulkan: Transition swapchain image to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR");
  present.addReadInput(blitted_swapchain_image, {}, PipelineStage::Present);
  m_present_semaphore = present.addSignalSync({.type = SyncType::Semaphore});
  setDesc(m_present_semaphore, "Vulkan: swapchain image present semaphore");
  present.setCallback([=, present_semaphore = m_present_semaphore](
                          CommandBuffer &cmd, RenderGraph &rg) {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    vk_cmd->signal(rg.getSyncObject(present_semaphore), {});
  });
}

std::unique_ptr<RenderGraph> VulkanRenderGraph::Builder::createRenderGraph(
    Vector<Batch> batches, Vector<Texture> textures,
    HashMap<RGTextureID, unsigned> phys_textures, Vector<SyncObject> syncs) {
  return std::make_unique<VulkanRenderGraph>(
      m_swapchain, std::move(batches), std::move(textures),
      std::move(phys_textures), std::move(syncs), m_swapchain_image,
      m_acquire_semaphore, m_present_semaphore);
}

namespace {
VkImageLayout getImageLayoutFromAccessesAndStages(MemoryAccessFlags accesses,
                                                  PipelineStageFlags stages) {
  using enum MemoryAccess;
  using enum PipelineStage;
  if (accesses.isSet(ColorWrite)) {
    return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
  } else if (accesses.isSet(TransferRead)) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  } else if (accesses.isSet(TransferWrite)) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  } else if (stages.isSet(Present)) {
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  } else if (accesses.isSet(StorageWrite)) {
    return VK_IMAGE_LAYOUT_GENERAL;
  } else if (accesses.isSet(StorageRead)) {
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
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
  auto barriers =
      configs | ranges::views::transform([](const BarrierConfig &config) {
        return VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = getVkPipelineStageFlags(config.src_stages),
            .srcAccessMask = getVkAccessFlags(config.src_accesses),
            .dstStageMask = getVkPipelineStageFlags(config.dst_stages),
            .dstAccessMask = getVkAccessFlags(config.dst_accesses),
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
      barrier.image = getVkImage(texture);
      barrier.subresourceRange = {
          .aspectMask = getFormatAspectFlags(texture.desc.format),
          .levelCount = texture.desc.levels,
          .layerCount = texture.desc.layers,
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

void VulkanRenderGraph::execute(CommandAllocator *cmd_pool) {
  auto *vk_cmd_pool = static_cast<VulkanCommandAllocator *>(cmd_pool);
  auto *vk_device = vk_cmd_pool->getVulkanDevice();
  auto *vk_swapchain = static_cast<VulkanSwapchain *>(m_swapchain);

  auto acquire_semaphore =
      vk_device->createSyncObject({.type = SyncType::Semaphore});
  vk_swapchain->acquireImage(getVkSemaphore(acquire_semaphore));
  setTexture(m_swapchain_image, vk_swapchain->getTexture());
  setSyncObject(m_acquire_semaphore, std::move(acquire_semaphore));

  SmallVector<VulkanSubmit, 16> submits;
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffer_infos;
  SmallVector<unsigned, 16> cmd_buffer_counts;

  for (auto &batch : m_batches) {
    SmallVector<VulkanCommandBuffer *, 8> cmds;
    cmds.reserve(batch.barrier_cbs.size());

    for (auto &&[barrier_cb, pass_cb] :
         ranges::views::zip(batch.barrier_cbs, batch.pass_cbs)) {
      auto *cmd = vk_cmd_pool->allocateVulkanCommandBuffer();
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

    submits.push_back(
        {.wait_semaphores = cmds.front()->getWaitSemaphores(),
         .signal_semaphores = cmds.back()->getSignalSemaphores()});

    cmd_buffer_counts.push_back(cmds.size());
  }

  auto *p_cmd_buffer_infos = cmd_buffer_infos.data();
  for (size_t i = 0; i < submits.size(); ++i) {
    unsigned cmd_cnt = cmd_buffer_counts[i];
    submits[i].command_buffers = {p_cmd_buffer_infos, cmd_cnt};
    p_cmd_buffer_infos += cmd_cnt;
  }

  vk_device->graphicsQueueSubmit(submits);

  auto &&present_semaphore = getSyncObject(m_present_semaphore);
  vk_swapchain->presentImage(getVkSemaphore(present_semaphore));
}
} // namespace ren
