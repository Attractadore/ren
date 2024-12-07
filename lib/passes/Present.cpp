#include "Present.hpp"
#include "../CommandRecorder.hpp"
#include "../Swapchain.hpp"

void ren::setup_present_pass(const PassCommonConfig &ccfg,
                             const PresentPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;

  if (!ccfg.rcs->acquire_semaphore) {
    ccfg.rcs->acquire_semaphore =
        ccfg.rgp->create_external_semaphore("acquire-semaphore");
  }

  if (!ccfg.rcs->present_semaphore) {
    ccfg.rcs->present_semaphore =
        ccfg.rgp->create_external_semaphore("present-semaphore");
  }

  auto present = ccfg.rgb->create_pass({.name = "present"});

  struct Resources {
    Swapchain *swapchain = nullptr;
    RgSemaphoreId acquire_semaphore;
    RgTextureToken src;
  } rcs;

  rcs.swapchain = cfg.swapchain;
  present.wait_semaphore(ccfg.rcs->acquire_semaphore);
  rcs.acquire_semaphore = ccfg.rcs->acquire_semaphore;
  rcs.src = present.read_texture(cfg.src, TRANSFER_SRC_TEXTURE);

  present.set_callback(
      [rcs](Renderer &renderer, const RgRuntime &rg, CommandRecorder &cmd) {
        Handle<Texture> src = rg.get_texture(rcs.src);

        Handle<Semaphore> acquire_semaphore =
            rg.get_semaphore(rcs.acquire_semaphore);
        Handle<Texture> backbuffer =
            rcs.swapchain->acquire_texture(acquire_semaphore);

        VkImageMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = renderer.get_texture(backbuffer).image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        };

        cmd.pipeline_barrier({}, {barrier});

        VkImageBlit region = {
            .srcSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
            .dstSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
        };
        glm::uvec3 src_size = renderer.get_texture(src).size;
        std::memcpy(&region.srcOffsets[1], &src_size, sizeof(src_size));
        glm::uvec3 backbuffer_size = renderer.get_texture(backbuffer).size;
        std::memcpy(&region.dstOffsets[1], &backbuffer_size,
                    sizeof(backbuffer_size));
        cmd.blit(src, backbuffer, {region}, VK_FILTER_LINEAR);

        barrier.srcStageMask = barrier.dstStageMask;
        barrier.srcAccessMask = barrier.dstAccessMask;
        barrier.dstStageMask = 0;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = barrier.newLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        cmd.pipeline_barrier({}, {barrier});
      });

  present.signal_semaphore(ccfg.rcs->present_semaphore);

  rgb.set_external_semaphore(ccfg.rcs->acquire_semaphore,
                             cfg.acquire_semaphore);
  rgb.set_external_semaphore(ccfg.rcs->present_semaphore,
                             cfg.present_semaphore);
}
