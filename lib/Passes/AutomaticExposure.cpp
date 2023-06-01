#include "Passes/AutomaticExposure.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/exposure.hpp"
#include "glsl/postprocess_interface.hpp"

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
  auto pass =
      rgb.create_pass("Automatic exposure: initialize luminance histogram");

  auto histogram = pass.create_buffer({
      .name = "Empty luminance histogram",
      REN_SET_DEBUG_NAME("Luminance histogram"),
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

struct BuildLumianceHistogramPassResources {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  TextureIDAllocator *texture_allocator;
  Handle<ComputePipeline> pipeline;
};

void run_build_luminance_histogram_pass(
    Device &device, RenderGraph &rg, CommandBuffer &cmd,
    const BuildLumianceHistogramPassResources &rcs) {
  assert(rcs.texture);
  assert(rcs.histogram_buffer);
  assert(rcs.texture_allocator);
  assert(rcs.pipeline);

  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  auto buffer = rg.get_buffer(rcs.histogram_buffer);
  auto texture = rg.get_texture(rcs.texture);
  auto texture_index =
      rcs.texture_allocator->allocate_frame_storage_texture(texture);

  cmd.bind_compute_pipeline(rcs.pipeline);

  std::array sets = {rcs.texture_allocator->get_set()};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, sets);

  glsl::BuildLuminanceHistogramConstants constants = {
      .histogram_ptr = device.get_buffer_device_address(buffer),
      .tex = texture_index,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

  auto size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::BUILD_LUMINANCE_HISTOGRAM_THREADS_X,
                           glsl::BUILD_LUMINANCE_HISTOGRAM_THREADS_Y};
  glm::uvec2 work_size = {glsl::BUILD_LUMINANCE_HISTOGRAM_ITEMS_X,
                          glsl::BUILD_LUMINANCE_HISTOGRAM_ITEMS_Y};
  cmd.dispatch_threads(size, group_size * work_size);
}

struct BuildLuminanceHistogramPassConfig {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  Handle<ComputePipeline> pipeline;
};

struct BuildLuminanceHistogramPassOutput {
  RGBufferID histogram_buffer;
};

auto setup_build_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const BuildLuminanceHistogramPassConfig &cfg)
    -> BuildLuminanceHistogramPassOutput {
  assert(cfg.texture);
  assert(cfg.histogram_buffer);
  assert(cfg.texture_allocator);
  assert(cfg.pipeline);

  auto pass = rgb.create_pass("Automatic exposure: build luminance histogram");

  pass.read_texture({
      .texture = cfg.texture,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
              .layout = VK_IMAGE_LAYOUT_GENERAL,
          },
  });

  auto histogram = pass.write_buffer({
      .name = "Luminance histogram",
      .buffer = cfg.histogram_buffer,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          },
  });

  BuildLumianceHistogramPassResources rcs = {
      .texture = cfg.texture,
      .histogram_buffer = histogram,
      .texture_allocator = cfg.texture_allocator,
      .pipeline = cfg.pipeline,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_build_luminance_histogram_pass(device, rg, cmd, rcs);
  });

  return {
      .histogram_buffer = histogram,
  };
}

struct ReduceLuminanceHistogramPassResources {
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
  RGBufferID exposure_buffer;
  Handle<ComputePipeline> pipeline;
  float exposure_compensation;
};

void run_reduce_luminance_histogram_pass(
    Device &device, RenderGraph &rg, CommandBuffer &cmd,
    const ReduceLuminanceHistogramPassResources &rcs) {
  assert(rcs.histogram_buffer);
  assert(rcs.previous_exposure_buffer);
  assert(rcs.exposure_buffer);
  assert(rcs.pipeline);

  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  const auto &histogram_buffer = rg.get_buffer(rcs.histogram_buffer);
  const auto &previous_exposure_buffer =
      rg.get_buffer(rcs.previous_exposure_buffer);
  const auto &exposure_buffer = rg.get_buffer(rcs.exposure_buffer);

  cmd.bind_compute_pipeline(rcs.pipeline);

  glsl::ReduceLuminanceHistogramConstants constants = {
      .histogram_ptr = device.get_buffer_device_address(histogram_buffer),
      .previous_exposure_ptr =
          device.get_buffer_device_address(previous_exposure_buffer),
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .exposure_compensation = rcs.exposure_compensation,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

  cmd.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
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
  assert(cfg.previous_exposure_buffer);
  assert(cfg.pipeline);

  auto reduce_pass =
      rgb.create_pass("Automatic exposure: reduce luminance histogram");

  reduce_pass.read_buffer({
      .buffer = cfg.histogram_buffer,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          },
  });

  reduce_pass.read_buffer({
      .buffer = cfg.previous_exposure_buffer,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,

          },
  });

  auto exposure = reduce_pass.create_buffer({
      .name = "Automatic exposure",
      REN_SET_DEBUG_NAME("Automatic exposure buffer"),
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
      .previous_exposure_buffer = cfg.previous_exposure_buffer,
      .exposure_buffer = exposure,
      .pipeline = cfg.pipeline,
      .exposure_compensation = cfg.exposure_compensation,
  };

  reduce_pass.set_callback(
      [rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        run_reduce_luminance_histogram_pass(device, rg, cmd, rcs);
      });

  return {
      .exposure_buffer = exposure,
  };
}

} // namespace

auto setup_automatic_exposure_setup_pass(
    Device &device, RenderGraph::Builder &rgb,
    const AutomaticExposureSetupPassConfig &cfg) -> ExposurePassOutput {
  return cfg.previous_exposure_buffer.map_or_else(
      [&](const RGBufferExportInfo &export_info) -> ExposurePassOutput {
        auto exposure_buffer = rgb.import_buffer({
            .name = "Previous frame's automatic exposure",
            .buffer = export_info.buffer,
            .state = export_info.state,
        });

        return {
            .exposure_buffer = exposure_buffer,
        };
      },
      [&]() -> ExposurePassOutput {
        auto pass = rgb.create_pass("Automatic exposure: set initial exposure");
        auto exposure_buffer = pass.create_buffer({
            .name = "Initial automatic exposure",
            REN_SET_DEBUG_NAME("Initial automatic exposure buffer"),
            .heap = BufferHeap::Upload,
            .size = sizeof(glsl::Exposure),
        });

        pass.set_callback([exposure_buffer](Device &device, RenderGraph &rg,
                                            CommandBuffer &cmd) {
          auto *exposure_ptr =
              device.map_buffer<glsl::Exposure>(rg.get_buffer(exposure_buffer));
          *exposure_ptr = {
              .exposure = 0.00005f,
          };
        });

        return {
            .exposure_buffer = exposure_buffer,
        };
      });
}

auto setup_automatic_exposure_calculation_pass(
    Device &device, RenderGraph::Builder &rgb,
    const AutomaticExposureCalculationPassConfig &cfg)
    -> AutomaticExposureCalculationPassOutput {
  assert(cfg.texture);
  assert(cfg.previous_exposure_buffer);
  assert(cfg.texture_allocator);
  assert(cfg.pipelines);

  auto [histogram_buffer] =
      setup_initialize_luminance_histogram_pass(device, rgb, {});

  histogram_buffer =
      setup_build_luminance_histogram_pass(
          device, rgb,
          {
              .texture = cfg.texture,
              .histogram_buffer = histogram_buffer,
              .texture_allocator = cfg.texture_allocator,
              .pipeline = cfg.pipelines->build_luminance_histogram,
          })
          .histogram_buffer;

  auto [exposure_buffer] = setup_reduce_luminance_histogram_pass(
      device, rgb,
      {
          .histogram_buffer = histogram_buffer,
          .previous_exposure_buffer = cfg.previous_exposure_buffer,
          .pipeline = cfg.pipelines->reduce_luminance_histogram,
          .exposure_compensation = cfg.exposure_compensation,
      });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

} // namespace ren
