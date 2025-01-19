#include "PipelineLoading.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"
#include "core/Errors.hpp"
#include "glsl/Texture.h"

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
#include "glsl/Opaque.h"

#include <spirv_reflect.h>

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> PersistentDescriptorSetLayouts {
  constexpr u32 MAX_SET_RESOURCES = glsl::MAX_NUM_RESOURCES / 3;
  return {
      .srv = arena.create_descriptor_set_layout({
          .name = "SRV descriptor set layout",
          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
          .bindings = {DescriptorBinding{
              .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                       VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                       VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .count = MAX_SET_RESOURCES,
              .stages =
                  VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
          }},
      }),
      .cis = arena.create_descriptor_set_layout({
          .name = "CIS descriptor set layout",
          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
          .bindings = {DescriptorBinding{
              .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                       VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                       VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .count = MAX_SET_RESOURCES,
              .stages =
                  VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
          }},
      }),
      .uav = arena.create_descriptor_set_layout({
          .name = "UAV descriptor set layout",
          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
          .bindings = {DescriptorBinding{
              .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                       VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                       VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              .count = MAX_SET_RESOURCES,
              .stages = VK_SHADER_STAGE_COMPUTE_BIT,
          }},
      }),
      .sampler = arena.create_descriptor_set_layout({
          .name = "Sampler descriptor set layout",
          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
          .bindings = {DescriptorBinding{
              .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                       VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
              .type = VK_DESCRIPTOR_TYPE_SAMPLER,
              .count = glsl::NUM_SAMPLERS,
              .stages =
                  VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
          }},
      }),
  };
}

auto create_pipeline_layout(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts,
    TempSpan<const Span<const std::byte>> shaders, StringView name)
    -> Handle<PipelineLayout> {
  std::array<Handle<DescriptorSetLayout>, 4> map = {};
  map[glsl::SRV_SET] = persistent_set_layouts.srv;
  map[glsl::CIS_SET] = persistent_set_layouts.cis;
  map[glsl::UAV_SET] = persistent_set_layouts.uav;
  map[glsl::SAMPLER_SET] = persistent_set_layouts.sampler;

  std::array<Handle<DescriptorSetLayout>, 4> layouts;
  VkPushConstantRange push_constants = {};

  SmallVector<SpvReflectDescriptorSet *> sets;
  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throw_if_failed(shader.GetResult(),
                    "SPIRV-Reflect: Failed to create shader module");

    auto stage = static_cast<VkShaderStageFlagBits>(shader.GetShaderStage());

    uint32_t num_push_constants = 0;
    throw_if_failed(
        shader.EnumeratePushConstantBlocks(&num_push_constants, nullptr),
        "SPIRV-Reflect: Failed to enumerate push constants");
    ren_assert(num_push_constants <= 1);
    if (num_push_constants) {
      SpvReflectBlockVariable *block_var;
      throw_if_failed(
          shader.EnumeratePushConstantBlocks(&num_push_constants, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants.stageFlags |= stage;
      push_constants.size = block_var->padded_size;
    }

    uint32_t num_descriptor_sets = 0;
    throw_if_failed(
        shader.EnumerateDescriptorSets(&num_descriptor_sets, nullptr),
        "SPIRV-Reflect: Failed to enumerate descriptor sets");
    sets.resize(num_descriptor_sets);
    throw_if_failed(
        shader.EnumerateDescriptorSets(&num_descriptor_sets, sets.data()),
        "SPIRV-Reflect: Failed to enumerate descriptor sets");
    for (SpvReflectDescriptorSet *set : sets) {
      layouts[set->set] = map[set->set];
    }
  }

  return arena.create_pipeline_layout({
      .name = fmt::format("{} pipeline layout", name),
      .set_layouts = layouts,
      .push_constants = push_constants,
  });
}

auto load_early_z_pass_pipeline(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts)
    -> Handle<GraphicsPipeline> {
  auto vs = Span(EarlyZVS, EarlyZVSSize).as_bytes();
  auto layout = create_pipeline_layout(arena, persistent_set_layouts, {vs},
                                       "Early Z pass");
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

auto load_opaque_pass_pipelines(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts)
    -> std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS> {
  auto vs = Span(OpaqueVS, OpaqueVSSize).as_bytes();
  auto fs = Span(OpaqueFS, OpaqueFSSize).as_bytes();
  auto layout = create_pipeline_layout(arena, persistent_set_layouts, {vs, fs},
                                       "Opaque pass");
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

auto load_imgui_pipeline(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts,
    TinyImageFormat format) -> Handle<GraphicsPipeline> {
  auto vs = Span(ImGuiVS, ImGuiVSSize).as_bytes();
  auto fs = Span(ImGuiFS, ImGuiFSSize).as_bytes();
  Handle<PipelineLayout> layout = create_pipeline_layout(
      arena, persistent_set_layouts, {vs, fs}, "ImGui pass");
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

auto load_pipelines(
    ResourceArena &arena,
    const PersistentDescriptorSetLayouts &persistent_set_layouts) -> Pipelines {
  auto load_compute_pipeline = [&](Span<const std::byte> shader,
                                   StringView name) {
    auto layout =
        create_pipeline_layout(arena, persistent_set_layouts, {shader}, name);
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

  return {
      .instance_culling_and_lod =
          compute_pipeline(InstanceCullingAndLODCS, "Instance culling and LOD"),
      .meshlet_culling = compute_pipeline(MeshletCullingCS, "Meshlet culling"),
      .exclusive_scan_uint32 =
          compute_pipeline(ExclusiveScanUint32CS, "Exclusive scan uint32"),
      .meshlet_sorting = compute_pipeline(MeshletSortingCS, "Meshlet soring"),
      .prepare_batch = compute_pipeline(PrepareBatchCS, "Prepare batch"),
      .hi_z = compute_pipeline(HiZSpdCS, "Hi-Z SPD"),
      .early_z_pass = load_early_z_pass_pipeline(arena, persistent_set_layouts),
      .opaque_pass = load_opaque_pass_pipelines(arena, persistent_set_layouts),
      .post_processing = compute_pipeline(PostProcessingCS, "Post-processing"),
      .reduce_luminance_histogram = compute_pipeline(
          ReduceLuminanceHistogramCS, "Reduce luminance histogram"),
#if REN_IMGUI
      .imgui_pass =
          load_imgui_pipeline(arena, persistent_set_layouts, SDR_FORMAT),
#endif
  };

#undef compute_pipeline
}

} // namespace ren
