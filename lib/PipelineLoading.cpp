#include "PipelineLoading.hpp"
#include "Formats.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"
#include "core/Errors.hpp"
#include "glsl/Opaque.h"

#include "EarlyZ.vert.hpp"
#include "ExclusiveScanUint32.comp.hpp"
#include "HiZSpd.comp.hpp"
#include "ImGui.frag.hpp"
#include "ImGui.vert.hpp"
#include "InstanceCullingAndLOD.comp.hpp"
#include "MeshletCulling.comp.hpp"
#include "MeshletSorting.comp.hpp"
#include "Opaque.frag.hpp"
#include "Opaque.vert.hpp"
#include "PostProcessing.comp.hpp"
#include "PrepareBatch.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

#include <spirv_reflect.h>

namespace ren {

auto create_pipeline_layout(ResourceArena &arena,
                            TempSpan<const Span<const std::byte>> shaders,
                            StringView name)
    -> Result<Handle<PipelineLayout>, Error> {
  u32 push_constants_size = 0;

  SmallVector<SpvReflectDescriptorSet *> sets;
  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throw_if_failed(shader.GetResult(),
                    "SPIRV-Reflect: Failed to create shader module");

    auto stage = static_cast<VkShaderStageFlagBits>(shader.GetShaderStage());

    u32 num_pc_blocks = 0;
    throw_if_failed(shader.EnumeratePushConstantBlocks(&num_pc_blocks, nullptr),
                    "SPIRV-Reflect: Failed to enumerate push constants");
    ren_assert(num_pc_blocks <= 1);
    if (num_pc_blocks) {
      SpvReflectBlockVariable *block_var;
      throw_if_failed(
          shader.EnumeratePushConstantBlocks(&num_pc_blocks, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants_size = block_var->padded_size;
    }
  }

  return arena.create_pipeline_layout({
      .name = fmt::format("{} pipeline layout", name),
      .use_resource_heap = true,
      .use_sampler_heap = true,
      .push_constants_size = push_constants_size,
  });
}

auto load_early_z_pass_pipeline(ResourceArena &arena)
    -> Result<Handle<GraphicsPipeline>, Error> {
  auto vs = Span(EarlyZVS, EarlyZVSSize).as_bytes();
  ren_try(Handle<PipelineLayout> layout,
          create_pipeline_layout(arena, {vs}, "Early Z pass"));
  return arena.create_graphics_pipeline({
      .name = "Early Z pass graphics pipeline",
      .layout = layout,
      .vertex_shader = {vs},
      .depth_test =
          DepthTestInfo{
              .format = DEPTH_FORMAT,
              .compare_op = VK_COMPARE_OP_GREATER_OR_EQUAL,
          },
  });
}

auto load_opaque_pass_pipelines(ResourceArena &arena) -> Result<
    std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>,
    Error> {
  auto vs = Span(OpaqueVS, OpaqueVSSize).as_bytes();
  auto fs = Span(OpaqueFS, OpaqueFSSize).as_bytes();
  ren_try(Handle<PipelineLayout> layout,
          create_pipeline_layout(arena, {vs, fs}, "Opaque pass"));
  std::array color_attachments = {ColorAttachmentInfo{
      .format = HDR_FORMAT,
  }};
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      pipelines;
  for (int i = 0; i < glsl::NUM_MESH_ATTRIBUTE_FLAGS; ++i) {
    MeshAttributeFlags flags(static_cast<MeshAttribute>(i));
    std::array<SpecConstant, 3> spec_constants = {{
        {glsl::S_OPAQUE_FEATURE_VC, flags.is_set(MeshAttribute::Color)},
        {glsl::S_OPAQUE_FEATURE_TS, flags.is_set(MeshAttribute::Tangent)},
        {glsl::S_OPAQUE_FEATURE_UV, flags.is_set(MeshAttribute::UV)},
    }};
    pipelines[i] = arena.create_graphics_pipeline({
        .name = fmt::format("Opaque pass graphics pipeline {}", i),
        .layout = layout,
        .vertex_shader =
            {
                .code = vs,
                .spec_constants = spec_constants,
            },
        .fragment_shader =
            ShaderInfo{
                .code = fs,
                .spec_constants = spec_constants,
            },
        .depth_test =
            DepthTestInfo{
                .format = DEPTH_FORMAT,
                .compare_op = VK_COMPARE_OP_GREATER_OR_EQUAL,
            },
        .color_attachments = color_attachments,
    });
  };
  return pipelines;
}

auto load_imgui_pipeline(ResourceArena &arena, TinyImageFormat format)
    -> Result<Handle<GraphicsPipeline>, Error> {
  auto vs = Span(ImGuiVS, ImGuiVSSize).as_bytes();
  auto fs = Span(ImGuiFS, ImGuiFSSize).as_bytes();
  ren_try(Handle<PipelineLayout> layout,
          create_pipeline_layout(arena, {vs, fs}, "ImGui pass"));
  std::array color_attachments = {ColorAttachmentInfo{
      .format = format,
      .blending =
          ColorBlendAttachmentInfo{
              .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
              .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              .color_blend_op = VK_BLEND_OP_ADD,
              .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
              .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              .alpha_blend_op = VK_BLEND_OP_ADD,
          },
  }};
  return arena.create_graphics_pipeline({
      .name = "ImGui pass graphics pipeline",
      .layout = layout,
      .vertex_shader = {vs},
      .fragment_shader = ShaderInfo{fs},
      .rasterization = {.cull_mode = VK_CULL_MODE_NONE},
      .color_attachments = color_attachments,
  });
  return {};
}

auto load_pipelines(ResourceArena &arena) -> Result<Pipelines, Error> {
  auto load_compute_pipeline =
      [&](Span<const std::byte> shader,
          StringView name) -> Result<Handle<ComputePipeline>, Error> {
    ren_try(Handle<PipelineLayout> layout,
            create_pipeline_layout(arena, {shader}, name));
    return arena.create_compute_pipeline({
        .name = fmt::format("{} compute pipeline", name),
        .layout = layout,
        .shader =
            {
                .code = shader,
            },
    });
  };

#define compute_pipeline(shader, name)                                         \
  load_compute_pipeline(Span(shader, shader##Size).as_bytes(), name)

  Pipelines pipelines;
  ren_try(
      pipelines.instance_culling_and_lod,
      compute_pipeline(InstanceCullingAndLODCS, "Instance culling and LOD"));
  ren_try(pipelines.meshlet_culling,
          compute_pipeline(MeshletCullingCS, "Meshlet culling"));
  ren_try(pipelines.exclusive_scan_uint32,
          compute_pipeline(ExclusiveScanUint32CS, "Exclusive scan uint32"));
  ren_try(pipelines.meshlet_sorting,
          compute_pipeline(MeshletSortingCS, "Meshlet soring"));
  ren_try(pipelines.prepare_batch,
          compute_pipeline(PrepareBatchCS, "Prepare batch"));
  ren_try(pipelines.hi_z, compute_pipeline(HiZSpdCS, "Hi-Z SPD"));
  ren_try(pipelines.early_z_pass, load_early_z_pass_pipeline(arena));
  ren_try(pipelines.opaque_pass, load_opaque_pass_pipelines(arena));
  ren_try(pipelines.post_processing,
          compute_pipeline(PostProcessingCS, "Post-processing"));
  ren_try(pipelines.reduce_luminance_histogram,
          compute_pipeline(ReduceLuminanceHistogramCS,
                           "Reduce luminance histogram"));
#if REN_IMGUI
  ren_try(pipelines.imgui_pass, load_imgui_pipeline(arena, SDR_FORMAT));
#endif

#undef compute_pipeline

  return pipelines;
}

} // namespace ren
