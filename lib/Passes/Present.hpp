#pragma once
#include "RenderGraph.hpp"

namespace ren {

struct PresentPassConfig {
  RgTextureId src;
  VkFormat backbuffer_format = VK_FORMAT_UNDEFINED;
  glm::uvec2 backbuffer_size;
};

struct PresentPassOutput {
  RgTextureId backbuffer;
  RgSemaphoreId acquire_semaphore;
  RgSemaphoreId present_semaphore;
};

auto setup_present_pass(RgBuilder &rgb, const PresentPassConfig &cfg)
    -> PresentPassOutput;

} // namespace ren
