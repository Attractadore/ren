#include "Reflection.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

#include <spirv_reflect.h>

namespace ren {

auto get_set_layout_bindings(std::span<const std::byte> code)
    -> StaticVector<std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS>,
                    MAX_DESCRIPTOR_SETS> {
  spv_reflect::ShaderModule shader_module(code.size_bytes(), code.data());
  throwIfFailed(shader_module.GetResult(),
                "SPIRV-Reflect: Failed to create shader module");

  uint32_t num_sets = 0;
  throwIfFailed(shader_module.EnumerateDescriptorSets(&num_sets, nullptr),
                "SPIRV-Reflect: Failed to enumerate shader descriptor sets");
  SmallVector<SpvReflectDescriptorSet *> sets(num_sets);
  throwIfFailed(shader_module.EnumerateDescriptorSets(&num_sets, sets.data()),
                "SPIRV-Reflect: Failed to enumerate shader descriptor sets");

  auto stage =
      static_cast<VkShaderStageFlagBits>(shader_module.GetShaderStage());

  StaticVector<std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS>,
               MAX_DESCRIPTOR_SETS>
      set_layout_bindings;
  for (const auto *set : sets) {
    if (set->set >= set_layout_bindings.size()) {
      set_layout_bindings.resize(set->set + 1);
    }
    auto &bindings = set_layout_bindings[set->set];

    for (const auto *binding : std::span(set->bindings, set->binding_count)) {
      assert(binding);
      auto index = binding->binding;
      if (index >= MAX_DESCIPTOR_BINDINGS) {
        throw std::runtime_error(
            "Shader module uses more bindings than is supported");
      }
      bindings[index] = {
          .type = static_cast<VkDescriptorType>(binding->descriptor_type),
          .count = binding->count,
          .stages = stage,
      };
    }
  }

  return set_layout_bindings;
}

} // namespace ren
