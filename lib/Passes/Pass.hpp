#pragma once
#include "BumpAllocator.hpp"
#include "RenderGraph.hpp"
#include "Support/NotNull.hpp"

namespace ren {

class Scene;
struct Pipelines;

struct PassPersistentConfig {
  glm::uvec2 viewport;
  ExposureMode exposure = {};
  VkImageUsageFlags backbuffer_usage = 0;
};

struct PassPersistentResources {
  RgTextureId exposure;
  RgTextureId hdr;
  RgTextureId depth_buffer;
  RgTextureId sdr;
  RgTextureId backbuffer;
  RgSemaphoreId acquire_semaphore;
  RgSemaphoreId present_semaphore;
};

struct PassCommonConfig {
  NotNull<RgPersistent *> rgp;
  NotNull<RgBuilder *> rgb;
  NotNull<UploadBumpAllocator *> allocator;
  NotNull<const Pipelines *> pipelines;
  NotNull<const Scene *> scene;
  NotNull<PassPersistentResources *> rcs;
};

} // namespace ren
