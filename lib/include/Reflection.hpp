#pragma once
#include "Descriptors.hpp"

#include <span>

namespace ren {

struct DescriptorSetBindingReflection {
  unsigned set;
  DescriptorSetBinding binding;
};

class ReflectionModule {
public:
  virtual ~ReflectionModule() = default;

  virtual auto get_shader_stage() const -> ShaderStage = 0;
  virtual auto get_binding_count() const -> unsigned = 0;
  virtual void get_bindings(std::span<DescriptorSetBindingReflection> out) = 0;
};

}
