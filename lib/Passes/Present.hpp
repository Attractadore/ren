#pragma once
#include "RenderGraph.hpp"
#include "Support/NotNull.hpp"

namespace ren {

struct PresentPassConfig {
  RgTextureId src;
  VkFormat backbuffer_format = VK_FORMAT_UNDEFINED;
  glm::uvec2 backbuffer_size;
  NotNull<RgTextureId *> backbuffer;
  NotNull<RgSemaphoreId *> acquire_semaphore;
  NotNull<RgSemaphoreId *> present_semaphore;
};

void setup_present_pass(RgBuilder &rgb, const PresentPassConfig &cfg);

} // namespace ren
