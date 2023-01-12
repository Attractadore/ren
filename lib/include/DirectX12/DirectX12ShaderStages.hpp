#pragma once
#include "D3D12Shader.hpp"
#include "ShaderStages.hpp"

namespace ren {

REN_MAP_TYPE(ShaderStage, D3D12_SHADER_VERSION_TYPE);
REN_MAP_FIELD(ShaderStage::Vertex, D3D12_SHVER_VERTEX_SHADER);
REN_MAP_FIELD(ShaderStage::Fragment, D3D12_SHVER_PIXEL_SHADER);
REN_MAP_FIELD(ShaderStage::Compute, D3D12_SHVER_COMPUTE_SHADER);
REN_REVERSE_MAP_ENUM(getShaderStage, ShaderStage, REN_SHADER_STAGES);

} // namespace ren
