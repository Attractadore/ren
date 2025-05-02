#pragma once
#include "../BumpAllocator.hpp"
#include "../RenderGraph.hpp"
#include "../core/NotNull.hpp"

namespace ren {

struct SceneData;
struct Pipelines;

struct PassPersistentConfig {
  bool async_compute = true;
  glm::uvec2 viewport;
  ExposureMode exposure = {};
  rhi::ImageUsageFlags backbuffer_usage = {};
  bool ssao = true;
  bool ssao_half_res = true;
};

struct PassPersistentResources {
  RgTextureId exposure;
  RgTextureId hdr;
  RgTextureId depth_buffer;
  RgTextureId hi_z;
  RgTextureId ssao_hi_z;
  RgTextureId ssao;
  RgTextureId ssao_depth;
  RgTextureId ssao_blurred;
  RgTextureId sdr;
  RgTextureId backbuffer;
  RgTextureId dhr_lut;
  RgSemaphoreId acquire_semaphore;
  RgSemaphoreId present_semaphore;
};

struct PassCommonConfig {
  NotNull<RgPersistent *> rgp;
  NotNull<RgBuilder *> rgb;
  NotNull<UploadBumpAllocator *> allocator;
  NotNull<const Pipelines *> pipelines;
  NotNull<const SceneData *> scene;
  NotNull<PassPersistentResources *> rcs;
  NotNull<Swapchain *> swapchain;
};

} // namespace ren
