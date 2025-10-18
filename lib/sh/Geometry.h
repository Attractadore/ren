#pragma once
#include "Std.h"
#include "Transforms.h"

namespace ren::sh {

struct BoundingSquare {
  vec2 min;
  vec2 max;
};

struct BoundingBox {
  vec3 min;
  vec3 max;
};

struct Position {
  i16vec3 position;
};

struct PositionBoundingBox {
  Position min;
  Position max;
};

inline Position encode_position(vec3 position, float scale) {
  scale = float(1 << 15) * scale;
  Position eposition;
  eposition.position =
      i16vec3(min(ivec3(round(position * scale)), (1 << 15) - 1));
  return eposition;
}

inline vec3 decode_position(Position position) {
  return vec3(position.position);
}

inline PositionBoundingBox encode_bounding_box(BoundingBox bb, float scale) {
  PositionBoundingBox pbb;
  pbb.min = encode_position(bb.min, scale);
  pbb.max = encode_position(bb.max, scale);
  return pbb;
}

inline BoundingBox decode_bounding_box(PositionBoundingBox pbb) {
  BoundingBox bb;
  bb.min = decode_position(pbb.min);
  bb.max = decode_position(pbb.max);
  return bb;
}

inline mat4 make_encode_position_matrix(float scale) {
  scale = float(1 << 15) * scale;
  mat4 m = mat4(1.0f);
  m[0][0] = m[1][1] = m[2][2] = scale;
  return m;
}

inline mat4 make_decode_position_matrix(float scale) {
  scale = 1.0f / (scale * float(1 << 15));
  mat4 m = mat4(1.0f);
  m[0][0] = m[1][1] = m[2][2] = scale;
  return m;
}

struct Normal {
  u16vec2 normal;
};

inline vec2 oct_wrap(vec2 v) {
  return (1.0f - abs(vec2(v.y, v.x))) *
         mix(vec2(-1.0f), vec2(1.0f), greaterThanEqual(v, vec2(0.0f)));
}

inline Normal encode_normal(vec3 normal) {
  normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
  vec2 xy = vec2(normal);
  xy = normal.z >= 0.0f ? xy : oct_wrap(xy);
  xy = xy * 0.5f + 0.5f;
  Normal enormal;
  enormal.normal =
      u16vec2(min(uvec2(round(xy * float(1 << 16))), (1u << 16) - 1));
  return enormal;
}

inline vec3 decode_normal(Normal normal) {
  vec2 xy = vec2(normal.normal) / float(1 << 16);
  xy = xy * 2.0f - 1.0f;
  float z = 1.0f - abs(xy.x) - abs(xy.y);
  xy = z >= 0.0f ? xy : oct_wrap(xy);
  return normalize(vec3(xy, z));
}

struct Tangent {
  uint16_t tangent_and_sign;
};

inline float sq_wrap(float v) {
  return (2.0f - abs(v)) * (v >= 0.0f ? 1.0f : -1.0f);
}

inline Tangent encode_tangent(vec4 tangent, vec3 normal) {
  vec3 t1 = normalize(make_orthogonal_vector(normal));
  vec3 t2 = cross(normal, t1);
  vec2 xy = vec2(dot(vec3(tangent), t1), dot(vec3(tangent), t2));
  float x = xy.x / (abs(xy.x) + abs(xy.y));
  x = xy.y >= 0.0f ? x : sq_wrap(x);
  x = x * 0.25f + 0.5f;
  uint tangent_and_sign = min(uint(round(x * float(1 << 15))), (1u << 15) - 1);
  tangent_and_sign |= (tangent.w < 0.0f) ? (1 << 15) : 0;
  Tangent etangent;
  etangent.tangent_and_sign = uint16_t(tangent_and_sign);
  return etangent;
}

inline vec4 decode_tangent(Tangent tangent, vec3 normal) {
  vec3 t1 = normalize(make_orthogonal_vector(normal));
  vec3 t2 = cross(normal, t1);
  uint tangent_and_sign = tangent.tangent_and_sign;
  float x = (tangent_and_sign & ((1 << 15) - 1)) / float(1 << 15);
  x = x * 4.0f - 2.0f;
  float y = 1.0f - abs(x);
  x = y >= 0.0f ? x : sq_wrap(x);
  vec2 xy = normalize(vec2(x, y));
  float sign = bool(tangent_and_sign & (1 << 15)) ? -1.0f : 1.0f;
  return vec4(t1 * xy.x + t2 * xy.y, sign);
}

struct UV {
  u16vec2 uv;
};

inline UV encode_uv(vec2 uv, BoundingSquare bs) {
  vec2 fuv = float(1 << 16) * (uv - bs.min) / (bs.max - bs.min);
  fuv = clamp(round(fuv), 0.0f, float((1 << 16) - 1));
  UV euv;
  euv.uv = u16vec2(fuv);
  return euv;
}

inline vec2 decode_uv(UV uv, BoundingSquare bs) {
  return mix(bs.min, bs.max, vec2(uv.uv) / float(1 << 16));
}

struct Color {
  u8vec4 color;
};

inline Color encode_color(vec4 color) {
  Color ecolor;
  ecolor.color = u8vec4(clamp(round(color * 255.0f), 0.0f, 255.0f));
  return ecolor;
}

inline vec4 decode_color(Color color) { return vec4(color.color) / 255.0f; }

static const uint MESH_ATTRIBUTE_UV_BIT = 1 << 0;
static const uint MESH_ATTRIBUTE_TANGENT_BIT = 1 << 1;
static const uint MESH_ATTRIBUTE_COLOR_BIT = 1 << 2;

static const uint NUM_MESH_ATTRIBUTE_FLAGS =
    (MESH_ATTRIBUTE_UV_BIT | MESH_ATTRIBUTE_TANGENT_BIT |
     MESH_ATTRIBUTE_COLOR_BIT) +
    1;

static const uint MAX_NUM_INDEX_POOL_BITS = 8;
static const uint MAX_NUM_INDEX_POOLS = 1 << MAX_NUM_INDEX_POOL_BITS;

static const uint INDEX_POOL_SIZE = 1 << 24;

static const uint NUM_MESHLET_VERTICES = 64;
static const uint NUM_MESHLET_TRIANGLES = 124;

static const uint MESH_MESHLET_COUNT_BITS = 15;
static const uint MAX_NUM_MESH_MESHLETS = 1 << MESH_MESHLET_COUNT_BITS;

struct Meshlet {
  uint base_index;
  uint base_triangle;
  uint num_triangles;
  Position cone_apex;
  Position cone_axis;
  float cone_cutoff;
  PositionBoundingBox bb;
};

static const uint MAX_NUM_LODS = 8;

struct MeshLOD {
  uint base_meshlet;
  uint num_meshlets;
  uint num_triangles;
};

struct Mesh {
  DevicePtr<Position> positions;
  DevicePtr<Normal> normals;
  DevicePtr<Tangent> tangents;
  DevicePtr<UV> uvs;
  DevicePtr<Color> colors;
  DevicePtr<Meshlet> meshlets;
  DevicePtr<uint> meshlet_indices;
  PositionBoundingBox bb;
  BoundingSquare uv_bs;
  uint index_pool;
  uint num_lods;
  MeshLOD lods[MAX_NUM_LODS];
};

struct MeshInstance {
  uint mesh;
  uint material;
};

typedef uint BatchId;

typedef uint MeshInstanceVisibilityMask;
static const uint MESH_INSTANCE_VISIBILITY_MASK_BIT_SIZE =
    sizeof(MeshInstanceVisibilityMask) * 8;

static const uint MAX_DRAW_MESHLETS = 4 * 1024 * 1024;

struct DrawSetItem {
  uint mesh;
  uint mesh_instance;
  BatchId batch;
};

struct MeshletCullData {
  uint mesh;
  uint mesh_instance;
  BatchId batch;
  uint base_meshlet;
};

struct MeshletDrawCommand {
  uint num_triangles;
  uint base_triangle;
  uint base_index;
  uint mesh_instance;
};

struct ClipSpaceBoundingBox {
  vec4 p[8];
};

inline ClipSpaceBoundingBox project_bb_to_cs(mat4 pvm,
                                             PositionBoundingBox pbb) {
  BoundingBox bb = decode_bounding_box(pbb);

  vec3 bbs = bb.max - bb.min;

  vec4 px = pvm * vec4(bbs.x, 0.0f, 0.0f, 0.0f);
  vec4 py = pvm * vec4(0.0f, bbs.y, 0.0f, 0.0f);
  vec4 pz = pvm * vec4(0.0f, 0.0f, bbs.z, 0.0f);

  ClipSpaceBoundingBox cs_bb;
  cs_bb.p[0] = pvm * vec4(bb.min, 1.0f);
  cs_bb.p[1] = cs_bb.p[0] + px;
  cs_bb.p[2] = cs_bb.p[1] + py;
  cs_bb.p[3] = cs_bb.p[0] + py;
  cs_bb.p[4] = cs_bb.p[0] + pz;
  cs_bb.p[5] = cs_bb.p[1] + pz;
  cs_bb.p[6] = cs_bb.p[2] + pz;
  cs_bb.p[7] = cs_bb.p[3] + pz;

  return cs_bb;
}

inline void get_cs_bb_min_max_z(ClipSpaceBoundingBox cs_bb, SH_OUT(float) zmin,
                                SH_OUT(float) zmax) {
  zmin = cs_bb.p[0].w;
  zmax = cs_bb.p[0].w;
  for (int i = 1; i < 8; ++i) {
    zmin = min(zmin, cs_bb.p[i].w);
    zmax = max(zmax, cs_bb.p[i].w);
  }
}

struct NDCBoundingBox {
  vec3 ndc[8];
};

inline NDCBoundingBox convert_cs_bb_to_ndc(ClipSpaceBoundingBox cs_bb) {
  NDCBoundingBox ndc_bb;
  for (int i = 0; i < 8; ++i) {
    ndc_bb.ndc[i] = vec3(cs_bb.p[i]) / cs_bb.p[i].w;
  }
  return ndc_bb;
}

inline void get_ndc_bb_min_max(NDCBoundingBox ndc_bb, SH_OUT(vec2) ndc_min,
                               SH_OUT(vec3) ndc_max) {
  ndc_min = vec2(ndc_bb.ndc[0]);
  ndc_max = ndc_bb.ndc[0];
  for (int i = 1; i < 8; ++i) {
    ndc_min = min(ndc_min, vec2(ndc_bb.ndc[i]));
    ndc_max = max(ndc_max, ndc_bb.ndc[i]);
  }
}

/// Assumes reverse-Z.
inline bool frustum_cull(vec2 ndc_min, vec3 ndc_max) {
  return any(lessThan(ndc_max, vec3(-1.0f, -1.0f, 0.0f))) ||
         any(greaterThan(ndc_min, vec2(1.0f)));
}

inline float get_ndc_bb_area(NDCBoundingBox ndc_bb) {
  const uvec4 faces[6] = {
      // Top
      uvec4(7, 5, 6, 4),
      // Bottom
      uvec4(3, 1, 2, 0),
      // Right
      uvec4(5, 2, 6, 1),
      // Left
      uvec4(4, 3, 7, 0),
      // Front
      uvec4(4, 1, 5, 0),
      // Back
      uvec4(7, 2, 6, 3),
  };
  // Compute total front and back-facing projected area.
  float area = 0.0f;
  for (int f = 0; f < 6; ++f) {
    uvec4 face = faces[f];
    area += abs(
        determinant(mat2(vec2(ndc_bb.ndc[face.x]) - vec2(ndc_bb.ndc[face.y]),
                         vec2(ndc_bb.ndc[face.z]) - vec2(ndc_bb.ndc[face.w]))));
  }
  // Face area is half of abs of det of matrix of diagonals, and was counted
  // twice.
  area /= 4.0f;
  return area;
}

#if __SLANG__

// Assume reverse-Z, ndc_max.z contains the nearest point's depth value.
bool occlusion_cull(Sampler2D hi_z, vec2 ndc_min, vec3 ndc_max) {
  vec2 uv_min = vec2(ndc_min.x, -ndc_max.y) * 0.5f + 0.5f;
  vec2 uv_max = vec2(ndc_max.x, -ndc_min.y) * 0.5f + 0.5f;
  vec2 box_size = TextureSize(hi_z);
  box_size = box_size * (uv_max - uv_min);
  float l = max(box_size.x, box_size.y);
  float mip = ceil(log2(max(l, 1.0f)));
  float hi_z_depth = 1.0f;
  hi_z_depth =
      min(hi_z_depth, hi_z.SampleLevel(vec2(uv_min.x, uv_min.y), mip).r);
  hi_z_depth =
      min(hi_z_depth, hi_z.SampleLevel(vec2(uv_min.x, uv_max.y), mip).r);
  hi_z_depth =
      min(hi_z_depth, hi_z.SampleLevel(vec2(uv_max.x, uv_min.y), mip).r);
  hi_z_depth =
      min(hi_z_depth, hi_z.SampleLevel(vec2(uv_max.x, uv_max.y), mip).r);
  return hi_z_depth > ndc_max.z;
}

#endif // __SLANG__

} // namespace ren::sh
