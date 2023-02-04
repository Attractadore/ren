#pragma once
#include "interface.h"

#if REFLECTION

#else

#if ALBEDO_CONST
#elif ALBEDO_VERTEX
#else
#error "Albedo not set"
#endif

template <typename T> T ptr_load(uint64_t base, uint idx) {
  return vk::RawBufferLoad<T>(base + idx * sizeof(T));
}

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float4 position : SV_Position;
#if ALBEDO_VERTEX
  float4 color : COLOR0;
#endif
};
typedef VS_OUT FS_IN;

#endif
