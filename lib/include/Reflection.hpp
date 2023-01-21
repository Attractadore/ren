#pragma once
#include "Descriptors.hpp"
#include "Pipeline.hpp"

#include <span>

namespace ren {

struct DescriptorBindingReflection {
  unsigned set;
  DescriptorBinding binding;
};

class ReflectionModule {
public:
  virtual ~ReflectionModule() = default;

  virtual auto get_shader_stage() const -> VkShaderStageFlagBits = 0;

  virtual auto get_binding_count() const -> unsigned = 0;
  virtual void
  get_bindings(std::span<DescriptorBindingReflection> out) const = 0;

  virtual auto get_input_variable_count() const -> unsigned = 0;
  virtual void get_input_variables(std::span<VertexAttribute> out) const = 0;
};

} // namespace ren
