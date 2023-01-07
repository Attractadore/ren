#pragma once
#include "ShaderStages.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_MAP_TYPE(ShaderStage, VkShaderStageFlagBits);
REN_ENUM_FLAGS(VkShaderStageFlagBits, VkShaderStageFlags);
REN_MAP_FIELD(ShaderStage::Vertex, VK_SHADER_STAGE_VERTEX_BIT);
REN_MAP_FIELD(ShaderStage::Fragment, VK_SHADER_STAGE_FRAGMENT_BIT);
REN_MAP_FIELD(ShaderStage::Compute, VK_SHADER_STAGE_COMPUTE_BIT);
REN_MAP_ENUM_AND_FLAGS(getVkShaderStage, ShaderStage, REN_SHADER_STAGES);
REN_REVERSE_MAP_ENUM_AND_FLAGS(getShaderStage, ShaderStage, REN_SHADER_STAGES);

} // namespace ren
