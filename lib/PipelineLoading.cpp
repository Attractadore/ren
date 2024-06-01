#include "PipelineLoading.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"
#include "Support/Errors.hpp"
#include "glsl/Textures.h"

#include "EarlyZVS.h"
#include "ImGuiFS.h"
#include "ImGuiVS.h"
#include "InstanceCullingAndLODCS.h"
#include "OpaqueFS.h"
#include "OpaqueVS.h"
#include "PostProcessingCS.h"
#include "ReduceLuminanceHistogramCS.h"
#include "glsl/OpaquePass.h"

#include <spirv_reflect.h>

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout> {
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings = {};
#if 0
  bindings[glsl::SAMPLERS_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_SAMPLER,
      .count = glsl::NUM_SAMPLERS,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
#endif
  bindings[glsl::SAMPLED_TEXTURES_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .count = glsl::NUM_SAMPLED_TEXTURES,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  bindings[glsl::STORAGE_TEXTURES_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .count = glsl::NUM_STORAGE_TEXTURES,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
  };
  return arena.create_descriptor_set_layout({
      .name = "Textures descriptor set layout",
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindings = bindings,
  });
}

auto create_pipeline_layout(ResourceArena &arena,
                            Handle<DescriptorSetLayout> persistent_set_layout,
                            TempSpan<const Span<const std::byte>> shaders,
                            std::string_view name) -> Handle<PipelineLayout> {
  VkPushConstantRange push_constants = {};

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
    assert(num_push_constants <= 1);
    if (num_push_constants) {
      SpvReflectBlockVariable *block_var;
      throw_if_failed(
          shader.EnumeratePushConstantBlocks(&num_push_constants, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants.stageFlags |= stage;
      push_constants.size = block_var->padded_size;
    }
  }

  SmallVector<Handle<DescriptorSetLayout>> layouts;
  if (persistent_set_layout) {
    layouts.push_back(persistent_set_layout);
  }

  return arena.create_pipeline_layout({
      .name = fmt::format("{} pipeline layout", name),
      .set_layouts = layouts,
      .push_constants = push_constants,
  });
}

auto load_compute_pipeline(ResourceArena &arena,
                           Handle<DescriptorSetLayout> persistent_set_layout,
                           Span<const std::byte> shader, std::string_view name)
    -> Handle<ComputePipeline> {
  auto layout =
      create_pipeline_layout(arena, persistent_set_layout, {shader}, name);
  return arena.create_compute_pipeline({
      .name = fmt::format("{} compute pipeline", name),
      .layout = layout,
      .shader =
          {
              .code = shader,
          },
  });
}

auto load_pipelines(ResourceArena &arena,
                    Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines {
  return {
    .instance_culling_and_lod = load_instance_culling_and_lod_pipeline(arena),
    .early_z_pass = load_early_z_pass_pipeline(arena),
    .opaque_pass = load_opaque_pass_pipelines(arena, persistent_set_layout),
    .post_processing =
        load_post_processing_pipeline(arena, persistent_set_layout),
    .reduce_luminance_histogram =
        load_reduce_luminance_histogram_pipeline(arena, persistent_set_layout),
#if REN_IMGUI
    .imgui_pass = load_imgui_pipeline(arena, persistent_set_layout, SDR_FORMAT),
#endif
  };
}

auto load_instance_culling_and_lod_pipeline(ResourceArena &arena)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, NullHandle,
      Span(InstanceCullingAndLODCS, InstanceCullingAndLODCS_count).as_bytes(),
      "Instance culling");
}

auto load_early_z_pass_pipeline(ResourceArena &arena)
    -> Handle<GraphicsPipeline> {
  auto vs = Span(EarlyZVS, EarlyZVS_count).as_bytes();
  auto layout = create_pipeline_layout(arena, Handle<DescriptorSetLayout>(),
                                       {vs}, "Early Z pass");
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
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS> {
  auto vs = Span(OpaqueVS, OpaqueVS_count).as_bytes();
  auto fs = Span(OpaqueFS, OpaqueFS_count).as_bytes();
  auto layout = create_pipeline_layout(arena, persistent_set_layout, {vs, fs},
                                       "Opaque pass");
  std::array color_attachments = {ColorAttachmentInfo{
      .format = HDR_FORMAT,
  }};
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      pipelines;
  for (int i = 0; i < glsl::NUM_MESH_ATTRIBUTE_FLAGS; ++i) {
    MeshAttributeFlags flags(static_cast<MeshAttribute>(i));
    std::array<SpecConstant, 3> spec_constants = {{
        {glsl::S_OPAQUE_FEATURE_VC, flags.isSet(MeshAttribute::Color)},
        {glsl::S_OPAQUE_FEATURE_TS, flags.isSet(MeshAttribute::Tangent)},
        {glsl::S_OPAQUE_FEATURE_UV, flags.isSet(MeshAttribute::UV)},
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

auto load_reduce_luminance_histogram_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      Span(ReduceLuminanceHistogramCS, ReduceLuminanceHistogramCS_count)
          .as_bytes(),
      "Reduce luminance histogram");
}

auto load_post_processing_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      Span(PostProcessingCS, PostProcessingCS_count).as_bytes(),
      "Post-processing");
}

auto load_imgui_pipeline(ResourceArena &arena,
                         Handle<DescriptorSetLayout> textures, VkFormat format)
    -> Handle<GraphicsPipeline> {
  auto vs = Span(ImGuiVS, ImGuiVS_count).as_bytes();
  auto fs = Span(ImGuiFS, ImGuiFS_count).as_bytes();
  Handle<PipelineLayout> layout =
      create_pipeline_layout(arena, textures, {vs, fs}, "ImGui pass");
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

} // namespace ren
