#include "Passes/PostProcessing.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/post_processing_pass.hpp"
#include "glsl/reduce_luminance_histogram_pass.hpp"

namespace ren {

namespace {

struct InitializeLuminanceHistogramPassConfig {};

struct InitializeLuminanceHistogramPassOutput {
  RGBufferID histogram_buffer;
};

auto setup_initialize_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const InitializeLuminanceHistogramPassConfig &cfg)
    -> InitializeLuminanceHistogramPassOutput {
  auto pass = rgb.create_pass({
      .name = "Automatic exposure: initialize luminance histogram",
  });

  auto histogram = pass.create_buffer({
      .name = "Empty luminance histogram",
      .heap = BufferHeap::Device,
      .size = sizeof(glsl::LuminanceHistogram),
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_CLEAR_BIT,
              .accesses = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          },
  });

  pass.set_callback(
      [histogram](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        cmd.fill_buffer(rg.get_buffer(histogram), 0);
      });

  return {
      .histogram_buffer = histogram,
  };
};

struct PostProcessingUberPassResources {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  Handle<ComputePipeline> pipeline;
};

void run_post_processing_uber_pass(Device &device, RenderGraph &rg,
                                   CommandBuffer &cmd,
                                   const PostProcessingUberPassResources &rcs) {
  assert(rcs.texture);
  assert(rcs.texture_allocator);
  assert(rcs.pipeline);

  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  auto texture = rg.get_texture(rcs.texture);
  auto texture_index =
      rcs.texture_allocator->allocate_frame_storage_texture(texture);

  cmd.bind_compute_pipeline(rcs.pipeline);

  std::array sets = {rcs.texture_allocator->get_set()};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, sets);

  u64 histogram_ptr = 0;
  u64 previous_exposure_ptr = 0;
  if (rcs.histogram_buffer) {
    assert(rcs.previous_exposure_buffer);
    histogram_ptr =
        device.get_buffer_device_address(rg.get_buffer(rcs.histogram_buffer));
    previous_exposure_ptr = device.get_buffer_device_address(
        rg.get_buffer(rcs.previous_exposure_buffer));
  }
  glsl::PostProcessingConstants constants = {
      .histogram_ptr = histogram_ptr,
      .previous_exposure_ptr = previous_exposure_ptr,
      .tex = texture_index,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

  auto size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::POST_PROCESSING_THREADS_X,
                           glsl::POST_PROCESSING_THREADS_Y};
  glm::uvec2 work_size = {glsl::POST_PROCESSING_WORK_SIZE_X,
                          glsl::POST_PROCESSING_WORK_SIZE_Y};
  cmd.dispatch_threads(size, group_size * work_size);
}

struct PostProcessingUberPassConfig {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  Handle<ComputePipeline> pipeline;
};

struct PostProcessingUberPassOutput {
  RGTextureID texture;
  RGBufferID histogram_buffer;
};

auto setup_post_processing_uber_pass(Device &device, RenderGraph::Builder &rgb,
                                     const PostProcessingUberPassConfig &cfg)
    -> PostProcessingUberPassOutput {
  assert(cfg.texture);
  assert(cfg.texture_allocator);
  assert(cfg.pipeline);

  auto pass = rgb.create_pass({
      .name = "Post-processing",
  });

  auto texture = pass.write_texture({
      .name = "Color buffer after post-processing",
      .texture = cfg.texture,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
              .layout = VK_IMAGE_LAYOUT_GENERAL,
          },
  });

  RGBufferID histogram_buffer;
  if (cfg.histogram_buffer) {
    assert(cfg.previous_exposure_buffer);
    histogram_buffer = pass.write_buffer({
        .name = "Luminance histogram",
        .buffer = cfg.histogram_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            },
    });
    pass.read_buffer({
        .buffer = cfg.previous_exposure_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
  }

  PostProcessingUberPassResources rcs = {
      .texture = cfg.texture,
      .histogram_buffer = histogram_buffer,
      .previous_exposure_buffer = cfg.previous_exposure_buffer,
      .texture_allocator = cfg.texture_allocator,
      .pipeline = cfg.pipeline,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_post_processing_uber_pass(device, rg, cmd, rcs);
  });

  return {
      .texture = texture,
      .histogram_buffer = histogram_buffer,
  };
}

struct ReduceLuminanceHistogramPassResources {
  RGBufferID histogram_buffer;
  RGBufferID exposure_buffer;
  Handle<ComputePipeline> pipeline;
  float exposure_compensation = 0.0f;
};

void run_reduce_luminance_histogram_pass(
    Device &device, RenderGraph &rg, CommandBuffer &cmd,
    const ReduceLuminanceHistogramPassResources &rcs) {
  assert(rcs.histogram_buffer);
  assert(rcs.exposure_buffer);
  assert(rcs.pipeline);

  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  const auto &histogram_buffer = rg.get_buffer(rcs.histogram_buffer);
  const auto &exposure_buffer = rg.get_buffer(rcs.exposure_buffer);

  cmd.bind_compute_pipeline(rcs.pipeline);

  glsl::ReduceLuminanceHistogramConstants constants = {
      .histogram_ptr = device.get_buffer_device_address(histogram_buffer),
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .exposure_compensation = rcs.exposure_compensation,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

  cmd.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  RGBufferID histogram_buffer;
  Handle<ComputePipeline> pipeline;
  float exposure_compensation = 0.0f;
};

struct ReduceLuminanceHistogramPassOutput {
  RGBufferID exposure_buffer;
};

auto setup_reduce_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const ReduceLuminanceHistogramPassConfig &cfg)
    -> ReduceLuminanceHistogramPassOutput {
  assert(cfg.histogram_buffer);
  assert(cfg.pipeline);

  auto pass = rgb.create_pass({
      .name = "Automatic exposure: reduce luminance histogram",
  });

  pass.read_buffer({
      .buffer = cfg.histogram_buffer,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          },
  });

  auto exposure_buffer = pass.create_buffer({
      .name = "Automatic exposure",
      .heap = BufferHeap::Device,
      .size = sizeof(glsl::Exposure),
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          },
      .preserve = true,
  });

  ReduceLuminanceHistogramPassResources rcs = {
      .histogram_buffer = cfg.histogram_buffer,
      .exposure_buffer = exposure_buffer,
      .pipeline = cfg.pipeline,
      .exposure_compensation = cfg.exposure_compensation,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_reduce_luminance_histogram_pass(device, rg, cmd, rcs);
  });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

} // namespace

auto setup_post_processing_passes(Device &device, RenderGraph::Builder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput {
  assert(cfg.texture);
  assert(cfg.texture_allocator);
  assert(cfg.pipelines);

  auto automatic_exposure =
      cfg.options.exposure.mode.get<ExposureOptions::Automatic>();

  RGBufferID histogram_buffer;
  if (automatic_exposure) {
    histogram_buffer =
        setup_initialize_luminance_histogram_pass(device, rgb, {})
            .histogram_buffer;
  }

  switch (cfg.options.tone_mapping.oper) {
  case REN_TONE_MAPPING_OPERATOR_REINHARD: {
  } break;
  case REN_TONE_MAPPING_OPERATOR_ACES: {
    todo("ACES tone mapping is not implemented!");
  } break;
  }

  auto [texture, histogram_buffer_after_build] =
      setup_post_processing_uber_pass(
          device, rgb,
          {
              .texture = cfg.texture,
              .histogram_buffer = histogram_buffer,
              .previous_exposure_buffer = cfg.previous_exposure_buffer,
              .texture_allocator = cfg.texture_allocator,
              .pipeline = cfg.pipelines->post_processing,
          });
  histogram_buffer = histogram_buffer_after_build;

  RGBufferID automatic_exposure_buffer;
  if (automatic_exposure) {
    automatic_exposure_buffer =
        setup_reduce_luminance_histogram_pass(
            device, rgb,
            {
                .histogram_buffer = histogram_buffer,
                .pipeline = cfg.pipelines->reduce_luminance_histogram,
                .exposure_compensation =
                    automatic_exposure->exposure_compensation,
            })
            .exposure_buffer;
  }

  return {
      .texture = texture,
      .automatic_exposure_buffer = automatic_exposure_buffer,
  };
}

} // namespace ren
