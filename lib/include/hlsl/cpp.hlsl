#pragma once
#if __cplusplus
#include <glm/glm.hpp>

#include <cstdint>
#endif

namespace ren {

#if __HLSL_VERSION
#if __spirv__
#define REN_HLSL_VULKAN 1
#else
#define REN_HLSL_DIRECTX12 1
#endif
#endif

// built-in types
#if __cplusplus
using uint = unsigned;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using float4x4 = glm::mat4x4;
#elif __HLSL_VERSION
#endif

// built-in functions
#if __cplusplus
auto lerp(const auto &x, const auto &y, const auto &a) {
  return glm::mix(x, y, a);
}
#elif __HLSL_VERSION
#endif

// assert
#if __cplusplus
#elif __HLSL_VERSION
#define assert(x)
#define static_assert(x)
#endif

// constexpr
#if __cplusplus
#elif __HLSL_VERSION
#define constexpr static const
#endif

} // namespace ren
