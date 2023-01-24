#include "Reflection.hpp"
#include "Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

ReflectionModule::ReflectionModule(std::span<const std::byte> data)
    : m_module([&] {
        spv_reflect::ShaderModule module(data.size_bytes(), data.data());
        throwIfFailed(module.GetResult(),
                      "SPIRV-Reflect: Failed to create shader module");
        return module;
      }()) {}

auto ReflectionModule::get_shader_stage() const -> VkShaderStageFlagBits {
  return static_cast<VkShaderStageFlagBits>(m_module.GetShaderStage());
}

void ReflectionModule::get_bindings(
    Vector<DescriptorBindingReflection> &out) const {
  uint32_t num_bindings = 0;
  throwIfFailed(m_module.EnumerateDescriptorBindings(&num_bindings, nullptr),
                "SPIRV-Reflect: Failed to enumerate shader bindings");
  SmallVector<SpvReflectDescriptorBinding *> bindings(num_bindings);
  throwIfFailed(
      m_module.EnumerateDescriptorBindings(&num_bindings, bindings.data()),
      "SPIRV-Reflect: Failed to enumerate shader bindings");
  auto stage = get_shader_stage();
  out.assign(map(bindings, [&](const SpvReflectDescriptorBinding *binding) {
    assert(binding);
    return DescriptorBindingReflection{
        .set = binding->set,
        .binding = {
            .binding = binding->binding,
            .type = static_cast<VkDescriptorType>(binding->descriptor_type),
            .count = binding->count,
            .stages = stage,
        }};
  }));
}

} // namespace ren
