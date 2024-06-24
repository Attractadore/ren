#include "Passes/Present.hpp"
#include "CommandRecorder.hpp"

namespace ren {

void setup_present_pass(RgBuilder &rgb, const PresentPassConfig &cfg) {
  *cfg.backbuffer = rgb.create_external_texture({
      .name = "backbuffer",
      .format = cfg.backbuffer_format,
      .width = cfg.backbuffer_size.x,
      .height = cfg.backbuffer_size.y,
  });

  auto blit = rgb.create_pass({.name = "blit-to-swapchain"});

  *cfg.acquire_semaphore =
      rgb.create_external_semaphore({.name = "acquire-semaphore"});
  blit.wait_semaphore(*cfg.acquire_semaphore);

  RgTextureToken src_token =
      blit.read_texture(cfg.src, RG_TRANSFER_SRC_TEXTURE);

  auto [final_backbuffer, backbuffer_token] = blit.write_texture(
      "final-backbuffer", *cfg.backbuffer, RG_TRANSFER_DST_TEXTURE);

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

  auto transition = rgb.create_pass({.name = "present"});
  transition.set_callback(
      [](Renderer &, const RgRuntime &, CommandRecorder &) {});

  (void)transition.read_texture(final_backbuffer, RG_PRESENT_TEXTURE);

  *cfg.present_semaphore =
      rgb.create_external_semaphore({.name = "present-semaphore"});
  transition.signal_semaphore(*cfg.present_semaphore);
}

} // namespace ren
