#include "PipelineLoading.hpp"
#include "Formats.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"
#include "ren/core/Format.hpp"
#include "sh/Opaque.h"

#include "EarlyZ.vert.hpp"
#include "ExclusiveScanUint32.comp.hpp"
#include "HiZ.comp.hpp"
#include "ImGui.frag.hpp"
#include "ImGui.vert.hpp"
#include "InstanceCullingAndLOD.comp.hpp"
#include "LocalToneMappingAccumulate.comp.hpp"
#include "LocalToneMappingLLM.comp.hpp"
#include "LocalToneMappingLightness.comp.hpp"
#include "LocalToneMappingReduce.comp.hpp"
#include "MeshletCulling.comp.hpp"
#include "MeshletSorting.comp.hpp"
#include "Opaque.frag.hpp"
#include "Opaque.vert.hpp"
#include "PostProcessing.comp.hpp"
#include "PrepareBatch.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"
#include "Skybox.frag.hpp"
#include "Skybox.vert.hpp"
#include "Ssao.comp.hpp"
#include "SsaoFilter.comp.hpp"
#include "SsaoHiZ.comp.hpp"

namespace ren {

Handle<GraphicsPipeline> load_early_z_pass_pipeline(ResourceArena &arena) {
  auto vs = Span(EarlyZVS, EarlyZVSSize).as_bytes();
  return arena.create_graphics_pipeline(GraphicsPipelineCreateInfo{
      .name = "Early Z pass graphics pipeline",
      .vs = {vs},
      .rasterization_state = {.cull_mode = rhi::CullMode::Back},
      .depth_stencil_state =
          {
              .depth_test_enable = true,
              .depth_compare_op = rhi::CompareOp::GreaterOrEqual,
          },
      .dsv_format = DEPTH_FORMAT,
  });
}

StackArray<Handle<GraphicsPipeline>, sh::NUM_MESH_ATTRIBUTE_FLAGS>
load_opaque_pass_pipelines(ResourceArena &arena) {
  ScratchArena scratch;
  auto vs = Span(OpaqueVS, OpaqueVSSize).as_bytes();
  auto fs = Span(OpaqueFS, OpaqueFSSize).as_bytes();
  StackArray<Handle<GraphicsPipeline>, sh::NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
  for (int i = 0; i < sh::NUM_MESH_ATTRIBUTE_FLAGS; ++i) {
    MeshAttributeFlags flags(static_cast<MeshAttribute>(i));
    StackArray<SpecializationConstant, 3> specialization_constants = {{
        {sh::S_OPAQUE_FEATURE_VC, flags.is_set(MeshAttribute::Color)},
        {sh::S_OPAQUE_FEATURE_TS, flags.is_set(MeshAttribute::Tangent)},
        {sh::S_OPAQUE_FEATURE_UV, flags.is_set(MeshAttribute::UV)},
    }};
    pipelines[i] = arena.create_graphics_pipeline(

        GraphicsPipelineCreateInfo{
            .name = format(scratch, "Opaque pass graphics pipeline {}", i),
            .vs =
                {
                    .code = vs,
                    .specialization_constants = specialization_constants,
                },
            .fs =
                {
                    .code = fs,
                    .specialization_constants = specialization_constants,
                },
            .rasterization_state = {.cull_mode = rhi::CullMode::Back},
            .depth_stencil_state =
                {
                    .depth_test_enable = true,
                    .depth_compare_op = rhi::CompareOp::GreaterOrEqual,
                },
            .rtv_formats = {HDR_FORMAT},
            .dsv_format = DEPTH_FORMAT,
        });
  };
  return pipelines;
}

Handle<GraphicsPipeline> load_skybox_pass_pipeline(ResourceArena &arena) {
  auto vs = Span(SkyboxVS, SkyboxVSSize).as_bytes();
  auto fs = Span(SkyboxFS, SkyboxFSSize).as_bytes();
  return arena.create_graphics_pipeline(GraphicsPipelineCreateInfo{
      .name = "Skybox pass graphics pipeline",
      .vs = {vs},
      .fs = {fs},
      .depth_stencil_state =
          {
              .depth_test_enable = true,
              .depth_compare_op = rhi::CompareOp::Equal,
          },
      .rtv_formats = {HDR_FORMAT},
      .dsv_format = DEPTH_FORMAT,
  });
}

Handle<GraphicsPipeline> load_imgui_pipeline(ResourceArena &arena,
                                             TinyImageFormat format) {
  auto vs = Span(ImGuiVS, ImGuiVSSize).as_bytes();
  auto fs = Span(ImGuiFS, ImGuiFSSize).as_bytes();
  return arena.create_graphics_pipeline({
      .name = "ImGui pass graphics pipeline",
      .vs = {vs},
      .fs = {fs},
      .rtv_formats = {format},
      .blend_state =
          {
              .targets = {{
                  .blend_enable = true,
                  .src_color_blend_factor = rhi::BlendFactor::SrcAlpha,
                  .dst_color_blend_factor = rhi::BlendFactor::OneMinusSrcAlpha,
                  .color_blend_op = rhi::BlendOp::Add,
                  .src_alpha_blend_factor = rhi::BlendFactor::One,
                  .dst_alpha_blend_factor = rhi::BlendFactor::OneMinusSrcAlpha,
                  .alpha_blend_op = rhi::BlendOp::Add,
              }},
          },
  });
}

Pipelines load_pipelines(ResourceArena &arena) {
#define compute_pipeline(shader, name)                                         \
  load_compute_pipeline(arena, shader, name)

  return {
      .instance_culling_and_lod =
          compute_pipeline(InstanceCullingAndLODCS, "Instance culling and LOD"),
      .meshlet_culling = compute_pipeline(MeshletCullingCS, "Meshlet culling"),
      .exclusive_scan_uint32 =
          compute_pipeline(ExclusiveScanUint32CS, "Exclusive scan uint32"),
      .meshlet_sorting = compute_pipeline(MeshletSortingCS, "Meshlet soring"),
      .prepare_batch = compute_pipeline(PrepareBatchCS, "Prepare batch"),
      .hi_z = compute_pipeline(HiZCS, "Hi-Z"),
      .ssao_hi_z = compute_pipeline(SsaoHiZCS, "SSAO Hi-Z"),
      .ssao = compute_pipeline(SsaoCS, "SSAO"),
      .ssao_filter = compute_pipeline(SsaoFilterCS, "SSAO filter"),
      .early_z_pass = load_early_z_pass_pipeline(arena),
      .opaque_pass = load_opaque_pass_pipelines(arena),
      .skybox_pass = load_skybox_pass_pipeline(arena),
      .local_tone_mapping_lightness = compute_pipeline(
          LocalToneMappingLightnessCS, "Local tone mapping lightness"),
      .local_tone_mapping_reduce = compute_pipeline(
          LocalToneMappingReduceCS, "Local tone mapping reduction"),
      .local_tone_mapping_accumulate = compute_pipeline(
          LocalToneMappingAccumulateCS, "Local tone mapping accumulation"),
      .local_tone_mapping_llm =
          compute_pipeline(LocalToneMappingLLMCS, "Local tone mapping LLM"),
      .post_processing = compute_pipeline(PostProcessingCS, "Post-processing"),
      .reduce_luminance_histogram = compute_pipeline(
          ReduceLuminanceHistogramCS, "Reduce luminance histogram"),
      .imgui_pass = load_imgui_pipeline(arena, SDR_FORMAT),
  };

#undef compute_pipeline
} // namespace ren

} // namespace ren
