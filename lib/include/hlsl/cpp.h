#pragma once
#if __cplusplus
#include <glm/glm.hpp>

#include <cstdint>
#endif

#if __cplusplus
#define REN_NAMESPACE_BEGIN namespace ren::hlsl {
#define REN_NAMESPACE_END }
#else
#define REN_NAMESPACE_BEGIN
#define REN_NAMESPACE_END
#endif

REN_NAMESPACE_BEGIN

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

REN_NAMESPACE_END
