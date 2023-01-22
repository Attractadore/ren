#pragma once
#include "Descriptors.hpp"

#include <spirv_reflect.h>

#include <span>

namespace ren {

struct DescriptorBindingReflection {
  unsigned set;
  DescriptorBinding binding;
};

class ReflectionModule {
  spv_reflect::ShaderModule m_module;

public:
  ReflectionModule(std::span<const std::byte> data);

  auto get_shader_stage() const -> VkShaderStageFlagBits;

  auto get_binding_count() const -> unsigned;
  void get_bindings(std::span<DescriptorBindingReflection> out) const;
};

} // namespace ren
