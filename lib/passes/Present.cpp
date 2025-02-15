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

  auto present = ccfg.rgb->create_pass({
      .name = "present",
      .queue = RgQueue::AsyncCompute,
  });

  struct Resources {
    Swapchain *swapchain = nullptr;
    RgSemaphoreId acquire_semaphore;
    RgTextureToken src;
  } rcs;

  rcs.swapchain = cfg.swapchain;
  present.wait_semaphore(ccfg.rcs->acquire_semaphore);
  rcs.acquire_semaphore = ccfg.rcs->acquire_semaphore;
  rcs.src = present.read_texture(cfg.src, rhi::TRANSFER_SRC_IMAGE);

  present.set_callback([rcs](Renderer &renderer, const RgRuntime &rg,
                             CommandRecorder &cmd) {
    Handle<Texture> src = rg.get_texture(rcs.src);

    Handle<Semaphore> acquire_semaphore =
        rg.get_semaphore(rcs.acquire_semaphore);
    Result<Handle<Texture>, Error> backbuffer =
        rcs.swapchain->acquire_texture(acquire_semaphore);
    ren_assert(backbuffer);

    cmd.texture_barrier({
        .resource = {*backbuffer},
        .dst_stage_mask = rhi::PipelineStage::Transfer,
        .dst_access_mask = rhi::Access::TransferWrite,
        .dst_layout = rhi::ImageLayout::TransferDst,
    });

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
    glm::uvec3 src_size = glm::max(renderer.get_texture(src).size, {1, 1, 1});
    std::memcpy(&region.srcOffsets[1], &src_size, sizeof(src_size));
    glm::uvec3 backbuffer_size =
        glm::max(renderer.get_texture(*backbuffer).size, {1, 1, 1});
    std::memcpy(&region.dstOffsets[1], &backbuffer_size,
                sizeof(backbuffer_size));
    cmd.blit(src, *backbuffer, {region}, VK_FILTER_LINEAR);

    cmd.texture_barrier({
        .resource = {*backbuffer},
        .src_stage_mask = rhi::PipelineStage::Transfer,
        .src_access_mask = rhi::Access::TransferWrite,
        .src_layout = rhi::ImageLayout::TransferDst,
        .dst_layout = rhi::ImageLayout::Present,
    });
  });

  present.signal_semaphore(ccfg.rcs->present_semaphore);

  rgb.set_external_semaphore(ccfg.rcs->acquire_semaphore,
                             cfg.acquire_semaphore);
  rgb.set_external_semaphore(ccfg.rcs->present_semaphore,
                             cfg.present_semaphore);
}
