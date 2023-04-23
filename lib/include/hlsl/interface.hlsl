#pragma once
#include "interface.h"

template <typename T> T ptr_load(uint64_t base, uint idx) {
  return vk::RawBufferLoad<T>(base + idx * sizeof(T));
}

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float3 world_position : POSITION;
  float4 position : SV_Position;
  float4 color : COLOR0;
  float3 normal : NORMAL;
};
typedef VS_OUT FS_IN;
