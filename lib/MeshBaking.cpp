#include "Mesh.hpp"
#include "MeshSimplification.hpp"
#include "core/Math.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Array.hpp"
#include "ren/core/Span.hpp"
#include "sh/Transforms.h"

#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>
#include <mikktspace.h>

namespace ren {

struct MeshRemapVertexStreamsOptions {
  usize num_vertices = 0;
  usize num_unique_vertices = 0;
  NotNull<glm::vec3 **> positions;
  NotNull<glm::vec3 **> normals;
  NotNull<glm::vec4 **> tangents;
  NotNull<glm::vec2 **> uvs;
  NotNull<glm::vec4 **> colors;
  const u32 *remap = nullptr;
};

void mesh_remap_vertex_streams(NotNull<Arena *> arena,
                               const MeshRemapVertexStreamsOptions &opts) {
  auto remap_stream = [&]<typename T>(NotNull<T **> stream) {
    if (*stream) {
      T *remaped_stream = arena->allocate<T>(opts.num_unique_vertices);
      meshopt_remapVertexBuffer(remaped_stream, *stream, opts.num_vertices,
                                sizeof(T), opts.remap);
      *stream = remaped_stream;
    }
  };
  remap_stream(opts.positions);
  remap_stream(opts.normals);
  remap_stream(opts.tangents);
  remap_stream(opts.uvs);
  remap_stream(opts.colors);
}

struct MeshGenerateIndicesOptions {
  NotNull<usize *> num_vertices;
  NotNull<glm::vec3 **> positions;
  NotNull<glm::vec3 **> normals;
  NotNull<glm::vec4 **> tangents;
  NotNull<glm::vec2 **> uvs;
  NotNull<glm::vec4 **> colors;
  NotNull<Span<u32> *> indices;
};

void mesh_generate_indices(NotNull<Arena *> arena,
                           const MeshGenerateIndicesOptions &opts) {
  const u32 *indices = nullptr;
  usize num_vertices = *opts.num_vertices;
  usize num_indices = num_vertices;
  if (not opts.indices->empty()) {
    indices = opts.indices->data();
    num_indices = opts.indices->size();
  };

  u32 num_streams = 0;
  meshopt_Stream streams[5];
  auto add_stream = [&]<typename T>(T *stream) {
    if (stream) {
      streams[num_streams++] = {
          .data = stream,
          .size = sizeof(T),
          .stride = sizeof(T),
      };
    }
  };
  add_stream(*opts.positions);
  add_stream(*opts.normals);
  add_stream(*opts.tangents);
  add_stream(*opts.uvs);
  add_stream(*opts.colors);

  ScratchArena scratch(arena);
  u32 *remap = scratch->allocate<u32>(num_vertices);
  num_vertices = meshopt_generateVertexRemapMulti(
      remap, indices, num_indices, num_vertices, streams, num_streams);

  mesh_remap_vertex_streams(arena, {
                                       .num_vertices = *opts.num_vertices,
                                       .num_unique_vertices = num_vertices,
                                       .positions = opts.positions,
                                       .normals = opts.normals,
                                       .tangents = opts.tangents,
                                       .uvs = opts.uvs,
                                       .colors = opts.colors,
                                       .remap = remap,
                                   });
  *opts.num_vertices = num_vertices;

  *opts.indices = Span<u32>::allocate(arena, num_indices);
  meshopt_remapIndexBuffer(opts.indices->data(), indices, num_indices, remap);
}

struct MeshGenerateTangentsOptions {
  NotNull<usize *> num_vertices;
  NotNull<glm::vec3 **> positions;
  NotNull<glm::vec3 **> normals;
  NotNull<glm::vec4 **> tangents;
  NotNull<glm::vec2 **> uvs;
  NotNull<glm::vec4 **> colors;
  NotNull<Span<u32> *> indices;
};

void mesh_generate_tangents(NotNull<Arena *> arena,
                            const MeshGenerateTangentsOptions &opts) {
  ScratchArena scratch(arena);

  auto unindex_stream = [&]<typename T>(NotNull<T **> stream) {
    T *unindexed_stream = scratch->allocate<T>(opts.indices->size());
    for (usize i = 0; i < opts.indices->size(); ++i) {
      usize index = (*opts.indices)[i];
      unindexed_stream[i] = (*stream)[index];
    }
    *stream = unindexed_stream;
  };

  *opts.num_vertices = opts.indices->size();
  unindex_stream(opts.positions);
  unindex_stream(opts.normals);
  *opts.tangents = scratch->allocate<glm::vec4>(opts.indices->size());
  unindex_stream(opts.uvs);
  if (*opts.colors) {
    unindex_stream(opts.colors);
  }
  *opts.indices = {};

  struct Context {
    size_t num_faces = 0;
    const glm::vec3 *positions = nullptr;
    const glm::vec3 *normals = nullptr;
    glm::vec4 *tangents = nullptr;
    const glm::vec2 *uvs = nullptr;
  };

  SMikkTSpaceInterface iface = {
      .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
        return ((const Context *)(pContext->m_pUserData))->num_faces;
      },

      .m_getNumVerticesOfFace = [](const SMikkTSpaceContext *,
                                   const int) -> int { return 3; },

      .m_getPosition =
          [](const SMikkTSpaceContext *pContext, float fvPosOut[],
             const int iFace, const int iVert) {
            glm::vec3 position = ((const Context *)(pContext->m_pUserData))
                                     ->positions[iFace * 3 + iVert];
            fvPosOut[0] = position.x;
            fvPosOut[1] = position.y;
            fvPosOut[2] = position.z;
          },

      .m_getNormal =
          [](const SMikkTSpaceContext *pContext, float fvNormOut[],
             const int iFace, const int iVert) {
            glm::vec3 normal = ((const Context *)(pContext->m_pUserData))
                                   ->normals[iFace * 3 + iVert];
            fvNormOut[0] = normal.x;
            fvNormOut[1] = normal.y;
            fvNormOut[2] = normal.z;
          },

      .m_getTexCoord =
          [](const SMikkTSpaceContext *pContext, float fvTexcOut[],
             const int iFace, const int iVert) {
            glm::vec2 tex_coord = ((const Context *)(pContext->m_pUserData))
                                      ->uvs[iFace * 3 + iVert];
            fvTexcOut[0] = tex_coord.x;
            fvTexcOut[1] = tex_coord.y;
          },

      .m_setTSpaceBasic =
          [](const SMikkTSpaceContext *pContext, const float fvTangent[],
             const float fSign, const int iFace, const int iVert) {
            glm::vec4 &tangent = ((const Context *)(pContext->m_pUserData))
                                     ->tangents[iFace * 3 + iVert];
            tangent.x = fvTangent[0];
            tangent.y = fvTangent[1];
            tangent.z = fvTangent[2];
            tangent.w = -fSign;
          },
  };

  Context user_data = {
      .num_faces = *opts.num_vertices / 3,
      .positions = *opts.positions,
      .normals = *opts.normals,
      .tangents = *opts.tangents,
      .uvs = *opts.uvs,
  };

  SMikkTSpaceContext ctx = {
      .m_pInterface = &iface,
      .m_pUserData = &user_data,
  };

  genTangSpaceDefault(&ctx);

  mesh_generate_indices(arena, {
                                   .num_vertices = opts.num_vertices,
                                   .positions = opts.positions,
                                   .normals = opts.normals,
                                   .tangents = opts.tangents,
                                   .uvs = opts.uvs,
                                   .colors = opts.colors,
                                   .indices = opts.indices,
                               });
}

void mesh_compute_bounds(Span<const glm::vec3> positions,
                         NotNull<sh::PositionBoundingBox *> pbb,
                         NotNull<float *> scale) {
  sh::BoundingBox bb = {
      .min = glm::vec3(std::numeric_limits<float>::infinity()),
      .max = -glm::vec3(std::numeric_limits<float>::infinity()),
  };

  // Select relatively big default size to avoid log2 NaN.
  float size = 1.0f;
  for (const glm::vec3 &position : positions) {
    glm::vec3 abs_position = glm::abs(position);
    size = max({size, abs_position.x, abs_position.y, abs_position.z});
    bb.min = glm::min(bb.min, position);
    bb.max = glm::max(bb.max, position);
  }
  *scale = glm::exp2(-glm::ceil(glm::log2(size)));

  *pbb = sh::encode_bounding_box(bb, *scale);
}

sh::Position *mesh_encode_positions(NotNull<Arena *> arena,
                                    Span<const glm::vec3> positions,
                                    float scale) {
  auto *enc_positions = arena->allocate<sh::Position>(positions.size());
  for (usize i = 0; i < positions.size(); ++i) {
    enc_positions[i] = sh::encode_position(positions[i], scale);
  }
  return enc_positions;
}

sh::Normal *mesh_encode_normals(NotNull<Arena *> arena,
                                Span<const glm::vec3> normals, float scale) {
  glm::mat3 encode_transform_matrix = sh::make_encode_position_matrix(scale);
  glm::mat3 encode_normal_matrix = sh::normal(encode_transform_matrix);

  auto *enc_normals = arena->allocate<sh::Normal>(normals.size());
  for (usize i = 0; i < normals.size(); ++i) {
    enc_normals[i] =
        sh::encode_normal(glm::normalize(encode_normal_matrix * normals[i]));
  }

  return enc_normals;
}

sh::Tangent *mesh_encode_tangents(NotNull<Arena *> arena,
                                  Span<const glm::vec4> tangents, float scale,
                                  Span<const sh::Normal> enc_normals) {
  glm::mat3 encode_transform_matrix = sh::make_encode_position_matrix(scale);

  auto *enc_tangents = arena->allocate<sh::Tangent>(tangents.size());
  for (usize i = 0; i < tangents.size(); ++i) {
    // Encoding and then decoding the normal can change how the
    // tangent basis is selected due to rounding errors. Since
    // shaders use the decoded normal to decode the tangent, use
    // it for encoding as well.
    glm::vec3 normal = sh::decode_normal(enc_normals[i]);

    // Orthonormalize tangent space.
    glm::vec4 tangent = tangents[i];
    glm::vec3 tangent3d(tangent);
    float sign = tangent.w;
    float proj = glm::dot(normal, tangent3d);
    tangent3d = tangent3d - proj * normal;

    tangent =
        glm::vec4(glm::normalize(encode_transform_matrix * tangent3d), sign);
    enc_tangents[i] = sh::encode_tangent(tangent, normal);
  };

  return enc_tangents;
}

sh::UV *mesh_encode_uvs(NotNull<Arena *> arena, Span<const glm::vec2> uvs,
                        NotNull<sh::BoundingSquare *> uv_bs) {
  for (glm::vec2 uv : uvs) {
    uv_bs->min = glm::min(uv_bs->min, uv);
    uv_bs->max = glm::max(uv_bs->max, uv);
  }

  // Round off the minimum and the maximum of the bounding square to the next
  // power of 2 if they are not equal to 0
  {
    // Select a relatively big default square size to avoid log2 NaN
    glm::vec2 p = glm::log2(glm::max(glm::max(-uv_bs->min, uv_bs->max), 1.0f));
    glm::vec2 bs = glm::exp2(glm::ceil(p));
    uv_bs->min = glm::mix(glm::vec2(0.0f), -bs,
                          glm::notEqual(uv_bs->min, glm::vec2(0.0f)));
    uv_bs->max = glm::mix(glm::vec2(0.0f), bs,
                          glm::notEqual(uv_bs->max, glm::vec2(0.0f)));
  }

  auto *enc_uvs = arena->allocate<sh::UV>(uvs.size());
  for (usize i = 0; i < uvs.size(); ++i) {
    enc_uvs[i] = sh::encode_uv(uvs[i], *uv_bs);
  }

  return enc_uvs;
}

[[nodiscard]] sh::Color *mesh_encode_colors(NotNull<Arena *> arena,
                                            Span<const glm::vec4> colors) {
  auto *enc_colors = arena->allocate<sh::Color>(colors.size());
  for (usize i = 0; i < colors.size(); ++i) {
    enc_colors[i] = sh::encode_color(colors[i]);
  }
  return enc_colors;
}

struct MeshGenerateMeshletsOptions {
  Span<const glm::vec3> positions;
  Span<const u32> indices;
  Span<const LOD> lods;
  NotNull<sh::Meshlet **> meshlets;
  NotNull<u32 **> meshlet_indices;
  NotNull<u8 **> meshlet_triangles;
  NotNull<MeshPackageHeader *> header;
  float cone_weight = 0.0f;
};

void mesh_generate_meshlets(NotNull<Arena *> arena,
                            const MeshGenerateMeshletsOptions &opts) {
  ren_assert(opts.header->scale != 0.0f);

  ScratchArena scratch(arena);
  auto *meshlets =
      scratch->allocate<meshopt_Meshlet>(meshopt_buildMeshletsBound(
          opts.lods[0].num_indices, sh::NUM_MESHLET_VERTICES,
          sh::NUM_MESHLET_TRIANGLES));

  struct MeshletLod {
    Span<const sh::Meshlet> meshlets;
    Span<const u32> indices;
    Span<const u8> triangles;
  };
  DynamicArray<MeshletLod> lods;

  usize base_lod_meshlet = 0;
  usize base_lod_index = 0;
  usize base_lod_triangle = 0;

  for (isize l = opts.lods.size() - 1; l >= 0; --l) {
    const LOD &lod = opts.lods[l];
    ren_assert(3 * base_lod_triangle == lod.base_index);

    u32 num_lod_meshlets = meshopt_buildMeshletsBound(
        lod.num_indices, sh::NUM_MESHLET_VERTICES, sh::NUM_MESHLET_TRIANGLES);

    auto gpu_meshlets = Span<sh::Meshlet>::allocate(scratch, num_lod_meshlets);
    auto meshlet_indices = Span<u32>::allocate(
        scratch, num_lod_meshlets * sh::NUM_MESHLET_VERTICES);
    auto meshlet_triangles = Span<u8>::allocate(
        scratch, num_lod_meshlets * sh::NUM_MESHLET_TRIANGLES * 3);

    num_lod_meshlets = meshopt_buildMeshlets(
        meshlets, meshlet_indices.data(), meshlet_triangles.data(),
        &opts.indices[lod.base_index], lod.num_indices,
        (const float *)opts.positions.data(), opts.positions.size(),
        sizeof(glm::vec3), sh::NUM_MESHLET_VERTICES, sh::NUM_MESHLET_TRIANGLES,
        opts.cone_weight);

    usize num_lod_indices = 0;
    usize num_lod_triangles = 0;
    for (usize m = 0; m < num_lod_meshlets; ++m) {
      const meshopt_Meshlet &meshlet = meshlets[m];
      ren_assert(num_lod_indices == meshlet.vertex_offset);

      sh::Meshlet gpu_meshlet = {
          .base_index = (u32)(base_lod_index + num_lod_indices),
          .base_triangle = (u32)((base_lod_triangle + num_lod_triangles) * 3),
          .num_triangles = meshlet.triangle_count,
      };

      auto indices =
          Span(&meshlet_indices[meshlet.vertex_offset], meshlet.vertex_count);

      auto triangles = Span(&meshlet_triangles[meshlet.triangle_offset],
                            meshlet.triangle_count * 3);

      // Optimize meshlet.
      // TODO: replace with meshopt_optimizeMeshlet

      u8 opt_triangles[sh::NUM_MESHLET_TRIANGLES * 3];
      ren_assert(std::size(opt_triangles) >= triangles.size());
      meshopt_optimizeVertexCache(opt_triangles, triangles.data(),
                                  triangles.size(), meshlet.vertex_count);
      triangles = {opt_triangles, triangles.size()};

      u32 opt_indices[sh::NUM_MESHLET_VERTICES];
      ren_assert(std::size(opt_indices) >= indices.size());
      usize num_indices = meshopt_optimizeVertexFetch(
          opt_indices, triangles.data(), triangles.size(), indices.data(),
          indices.size(), sizeof(u32));
      ren_assert(num_indices == indices.size());
      indices = {opt_indices, indices.size()};

      // Compact triangle buffer.
      copy(triangles, &meshlet_triangles[num_lod_triangles * 3]);
      copy(indices, &meshlet_indices[num_lod_indices]);

      meshopt_Bounds bounds = meshopt_computeMeshletBounds(
          indices.data(), triangles.data(), meshlet.triangle_count,
          (const float *)opts.positions.data(), opts.positions.size(),
          sizeof(glm::vec3));
      glm::vec3 cone_apex = glm::make_vec3(bounds.cone_apex);
      glm::vec3 cone_axis = glm::make_vec3(bounds.cone_axis);

      gpu_meshlet.cone_apex =
          sh::encode_position(cone_apex, opts.header->scale),
      gpu_meshlet.cone_axis =
          sh::encode_position(cone_axis, opts.header->scale),
      gpu_meshlet.cone_cutoff = bounds.cone_cutoff;

      sh::BoundingBox bb = {
          .min = glm::vec3(std::numeric_limits<float>::infinity()),
          .max = -glm::vec3(std::numeric_limits<float>::infinity()),
      };

      for (usize t : triangles) {
        usize index = indices[t];
        const glm::vec3 &position = opts.positions[index];
        bb.min = glm::min(bb.min, position);
        bb.max = glm::max(bb.max, position);
      }

      gpu_meshlet.bb = sh::encode_bounding_box(bb, opts.header->scale);

      gpu_meshlets[m] = gpu_meshlet;

      num_lod_indices += meshlet.vertex_count;
      num_lod_triangles += meshlet.triangle_count;
    }
    ren_assert(num_lod_triangles * 3 == lod.num_indices);

    base_lod_meshlet += num_lod_meshlets;
    base_lod_index += num_lod_indices;
    base_lod_triangle += num_lod_triangles;

    lods.push(
        scratch,
        {
            .meshlets = gpu_meshlets.subspan(0, num_lod_meshlets),
            .indices = meshlet_indices.subspan(0, num_lod_indices),
            .triangles = meshlet_triangles.subspan(0, 3 * num_lod_triangles),
        });
  }
  ren_assert(3 * base_lod_triangle == opts.indices.size());

  opts.header->num_vertices = opts.positions.size();
  *opts.meshlets = arena->allocate<sh::Meshlet>(base_lod_meshlet);
  opts.header->num_meshlets = base_lod_meshlet;
  *opts.meshlet_indices = arena->allocate<u32>(base_lod_index);
  opts.header->num_indices = base_lod_index;
  *opts.meshlet_triangles = arena->allocate<u8>(3 * base_lod_triangle);
  opts.header->num_triangles = base_lod_triangle;

  base_lod_meshlet = 0;
  base_lod_index = 0;
  base_lod_triangle = 0;
  opts.header->num_lods = lods.m_size;
  for (usize lod : range(lods.m_size)) {
    opts.header->lods[lod] = sh::MeshLOD{
        .base_meshlet = (u32)base_lod_meshlet,
        .num_meshlets = (u32)lods[lod].meshlets.size(),
        .num_triangles = (u32)lods[lod].triangles.size() / 3,
    };
    copy(lods[lod].meshlets, &(*opts.meshlets)[base_lod_meshlet]);
    copy(lods[lod].indices, &(*opts.meshlet_indices)[base_lod_index]);
    copy(lods[lod].triangles,
         &(*opts.meshlet_triangles)[3 * base_lod_triangle]);
    base_lod_meshlet += lods[lod].meshlets.size();
    base_lod_index += lods[lod].indices.size();
    base_lod_triangle += lods[lod].triangles.size() / 3;
  }
}

struct BakedMesh {
  MeshPackageHeader header;
  usize size = 0;
  sh::Position *positions = nullptr;
  sh::Normal *normals = nullptr;
  sh::Tangent *tangents = nullptr;
  sh::UV *uvs = nullptr;
  sh::Color *colors = nullptr;
  sh::Meshlet *meshlets = nullptr;
  u32 *indices = nullptr;
  u8 *triangles = nullptr;
};

BakedMesh bake_mesh(NotNull<Arena *> arena, const MeshInfo &info) {
  usize num_vertices = info.num_vertices;
  glm::vec3 *positions = (glm::vec3 *)info.positions.get();
  glm::vec3 *normals = (glm::vec3 *)info.normals.get();
  glm::vec4 *tangents = (glm::vec4 *)info.tangents;
  glm::vec2 *uvs = (glm::vec2 *)info.uvs;
  glm::vec4 *colors = (glm::vec4 *)info.colors;
  Span<u32> indices =
      Span<u32>((u32 *)info.indices.data(), info.indices.size());

  ren_assert(num_vertices > 0);
  if (indices.size() > 0) {
    ren_assert(indices.size() % 3 == 0);
  } else {
    ren_assert(num_vertices % 3 == 0);
  }

  ScratchArena scratch(arena);

  BakedMesh mesh;

  // (Re)generate index buffer to remove duplicate vertices for LOD generation
  // to work correctly

  mesh_generate_indices(scratch, {
                                     .num_vertices = &num_vertices,
                                     .positions = &positions,
                                     .normals = &normals,
                                     .tangents = &tangents,
                                     .uvs = &uvs,
                                     .colors = &colors,
                                     .indices = &indices,
                                 });

  // Generate tangents

  if (uvs and !tangents) {
    mesh_generate_tangents(scratch, {
                                        .num_vertices = &num_vertices,
                                        .positions = &positions,
                                        .normals = &normals,
                                        .tangents = &tangents,
                                        .uvs = &uvs,
                                        .colors = &colors,
                                        .indices = &indices,
                                    });
  }

  // Generate LODs

  u32 num_lods = sh::MAX_NUM_LODS;
  LOD lods[sh::MAX_NUM_LODS];
  mesh_simplify(scratch, {
                             .num_vertices = num_vertices,
                             .positions = positions,
                             .normals = normals,
                             .tangents = tangents,
                             .uvs = uvs,
                             .colors = colors,
                             .indices = &indices,
                             .num_lods = &num_lods,
                             .lods = lods,
                         });

  // Optimize each LOD separately

  Span opt_indices = Span<u32>::allocate(scratch, indices.size());
  for (const LOD &lod : Span(lods, num_lods)) {
    meshopt_optimizeVertexCache(&opt_indices[lod.base_index],
                                &indices[lod.base_index], lod.num_indices,
                                num_vertices);
  }
  indices = opt_indices;

#if 0
  // Optimize all LODs together.

  {
    u32 *remap = scratch->allocate<u32>(num_vertices);
    usize num_unique_vertices = meshopt_optimizeVertexFetchRemap(
        remap, indices.data(), indices.size(), num_vertices);
    mesh_remap_vertex_streams(scratch,
                              {
                                  .num_vertices = num_vertices,
                                  .num_unique_vertices = num_unique_vertices,
                                  .positions = &positions,
                                  .normals = &normals,
                                  .tangents = &tangents,
                                  .uvs = &uvs,
                                  .colors = &colors,
                                  .remap = remap,
                              });
    num_vertices = num_unique_vertices;
    Span remapped_indices = Span<u32>::allocate(scratch, indices.size());
    meshopt_remapIndexBuffer(remapped_indices.data(), indices.data(),
                             indices.size(), remap);
    indices = remapped_indices;
  }
#endif

  // Compute bounds.

  mesh_compute_bounds({positions, num_vertices}, &mesh.header.bb,
                      &mesh.header.scale);

  // Generate meshlets

  mesh_generate_meshlets(arena, {
                                    .positions = {positions, num_vertices},
                                    .indices = indices,
                                    .lods = {lods, num_lods},
                                    .meshlets = &mesh.meshlets,
                                    .meshlet_indices = &mesh.indices,
                                    .meshlet_triangles = &mesh.triangles,
                                    .header = &mesh.header,
                                    .cone_weight = 1.0f,
                                });

  // Encode vertex attributes

  mesh.positions = mesh_encode_positions(arena, {positions, num_vertices},
                                         mesh.header.scale);

  mesh.normals =
      mesh_encode_normals(arena, {normals, num_vertices}, mesh.header.scale);

  if (tangents) {
    mesh.tangents =
        mesh_encode_tangents(arena, {tangents, num_vertices}, mesh.header.scale,
                             {mesh.normals, num_vertices});
  }

  if (uvs) {
    mesh.uvs = mesh_encode_uvs(arena, {uvs, num_vertices}, &mesh.header.uv_bs);
  }

  if (colors) {
    mesh.colors = mesh_encode_colors(arena, {colors, num_vertices});
  }

  usize align = 8;

  u64 end = pad(sizeof(mesh.header), align);

#define set_offset(data, size)                                                 \
  [&] {                                                                        \
    mesh.header.data##_offset = mesh.data ? end : 0;                           \
    end = pad(                                                                 \
        end + Span(mesh.data, mesh.data ? mesh.header.size : 0).size_bytes(),  \
        align);                                                                \
  }()
  set_offset(positions, num_vertices);
  set_offset(normals, num_vertices);
  set_offset(tangents, num_vertices);
  set_offset(uvs, num_vertices);
  set_offset(colors, num_vertices);
  set_offset(meshlets, num_meshlets);
  set_offset(indices, num_indices);
  set_offset(triangles, num_triangles * 3);
#undef set_offset

  mesh.size = end;

  return mesh;
}

auto bake_mesh_to_file(const MeshInfo &info, FILE *out) -> expected<void> {
  ScratchArena scratch;
  BakedMesh mesh = bake_mesh(scratch, info);

  usize file_start = std::ftell(out);

  bool success = [&]() {
    if (std::fwrite(&mesh.header, sizeof(mesh.header), 1, out) != 1) {
      return false;
    }

#define write_array(arr, size)                                                 \
  do {                                                                         \
    if (!std::fseek(out, file_start + mesh.header.arr##_offset, SEEK_SET)) {   \
      return false;                                                            \
    }                                                                          \
    usize sz = Span(mesh.arr, mesh.arr ? mesh.header.size : 0).size_bytes();   \
    if (std::fwrite(mesh.arr, 1, sz, out) != sz) {                             \
      return false;                                                            \
    }                                                                          \
  } while (0)
    write_array(positions, num_vertices);
    write_array(normals, num_vertices);
    write_array(tangents, num_vertices);
    write_array(uvs, num_vertices);
    write_array(colors, num_vertices);
    write_array(meshlets, num_meshlets);
    write_array(indices, num_indices);
    write_array(triangles, num_triangles * 3);
#undef write_array

    return true;
  }();

  if (!success) {
    std::fseek(out, file_start, SEEK_SET);
    return std::unexpected(Error::IO);
  }

  return {};
}

Blob bake_mesh_to_memory(NotNull<Arena *> arena, const MeshInfo &info) {
  ScratchArena scratch(arena);
  BakedMesh mesh = bake_mesh(scratch, info);
  u8 *buffer = (u8 *)arena->allocate(mesh.size, 8);
  std::memcpy(buffer, &mesh.header, sizeof(mesh.header));
#define write_array(arr, size)                                                 \
  std::memcpy(&buffer[mesh.header.arr##_offset], mesh.arr,                     \
              Span(mesh.arr, mesh.arr ? mesh.header.size : 0).size_bytes())
  write_array(positions, num_vertices);
  write_array(normals, num_vertices);
  write_array(tangents, num_vertices);
  write_array(uvs, num_vertices);
  write_array(colors, num_vertices);
  write_array(meshlets, num_meshlets);
  write_array(indices, num_indices);
  write_array(triangles, num_triangles * 3);
#undef write_array
  return {buffer, mesh.size};
}

} // namespace ren
