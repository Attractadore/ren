#include "PipelineLoading.hpp"
#include "Errors.hpp"
#include "ResourceArena.hpp"
#include "Support/Array.hpp"
#include "glsl/interface.hpp"

#include "BuildLuminanceHistogramShader.h"
#include "FragmentShader.h"
#include "ReduceLuminanceHistogramShader.h"
#include "ReinhardToneMappingShader.h"
#include "VertexShader.h"

#include "spirv_reflect.h"

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
                            std::span<const std::span<const std::byte>> shaders,
                            std::string_view name) -> Handle<PipelineLayout> {
  VkPushConstantRange push_constants = {};

  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throwIfFailed(shader.GetResult(),
                  "SPIRV-Reflect: Failed to create shader module");

    auto stage = static_cast<VkShaderStageFlagBits>(shader.GetShaderStage());

    uint32_t num_push_constants = 0;
    throwIfFailed(
        shader.EnumeratePushConstantBlocks(&num_push_constants, nullptr),
        "SPIRV-Reflect: Failed to enumerate push constants");
    assert(num_push_constants <= 1);
    if (num_push_constants) {
      SpvReflectBlockVariable *block_var;
      throwIfFailed(
          shader.EnumeratePushConstantBlocks(&num_push_constants, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants.stageFlags |= stage;
      push_constants.size = block_var->size;
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

auto create_color_pass_pipeline_layout(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<PipelineLayout> {
  auto shaders = makeArray<std::span<const std::byte>>(
      std::as_bytes(std::span(VertexShader, VertexShader_count)),
      std::as_bytes(std::span(FragmentShader, FragmentShader_count)));
  return create_pipeline_layout(arena, persistent_set_layout, shaders,
                                "Color pass");
}

auto load_compute_pipeline(ResourceArena &arena,
                           Handle<DescriptorSetLayout> persistent_set_layout,
                           std::span<const std::byte> shader,
                           std::string_view name) -> Handle<ComputePipeline> {
  std::array shaders = {shader};
  auto layout =
      create_pipeline_layout(arena, persistent_set_layout, shaders, name);
  return arena.create_compute_pipeline({
      .name = fmt::format("{} pipeline", name),
      .layout = layout,
      .shader =
          {
              .code = shader,
          },
  });
}

auto load_postprocessing_pipelines(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines {
  return {
      .build_luminance_histogram =
          load_build_luminance_histogram_pipeline(arena, persistent_set_layout),
      .reduce_luminance_histogram =
          load_reduce_luminance_histogram_pipeline(arena),
      .reinhard_tone_mapping =
          load_reinhard_tone_mapping_pipeline(arena, persistent_set_layout),
  };
}

auto load_build_luminance_histogram_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      std::as_bytes(std::span(BuildLuminanceHistogramShader,
                              BuildLuminanceHistogramShader_count)),
      "Build luminance histogram");
}

auto load_reduce_luminance_histogram_pipeline(ResourceArena &arena)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, Handle<DescriptorSetLayout>(),
      std::as_bytes(std::span(ReduceLuminanceHistogramShader,
                              ReduceLuminanceHistogramShader_count)),
      "Reduce luminance histogram");
}

auto load_reinhard_tone_mapping_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      std::as_bytes(std::span(ReinhardToneMappingShader,
                              ReinhardToneMappingShader_count)),
      "Reinhard tone mapping");
}

} // namespace ren
