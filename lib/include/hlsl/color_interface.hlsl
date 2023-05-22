#pragma once
#include "color_interface.h"

struct VS_IN {
  uint index : SV_VertexID;
};

struct VS_OUT {
  float3 world_position : POSITION;
  float4 position : SV_Position;
  float4 color : COLOR0;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
};
typedef VS_OUT FS_IN;
