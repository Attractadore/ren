#pragma once
#include "Reflection.hpp"
#include "Support/HashMap.hpp"

#include <spirv_reflect.h>

namespace ren {

class VulkanReflectionModule final : public ReflectionModule {
  spv_reflect::ShaderModule m_module;
  HashMap<unsigned, std::string_view> m_user_types;
  Vector<VertexAttribute> m_input_variables;

public:
  VulkanReflectionModule(std::span<const std::byte> data);

  auto get_shader_stage() const -> ShaderStage override;

  auto get_binding_count() const -> unsigned override;
  void get_bindings(std::span<DescriptorBindingReflection> out) const override;

  auto get_input_variable_count() const -> unsigned override;
  void get_input_variables(std::span<VertexAttribute> out) const override;
};

} // namespace ren
