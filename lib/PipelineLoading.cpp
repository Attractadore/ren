#include "PipelineLoading.hpp"
#include "Formats.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"
#include "sh/Opaque.h"

#include "EarlyZ.vert.hpp"
#include "ExclusiveScanUint32.comp.hpp"
#include "HiZ.comp.hpp"
#include "ImGui.frag.hpp"
#include "ImGui.vert.hpp"
#include "InstanceCullingAndLOD.comp.hpp"
#include "LocalToneMappingAccumulate.comp.hpp"
#include "LocalToneMappingInit.comp.hpp"
#include "LocalToneMappingLLM.comp.hpp"
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

#include <fmt/format.h>

namespace ren {

auto load_early_z_pass_pipeline(ResourceArena &arena)
    -> Result<Handle<GraphicsPipeline>, Error> {
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

auto load_opaque_pass_pipelines(ResourceArena &arena) -> Result<
    std::array<Handle<GraphicsPipeline>, sh::NUM_MESH_ATTRIBUTE_FLAGS>, Error> {
  auto vs = Span(OpaqueVS, OpaqueVSSize).as_bytes();
  auto fs = Span(OpaqueFS, OpaqueFSSize).as_bytes();
  std::array<Handle<GraphicsPipeline>, sh::NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
  for (int i = 0; i < sh::NUM_MESH_ATTRIBUTE_FLAGS; ++i) {
    MeshAttributeFlags flags(static_cast<MeshAttribute>(i));
    std::array<SpecializationConstant, 3> specialization_constants = {{
        {sh::S_OPAQUE_FEATURE_VC, flags.is_set(MeshAttribute::Color)},
        {sh::S_OPAQUE_FEATURE_TS, flags.is_set(MeshAttribute::Tangent)},
        {sh::S_OPAQUE_FEATURE_UV, flags.is_set(MeshAttribute::UV)},
    }};
    Result<Handle<GraphicsPipeline>, Error> result =
        arena.create_graphics_pipeline(GraphicsPipelineCreateInfo{
            .name = fmt::format("Opaque pass graphics pipeline {}", i),
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
    if (!result) {
      for (Handle<GraphicsPipeline> pipeline : Span(pipelines).subspan(0, i)) {
        arena.destroy(pipeline);
      }
      return Failure(result.error());
    }
    pipelines[i] = *result;
  };
  return pipelines;
}

auto load_skybox_pass_pipeline(ResourceArena &arena)
    -> Result<Handle<GraphicsPipeline>, Error> {
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

auto load_imgui_pipeline(ResourceArena &arena, TinyImageFormat format)
    -> Result<Handle<GraphicsPipeline>, Error> {
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

auto load_pipelines(ResourceArena &arena) -> Result<Pipelines, Error> {

#define compute_pipeline(shader, name)                                         \
  load_compute_pipeline(arena, shader, name)

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
  ren_try(pipelines.hi_z, compute_pipeline(HiZCS, "Hi-Z"));
  ren_try(pipelines.ssao_hi_z, compute_pipeline(SsaoHiZCS, "SSAO Hi-Z"));
  ren_try(pipelines.ssao, compute_pipeline(SsaoCS, "SSAO"));
  ren_try(pipelines.ssao_filter, compute_pipeline(SsaoFilterCS, "SSAO filter"));
  ren_try(pipelines.early_z_pass, load_early_z_pass_pipeline(arena));
  ren_try(pipelines.opaque_pass, load_opaque_pass_pipelines(arena));
  ren_try(pipelines.skybox_pass, load_skybox_pass_pipeline(arena));
  ren_try(pipelines.local_tone_mapping_init,
          compute_pipeline(LocalToneMappingInitCS,
                           "Local tone mapping initialization"));
  ren_try(pipelines.local_tone_mapping_reduce,
          compute_pipeline(LocalToneMappingReduceCS,
                           "Local tone mapping reduction"));
  ren_try(pipelines.local_tone_mapping_accumulate,
          compute_pipeline(LocalToneMappingAccumulateCS,
                           "Local tone mapping accumulation"));
  ren_try(pipelines.local_tone_mapping_llm,
          compute_pipeline(LocalToneMappingLLMCS, "Local tone mapping LLM"));
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
