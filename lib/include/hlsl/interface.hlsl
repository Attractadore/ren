#pragma once
#include "interface.h"

#define CAT_HELPER(a, b) a##b
#define CAT(a, b) CAT_HELPER(a, b)
#define BREGISTER(r, s) register(CAT(b, r), CAT(space, s))
#define TREGISTER(r, s) register(CAT(t, r), CAT(space, s))

#if REFLECTION

#else

#if VERTEX_FETCH_PHYSICAL
#elif VERTEX_FETCH_LOGICAL
#elif VERTEX_FETCH_ATTRIBUTE
#else
#error "Vertex fetch not set"
#endif

#if ALBEDO_CONST
#elif ALBEDO_VERTEX
#else
#error "Albedo not set"
#endif

#if VERTEX_FETCH_PHYSICAL
constexpr VertexFetch VertexFetchConfig = Physical;
#elif VERTEX_FETCH_LOGICAL
constexpr VertexFetch VertexFetchConfig = Logical;
#elif VERTEX_FETCH_ATTRIBUTE
constexpr VertexFetch VertexFetchConfig = Attribute;
#endif

typedef PushConstantsTemplate<VertexFetchConfig> PushConstants;
template class PushConstantsTemplate<VertexFetchConfig>;

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
#if VERTEX_FETCH_ATTRIBUTE
  float3 position : POSITION;
#if ALBEDO_VERTEX
  float3 color : ALBEDO;
#endif
#endif
};

struct VS_OUT {
  float4 position : SV_Position;
#if ALBEDO_VERTEX
  float3 color : COLOR0;
#endif
};
typedef VS_OUT PS_IN;

#endif
