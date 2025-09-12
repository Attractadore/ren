#pragma once
#include "../BumpAllocator.hpp"
#include "../RenderGraph.hpp"
#include "../core/NotNull.hpp"
#include "../sh/LocalToneMapping.h"
#include "../sh/PostProcessing.h"

namespace ren {

struct SceneData;
struct Pipelines;

struct PassPersistentConfig {
  bool async_compute = true;
  glm::uvec2 viewport;
  sh::ExposureMode exposure_mode = {};
  rhi::ImageUsageFlags backbuffer_usage = {};
  bool ssao = true;
  bool ssao_half_res = true;
  bool local_tone_mapping = true;
};

struct PassPersistentResources {
  RgTextureId hdr;
  RgTextureId depth_buffer;
  RgTextureId hi_z;
  RgTextureId ssao_hi_z;
  RgTextureId ssao;
  RgTextureId ssao_depth;
  RgTextureId ssao_llm;
  RgTextureId ltm_lightness;
  RgTextureId ltm_weights;
  RgTextureId ltm_accumulator;
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
  NotNull<const SceneData *> scene;
  NotNull<PassPersistentResources *> rcs;
  glm::uvec2 viewport = {};
};

} // namespace ren
