#pragma once
#include "BumpAllocator.hpp"
#include "RenderGraph.hpp"
#include "core/NotNull.hpp"

namespace ren {

struct SceneData;
struct Pipelines;
struct Samplers;

struct PassPersistentConfig {
  glm::uvec2 viewport;
  ExposureMode exposure = {};
  VkImageUsageFlags backbuffer_usage = 0;
};

struct PassPersistentResources {
  RgTextureId exposure;
  RgTextureId hdr;
  RgTextureId depth_buffer;
  RgTextureId hi_z;
  RgTextureId sdr;
  RgSemaphoreId acquire_semaphore;
  RgSemaphoreId present_semaphore;
};

struct PassCommonConfig {
  NotNull<RgPersistent *> rgp;
  NotNull<RgBuilder *> rgb;
  NotNull<UploadBumpAllocator *> allocator;
  NotNull<const Pipelines *> pipelines;
  NotNull<const Samplers *> samplers;
  NotNull<const SceneData *> scene;
  NotNull<PassPersistentResources *> rcs;
  NotNull<Swapchain *> swapchain;
};

} // namespace ren
