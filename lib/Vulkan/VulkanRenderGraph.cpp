#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanPipelineStages.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanSync.inl"
#include "Vulkan/VulkanTexture.hpp"

#include <range/v3/range.hpp>
#include <range/v3/view.hpp>

namespace ren {
void VulkanRenderGraphBuilder::addPresentNodes() {
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
  blit.setCallback(
      [=, final_image = m_final_image, swapchain_image = m_swapchain_image,
       acquire_semaphore = m_acquire_semaphore](CommandBuffer &cmd,
                                                RGResources &resources) {
        auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
        auto &&src_tex = resources.getTexture(final_image);
        auto &&dst_tex = resources.getTexture(swapchain_image);
        vk_cmd->wait(resources.getSyncObject(acquire_semaphore),
                     PipelineStage::Blit);
        vk_cmd->blit(src_tex, dst_tex);
      });

  auto present = addNode();
  present.setDesc(
      "Vulkan: Transition swapchain image to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR");
  present.addReadInput(blitted_swapchain_image, {}, PipelineStage::Present);
  m_present_semaphore = present.addSignalSync({.type = SyncType::Semaphore});
  setDesc(m_present_semaphore, "Vulkan: swapchain image present semaphore");
  present.setCallback([=, present_semaphore = m_present_semaphore](
                          CommandBuffer &cmd, RGResources &resources) {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    vk_cmd->signal(resources.getSyncObject(present_semaphore), {});
  });
}

std::unique_ptr<RenderGraph> VulkanRenderGraphBuilder::createRenderGraph(
    Vector<Batch> batches, HashMap<RGTextureID, Texture> textures,
    HashMap<RGTextureID, RGTextureID> texture_aliases,
    HashMap<RGSyncID, SyncObject> syncs) {
  return std::make_unique<VulkanRenderGraph>(
      m_swapchain, std::move(batches), std::move(textures),
      std::move(texture_aliases), std::move(syncs), m_swapchain_image,
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

PassCallback VulkanRenderGraphBuilder::generateBarrierGroup(
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
             CommandBuffer &cmd, RGResources &resources) mutable {
    auto *vk_cmd = static_cast<VulkanCommandBuffer *>(&cmd);
    auto vk_device = vk_cmd->getDevice();
    auto vk_cmd_buffer = vk_cmd->get();

    for (auto &&[tex, barrier] : ranges::views::zip(textures, barriers)) {
      auto &&texture = resources.getTexture(tex);
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
  vk_cmd_pool->addFrameResource(acquire_semaphore);
  vk_swapchain->acquireImage(getVkSemaphore(acquire_semaphore));
  m_resources.setTexture(m_swapchain_image, vk_swapchain->getTexture());
  m_resources.setSyncObject(m_acquire_semaphore, std::move(acquire_semaphore));

  SmallVector<VkSubmitInfo2, 16> submit_infos;
  SmallVector<VkCommandBufferSubmitInfo, 16> cmd_buffer_infos;
  SmallVector<VkSemaphoreSubmitInfo, 16> signal_semaphore_infos;
  SmallVector<unsigned, 16> cmd_buffer_counts;
  SmallVector<unsigned, 16> signal_semaphore_counts;

  for (auto &batch : m_batches) {
    VulkanCommandBuffer *cmd = nullptr;
    auto p_cmd_buffer_infos = cmd_buffer_infos.data() + cmd_buffer_infos.size();
    unsigned cmd_buffer_info_count = batch.pass_cbs.size();
    for (auto &&[barrier_cb, pass_cb] :
         ranges::views::zip(batch.barrier_cbs, batch.pass_cbs)) {
      cmd = vk_cmd_pool->allocateVulkanCommandBuffer();
      if (barrier_cb) {
        barrier_cb(*cmd, m_resources);
      }
      if (pass_cb) {
        pass_cb(*cmd, m_resources);
      }
      cmd->close();
      cmd_buffer_infos.push_back(
          {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
           .commandBuffer = cmd->get()});
    }
    assert(cmd);
    signal_semaphore_infos.append(cmd->getSignalSemaphores());

    cmd_buffer_counts.push_back(batch.pass_cbs.size());
    signal_semaphore_counts.push_back(cmd->getSignalSemaphores().size());

    submit_infos.push_back({
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount =
            static_cast<uint32_t>(cmd->getWaitSemaphores().size()),
        .pWaitSemaphoreInfos = cmd->getWaitSemaphores().data(),
    });
  }

  auto p_cmd_buffer_infos = cmd_buffer_infos.data();
  auto p_signal_semaphore_infos = signal_semaphore_infos.data();
  for (size_t i = 0; i < submit_infos.size(); ++i) {
    unsigned cmd_buffer_info_count = cmd_buffer_counts[i];
    unsigned signal_semaphore_count = signal_semaphore_counts[i];
    submit_infos[i].commandBufferInfoCount = cmd_buffer_info_count;
    submit_infos[i].pCommandBufferInfos = p_cmd_buffer_infos;
    submit_infos[i].signalSemaphoreInfoCount = signal_semaphore_count;
    submit_infos[i].pSignalSemaphoreInfos = p_signal_semaphore_infos;
    p_cmd_buffer_infos += cmd_buffer_info_count;
    p_signal_semaphore_infos += signal_semaphore_count;
  }

  vk_device->graphicsQueueSubmit(submit_infos);

  auto &&present_semaphore = m_resources.getSyncObject(m_present_semaphore);
  vk_swapchain->presentImage(getVkSemaphore(present_semaphore));
}
} // namespace ren
