#include "DirectX12/DirectX12Reflection.hpp"
#include "DirectX12/DirectX12ShaderStages.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"

namespace ren {
DirectX12ReflectionModule::DirectX12ReflectionModule(
    IDxcUtils *utils, std::span<const std::byte> data) {
  DxcBuffer reflection_desc = {.Ptr = data.data(), .Size = data.size()};
  throwIfFailed(
      utils->CreateReflection(&reflection_desc, IID_PPV_ARGS(&m_reflection)),
      "DXC: Failed to create shader reflection object");
  throwIfFailed(m_reflection->GetDesc(&m_desc),
                "DXC: Failed to get shader description");
}

auto DirectX12ReflectionModule::get_shader_stage() const -> ShaderStage {
  return getShaderStage(
      D3D12_SHADER_VERSION_TYPE((m_desc.Version & 0xFFFF0000) >> 16));
}

auto DirectX12ReflectionModule::get_binding_count() const -> unsigned {
  return m_desc.BoundResources;
}

namespace {
DescriptorType getDescriptorType(D3D_SHADER_INPUT_TYPE type,
                                 D3D_SRV_DIMENSION dimension) {
  switch (type) {
  default:
    unreachable("Unknown D3D_SHADER_INPUT_TYPE {}", int(type));
  case D3D_SIT_CBUFFER:
    return DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case D3D_SIT_TEXTURE: {
    if (dimension == D3D_SRV_DIMENSION_BUFFER) {
      return DESCRIPTOR_TYPE_TEXEL_BUFFER;
    }
    return DESCRIPTOR_TYPE_TEXTURE;
  }
  case D3D_SIT_SAMPLER:
    return DESCRIPTOR_TYPE_SAMPLER;
  case D3D_SIT_UAV_RWTYPED: {
    if (dimension == D3D_SRV_DIMENSION_BUFFER) {
      return DESCRIPTOR_TYPE_RW_TEXEL_BUFFER;
    }
    return DESCRIPTOR_TYPE_RW_TEXTURE;
  }
  case D3D_SIT_STRUCTURED:
    return DESCRIPTOR_TYPE_STRUCTURED_BUFFER;
  case D3D_SIT_UAV_RWSTRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
  case D3D_SIT_UAV_APPEND_STRUCTURED:
  case D3D_SIT_UAV_CONSUME_STRUCTURED:
    return DESCRIPTOR_TYPE_RW_STRUCTURED_BUFFER;
  case D3D_SIT_BYTEADDRESS:
    return DESCRIPTOR_TYPE_RAW_BUFFER;
  case D3D_SIT_UAV_RWBYTEADDRESS:
    return DESCRIPTOR_TYPE_RW_RAW_BUFFER;
  }
}
} // namespace

void DirectX12ReflectionModule::get_bindings(
    std::span<DescriptorBindingReflection> out) const {
  auto stage = get_shader_stage();
  auto num_bindings = get_binding_count();
  assert(out.size() >= num_bindings);
  ranges::transform(range(num_bindings), out.data(), [&](unsigned index) {
    D3D12_SHADER_INPUT_BIND_DESC binding_desc;
    throwIfFailed(m_reflection->GetResourceBindingDesc(index, &binding_desc),
                  "DXC: Failed to get shader binding");
    return DescriptorBindingReflection{
        .set = binding_desc.Space,
        .binding =
            {
                .binding = binding_desc.BindPoint,
                .type = getDescriptorType(binding_desc.Type,
                                          binding_desc.Dimension),
                .count = binding_desc.BindCount,
                .stages = stage,
            },
    };
  });
}

auto DirectX12ReflectionModule::get_input_variable_count() const -> unsigned {
  todo();
}

void DirectX12ReflectionModule::get_input_variables(
    std::span<VertexAttribute> out) const {
  todo();
}
} // namespace ren
