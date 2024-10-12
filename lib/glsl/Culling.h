#ifndef REN_GLSL_CULLING_H
#define REN_GLSL_CULLING_H

#include "Common.h"
#include "DevicePtr.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct InstanceCullData {
  uint mesh;
  uint mesh_instance;
};

GLSL_DEFINE_PTR_TYPE(InstanceCullData, 4);

struct MeshletCullData {
  uint mesh;
  uint mesh_instance;
  uint base_meshlet;
};

GLSL_DEFINE_PTR_TYPE(MeshletCullData, 4);

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

inline void get_cs_bb_min_max_z(ClipSpaceBoundingBox cs_bb,
                                GLSL_OUT(float) zmin, GLSL_OUT(float) zmax) {
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

inline void get_ndc_bb_min_max(NDCBoundingBox ndc_bb, GLSL_OUT(vec2) ndc_min,
                               GLSL_OUT(vec3) ndc_max) {
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

const uint MESHLET_CULLING_THREADS = 128;

const uint NUM_MESHLET_CULLING_BUCKETS = MESH_MESHLET_COUNT_BITS;

GLSL_NAMESPACE_END

#endif // REN_GLSL_CULLING_H
