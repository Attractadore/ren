#pragma once
#include "Reflection.hpp"

#include <spirv_reflect.h>

namespace ren {

class VulkanReflectionModule final : public ReflectionModule {
  spv_reflect::ShaderModule m_module;

public:
  VulkanReflectionModule(std::span<const std::byte> data);

  auto get_shader_stage() const -> ShaderStage override;
  auto get_binding_count() const -> unsigned override;
  void get_bindings(std::span<DescriptorBindingReflection> out) const override;
};

} // namespace ren
