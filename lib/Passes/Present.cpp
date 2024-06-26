#include "Passes/Present.hpp"
#include "CommandRecorder.hpp"
#include "Swapchain.hpp"

void ren::setup_present_pass(const PassCommonConfig &ccfg,
                             const PresentPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;

  if (!ccfg.rcs->backbuffer) {
    glm::uvec2 size = cfg.swapchain->get_size();
    VkFormat format = cfg.swapchain->get_format();
    ccfg.rcs->backbuffer = ccfg.rgp->create_texture({
        .name = "backbuffer",
        .format = format,
        .width = size.x,
        .height = size.y,
        .ext =
            RgTextureExternalInfo{
                .usage = cfg.swapchain->get_usage(),
            },
    });
  }

  if (!ccfg.rcs->acquire_semaphore) {
    ccfg.rcs->acquire_semaphore =
        ccfg.rgp->create_external_semaphore("acquire-semaphore");
  }

  if (!ccfg.rcs->present_semaphore) {
    ccfg.rcs->present_semaphore =
        ccfg.rgp->create_external_semaphore("present-semaphore");
  }

  auto blit = ccfg.rgb->create_pass({.name = "blit-to-swapchain"});

  blit.wait_semaphore(ccfg.rcs->acquire_semaphore);

  RgTextureToken src_token =
      blit.read_texture(cfg.src, RG_TRANSFER_SRC_TEXTURE);

  auto [final_backbuffer, backbuffer_token] = blit.write_texture(
      "final-backbuffer", ccfg.rcs->backbuffer, RG_TRANSFER_DST_TEXTURE);

  blit.set_callback(
      [=](Renderer &renderer, const RgRuntime &rg, CommandRecorder &cmd) {
        Handle<Texture> src = rg.get_texture(src_token);
        Handle<Texture> backbuffer = rg.get_texture(backbuffer_token);
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
      });

  auto transition = ccfg.rgb->create_pass({.name = "present"});
  transition.set_callback(
      [](Renderer &, const RgRuntime &, CommandRecorder &) {});

  (void)transition.read_texture(final_backbuffer, RG_PRESENT_TEXTURE);

  transition.signal_semaphore(ccfg.rcs->present_semaphore);

  rgb.set_external_semaphore(ccfg.rcs->acquire_semaphore,
                             cfg.acquire_semaphore);
  rgb.set_external_semaphore(ccfg.rcs->present_semaphore,
                             cfg.present_semaphore);
  rgb.set_external_texture(ccfg.rcs->backbuffer, cfg.swapchain->acquire_texture(
                                                     cfg.acquire_semaphore));
}
