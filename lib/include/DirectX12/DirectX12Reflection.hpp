#pragma once
#include "D3D12Shader.hpp"
#include "DXC.hpp"
#include "Reflection.hpp"
#include "Support/ComPtr.hpp"

namespace ren {

class DirectX12ReflectionModule final : public ReflectionModule {
  ComPtr<ID3D12ShaderReflection> m_reflection;
  D3D12_SHADER_DESC m_desc;

public:
  DirectX12ReflectionModule(IDxcUtils *utils, std::span<const std::byte> data);

  auto get_shader_stage() const -> ShaderStage override;

  auto get_binding_count() const -> unsigned override;
  void get_bindings(std::span<DescriptorBindingReflection> out) const override;

  auto get_input_variable_count() const -> unsigned override;
  void get_input_variables(std::span<VertexAttribute> out) const override;
};

} // namespace ren
