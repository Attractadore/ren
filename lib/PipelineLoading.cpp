#include "PipelineLoading.hpp"
#include "Errors.hpp"
#include "ResourceArena.hpp"
#include "Support/Array.hpp"
#include "glsl/interface.hpp"

#include "FragmentShader.h"
#include "ReinhardTonemapShader.h"
#include "VertexShader.h"

#include "spirv_reflect.h"

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout> {
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings = {};
  bindings[glsl::SAMPLERS_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_SAMPLER,
      .count = glsl::NUM_SAMPLERS,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  bindings[glsl::SAMPLED_TEXTURES_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
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
      REN_SET_DEBUG_NAME("Persistent descriptor set"),
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindings = bindings,
  });
}

auto create_pipeline_layout(ResourceArena &arena,
                            Handle<DescriptorSetLayout> persistent_set_layout,
                            std::span<const std::span<const std::byte>> shaders,
                            std::string_view name) -> Handle<PipelineLayout> {
  std::array<std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS>,
             MAX_DESCRIPTOR_SETS>
      set_bindings = {};
  unsigned max_set = 0;

  VkPushConstantRange push_constants = {};

  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throwIfFailed(shader.GetResult(),
                  "SPIRV-Reflect: Failed to create shader module");

    auto stage = static_cast<VkShaderStageFlagBits>(shader.GetShaderStage());

    uint32_t num_sets = 0;
    throwIfFailed(shader.EnumerateDescriptorSets(&num_sets, nullptr),
                  "SPIRV-Reflect: Failed to enumerate shader descriptor sets");
    SmallVector<SpvReflectDescriptorSet *> sets(num_sets);
    throwIfFailed(shader.EnumerateDescriptorSets(&num_sets, sets.data()),
                  "SPIRV-Reflect: Failed to enumerate shader descriptor sets");

    for (const auto *set : sets) {
      max_set = std::max(max_set, set->set);
      for (const auto *binding : std::span(set->bindings, set->binding_count)) {
        if (binding->binding >= MAX_DESCIPTOR_BINDINGS) {
          throw std::runtime_error(
              "Shader module uses more bindings than is supported");
        }
        auto &set_binding = set_bindings[set->set][binding->binding];
        set_binding.type =
            static_cast<VkDescriptorType>(binding->descriptor_type);
        set_binding.count = binding->count;
        set_binding.stages |= stage;
      }
    }

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

  StaticVector<Handle<DescriptorSetLayout>, MAX_DESCRIPTOR_SETS> layouts = {
      persistent_set_layout};
  for (const auto &bindings :
       std::span(&set_bindings[1], &set_bindings[max_set + 1])) {
    layouts.push_back(arena.create_descriptor_set_layout({
        .bindings = bindings,
    }));
  }

  return arena.create_pipeline_layout({
      REN_SET_DEBUG_NAME(fmt::format("{} pipeline layout", name)),
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
      REN_SET_DEBUG_NAME(fmt::format("{} pipeline", name)),
      .layout = layout,
      .shader =
          {
              .code = shader,
          },
  });
}

auto load_reinhard_tonemap_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      std::as_bytes(
          std::span(ReinhardTonemapShader, ReinhardTonemapShader_count)),
      "Reinhard tonemap");
}

auto load_postprocessing_pipelines(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> PostprocessingPipelines {
  return {
      .reinhard_tonemap =
          load_reinhard_tonemap_pipeline(arena, persistent_set_layout),
  };
}

} // namespace ren
