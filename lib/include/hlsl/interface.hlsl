#pragma once
#include "interface.h"

#if __spirv__
#define PUSH_CONSTANTS(type, name) [[vk::push_constant]] type name
#else
#define PUSH_CONSTANTS(type, name) ConstantBuffer<type> name
#endif

#if __spirv__
template <typename T> T ptr_load(uint64_t base, uint idx) {
  return vk::RawBufferLoad<T>(base + idx * sizeof(T));
}
#endif

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float4 position : SV_Position;
#if ALBEDO_VERTEX
  float3 color : COLOR0;
#endif
};
typedef VS_OUT PS_IN;
