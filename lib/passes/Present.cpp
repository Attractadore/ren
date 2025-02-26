#include "Present.hpp"
#include "../CommandRecorder.hpp"
#include "../Swapchain.hpp"

void ren::setup_present_pass(const PassCommonConfig &ccfg,
                             const PresentPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;

  if (!ccfg.rcs->present_semaphore) {
    ccfg.rcs->present_semaphore =
        ccfg.rgp->create_semaphore("present-semaphore");
  }

  Result<Handle<Texture>, Error> backbuffer =
      ccfg.swapchain->acquire_texture(cfg.acquire_semaphore);
  ren_assert(backbuffer);

  rgb.set_external_texture(ccfg.rcs->backbuffer, *backbuffer);

  rgb.set_external_semaphore(ccfg.rcs->acquire_semaphore,
                             cfg.acquire_semaphore);
  rgb.set_external_semaphore(ccfg.rcs->present_semaphore,
                             cfg.present_semaphore);

  auto present = ccfg.rgb->create_pass({
      .name = "present",
      .queue = RgQueue::Async,
  });

  auto _ = present.read_texture(cfg.src, rhi::PRESENT_IMAGE);

  present.set_callback(
      [](Renderer &renderer, const RgRuntime &rg, CommandRecorder &cmd) {});

  present.signal_semaphore(ccfg.rcs->present_semaphore);
}
