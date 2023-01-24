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
  ReflectionModule(std::span<const std::byte> code);

  auto get_shader_stage() const -> VkShaderStageFlagBits;

  void get_bindings(Vector<DescriptorBindingReflection> &out) const;
};

} // namespace ren
