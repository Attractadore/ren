#pragma once
#include "interface.hlsl"

REN_NAMESPACE_BEGIN

#if REN_HLSL_VULKAN
#define PUSH_CONSTANTS(type, name) [[vk::push_constant]] type name
#endif

#if REN_HLSL_VULKAN
template <typename T> T ptr_load(uint64_t base, uint idx) {
  return vk::RawBufferLoad(base + idx * sizeof(T));
}
#endif

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float4 position : SV_Position;
#if VERTEX_COLOR
  float3 color : COLOR0;
#endif
};
typedef VS_OUT PS_IN;

REN_NAMESPACE_END
