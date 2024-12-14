#include "MeshProcessing.hpp"
#include "MeshSimplification.hpp"
#include "glsl/Transforms.h"

#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>
#include <mikktspace.h>

namespace ren {

auto mesh_process(const MeshProcessingOptions &opts) -> Mesh {
  Vector<glm::vec3> positions = opts.positions;
  Vector<glm::vec3> normals = opts.normals;
  Vector<glm::vec4> tangents = opts.tangents;
  Vector<glm::vec2> uvs = opts.uvs;
  Vector<glm::vec4> colors = opts.colors;
  Vector<u32> indices = opts.indices;

  ren_assert(positions.size() > 0);
  ren_assert(normals.size() == positions.size());
  if (not tangents.empty()) {
    ren_assert(tangents.size() == positions.size());
  }
  if (not uvs.empty()) {
    ren_assert(uvs.size() == positions.size());
  }
  if (not colors.empty()) {
    ren_assert(colors.size() == positions.size());
  }
  if (not indices.empty()) {
    ren_assert(indices.size() % 3 == 0);
  } else {
    ren_assert(positions.size() % 3 == 0);
  }

  Mesh mesh;

  // (Re)generate index buffer to remove duplicate vertices for LOD generation
  // to work correctly

  mesh_generate_indices({
      .positions = &positions,
      .normals = &normals,
      .tangents = tangents.size() ? &tangents : nullptr,
      .uvs = uvs.size() ? &uvs : nullptr,
      .colors = colors.size() ? &colors : nullptr,
      .indices = &indices,
  });

  // Generate tangents

  if (not uvs.empty() and tangents.empty()) {
    mesh_generate_tangents({
        .positions = &positions,
        .normals = &normals,
        .tangents = &tangents,
        .uvs = &uvs,
        .colors = colors.size() ? &colors : nullptr,
        .indices = &indices,
    });
  }

  // Generate LODs

  StaticVector<LOD, glsl::MAX_NUM_LODS> lods;
  mesh_simplify({
      .positions = &positions,
      .normals = &normals,
      .tangents = tangents.size() ? &tangents : nullptr,
      .uvs = uvs.size() ? &uvs : nullptr,
      .colors = colors.size() ? &colors : nullptr,
      .indices = &indices,
      .lods = &lods,
  });

  u32 num_vertices = positions.size();
  u32 num_indices = indices.size();

  // Optimize each LOD separately

  for (const LOD &lod : lods) {
    meshopt_optimizeVertexCache(&indices[lod.base_index],
                                &indices[lod.base_index], lod.num_indices,
                                num_vertices);
  }

#if 0
  // Optimize all LODs together.

  {
    Vector<u32> remap(num_vertices);
    num_vertices = meshopt_optimizeVertexFetchRemap(
        remap.data(), indices.data(), num_indices, num_vertices);
    meshopt_remapIndexBuffer(indices.data(), indices.data(), num_indices,
                             remap.data());
    mesh_remap_vertex_streams({
        .positions = &positions,
        .normals = &normals,
        .tangents = tangents.size() ? &tangents : nullptr,
        .uvs = uvs.size() ? &uvs : nullptr,
        .colors = colors.size() ? &colors : nullptr,
        .num_vertices = num_vertices,
        .remap = remap,
    });
  }
#endif

  // Compute bounds.

  mesh_compute_bounds(positions, &mesh.bb, &mesh.pos_enc_bb);

  // Generate meshlets

  mesh_generate_meshlets({
      .positions = positions,
      .indices = indices,
      .lods = lods,
      .meshlets = opts.meshlets,
      .meshlet_indices = opts.meshlet_indices,
      .meshlet_triangles = opts.meshlet_triangles,
      .mesh = &mesh,
      .cone_weight = 1.0f,
  });

  // Encode vertex attributes

  *opts.enc_positions = mesh_encode_positions(positions, mesh.pos_enc_bb);

  *opts.enc_normals = mesh_encode_normals(normals, mesh.pos_enc_bb);

  if (not tangents.empty()) {
    *opts.enc_tangents =
        mesh_encode_tangents(tangents, mesh.pos_enc_bb, *opts.enc_normals);
  }

  if (not uvs.empty()) {
    *opts.enc_uvs = mesh_encode_uvs(uvs, &mesh.uv_bs);
  }

  if (not colors.empty()) {
    *opts.enc_colors = mesh_encode_colors(colors);
  }

  return mesh;
}

void mesh_remap_vertex_streams(const MeshRemapVertexStreamsOptions &opts) {
  auto remap_stream = [&]<typename T>(Vector<T> *stream) {
    if (stream) {
      meshopt_remapVertexBuffer(stream->data(), stream->data(), stream->size(),
                                sizeof(T), opts.remap.data());
      stream->resize(opts.num_vertices);
    }
  };
  remap_stream(opts.positions.get());
  remap_stream(opts.normals.get());
  remap_stream(opts.tangents);
  remap_stream(opts.uvs);
  remap_stream(opts.colors);
}

void mesh_generate_indices(const MeshGenerateIndicesOptions &opts) {
  StaticVector<meshopt_Stream, 5> streams;
  auto add_stream = [&]<typename T>(Vector<T> *stream) {
    if (stream) {
      streams.push_back({
          .data = stream->data(),
          .size = sizeof(T),
          .stride = sizeof(T),
      });
    }
  };
  add_stream(opts.positions.get());
  add_stream(opts.normals.get());
  add_stream(opts.tangents);
  add_stream(opts.uvs);
  add_stream(opts.colors);
  const u32 *indices = nullptr;
  u32 num_vertices = opts.positions->size();
  u32 num_indices = num_vertices;
  if (not opts.indices->empty()) {
    indices = opts.indices->data();
    num_indices = opts.indices->size();
  };
  Vector<u32> remap(num_vertices);
  num_vertices = meshopt_generateVertexRemapMulti(
      remap.data(), indices, num_indices, num_vertices, streams.data(),
      streams.size());
  opts.indices->resize(num_indices);
  meshopt_remapIndexBuffer(opts.indices->data(), indices, num_indices,
                           remap.data());
  mesh_remap_vertex_streams({
      .positions = opts.positions,
      .normals = opts.normals,
      .tangents = opts.tangents,
      .uvs = opts.uvs,
      .colors = opts.colors,
      .num_vertices = num_vertices,
      .remap = remap,
  });
}

void mesh_generate_tangents(const MeshGenerateTangentsOptions &opts) {
  u32 num_vertices = opts.indices->size();

  auto unindex_stream = [&]<typename T>(Vector<T> &stream) {
    Vector<T> unindexed_stream(num_vertices);
    for (usize i = 0; i < num_vertices; ++i) {
      u32 index = (*opts.indices)[i];
      unindexed_stream[i] = stream[index];
    }
    std::swap(stream, unindexed_stream);
  };

  unindex_stream(*opts.positions);
  unindex_stream(*opts.normals);
  opts.tangents->resize(num_vertices);
  unindex_stream(*opts.uvs);
  if (opts.colors) {
    unindex_stream(*opts.colors);
  }
  opts.indices->clear();

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
      .num_faces = num_vertices / 3,
      .positions = opts.positions->data(),
      .normals = opts.normals->data(),
      .tangents = opts.tangents->data(),
      .uvs = opts.uvs->data(),
  };

  SMikkTSpaceContext ctx = {
      .m_pInterface = &iface,
      .m_pUserData = &user_data,
  };

  genTangSpaceDefault(&ctx);

  mesh_generate_indices({
      .positions = opts.positions,
      .normals = opts.normals,
      .tangents = opts.tangents,
      .uvs = opts.uvs,
      .colors = opts.colors,
      .indices = opts.indices,
  });
}

void mesh_compute_bounds(Span<const glm::vec3> positions,
                         NotNull<glsl::PositionBoundingBox *> pbb,
                         NotNull<glm::vec3 *> enc_bb) {
  glsl::BoundingBox bb = {
      .min = glm::vec3(std::numeric_limits<float>::infinity()),
      .max = -glm::vec3(std::numeric_limits<float>::infinity()),
  };

  // Select relatively big default bounding box size to avoid log2 NaN.
  *enc_bb = glm::vec3(1.0f);

  for (const glm::vec3 &position : positions) {
    *enc_bb = glm::max(*enc_bb, glm::abs(position));
    bb.min = glm::min(bb.min, position);
    bb.max = glm::max(bb.max, position);
  }

  *enc_bb = glm::exp2(glm::ceil(glm::log2(*enc_bb)));

  *pbb = glsl::encode_bounding_box(bb, *enc_bb);
}

auto mesh_encode_positions(Span<const glm::vec3> positions,
                           const glm::vec3 &enc_bb) -> Vector<glsl::Position> {
  Vector<glsl::Position> enc_positions(positions.size());
  for (usize i = 0; i < positions.size(); ++i) {
    enc_positions[i] = glsl::encode_position(positions[i], enc_bb);
  }
  return enc_positions;
}

auto mesh_encode_normals(Span<const glm::vec3> normals,
                         const glm::vec3 &pos_enc_bb) -> Vector<glsl::Normal> {
  glm::mat3 encode_transform_matrix =
      glsl::make_encode_position_matrix(pos_enc_bb);
  glm::mat3 encode_normal_matrix = glsl::normal(encode_transform_matrix);

  Vector<glsl::Normal> enc_normals(normals.size());
  for (usize i = 0; i < normals.size(); ++i) {
    enc_normals[i] =
        glsl::encode_normal(glm::normalize(encode_normal_matrix * normals[i]));
  }

  return enc_normals;
}

auto mesh_encode_tangents(Span<const glm::vec4> tangents,
                          const glm::vec3 &pos_enc_bb,
                          Span<const glsl::Normal> enc_normals)
    -> Vector<glsl::Tangent> {
  glm::mat3 encode_transform_matrix =
      glsl::make_encode_position_matrix(pos_enc_bb);

  Vector<glsl::Tangent> enc_tangents(tangents.size());
  for (usize i = 0; i < tangents.size(); ++i) {
    // Encoding and then decoding the normal can change how the
    // tangent basis is selected due to rounding errors. Since
    // shaders use the decoded normal to decode the tangent, use
    // it for encoding as well.
    glm::vec3 normal = glsl::decode_normal(enc_normals[i]);

    // Orthonormalize tangent space.
    glm::vec4 tangent = tangents[i];
    glm::vec3 tangent3d(tangent);
    float sign = tangent.w;
    float proj = glm::dot(normal, tangent3d);
    tangent3d = tangent3d - proj * normal;

    tangent =
        glm::vec4(glm::normalize(encode_transform_matrix * tangent3d), sign);
    enc_tangents[i] = glsl::encode_tangent(tangent, normal);
  };

  return enc_tangents;
}

auto mesh_encode_uvs(Span<const glm::vec2> uvs,
                     NotNull<glsl::BoundingSquare *> uv_bs)
    -> Vector<glsl::UV> {
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

  Vector<glsl::UV> enc_uvs(uvs.size());
  for (usize i = 0; i < uvs.size(); ++i) {
    enc_uvs[i] = glsl::encode_uv(uvs[i], *uv_bs);
  }

  return enc_uvs;
}

[[nodiscard]] auto mesh_encode_colors(Span<const glm::vec4> colors)
    -> Vector<glsl::Color> {
  Vector<glsl::Color> enc_colors(colors.size());
  for (usize i = 0; i < colors.size(); ++i) {
    enc_colors[i] = glsl::encode_color(colors[i]);
  }
  return enc_colors;
}

void mesh_generate_meshlets(const MeshGenerateMeshletsOptions &opts) {
  ren_assert(opts.mesh->pos_enc_bb != glm::vec3(0.0f));

  SmallVector<u32, glsl::NUM_MESHLET_TRIANGLES * 3> opt_triangles;

  opts.mesh->lods.resize(opts.lods.size());
  Vector<meshopt_Meshlet> lod_meshlets;
  for (isize l = opts.lods.size() - 1; l >= 0; --l) {
    const LOD &lod = opts.lods[l];

    u32 num_lod_meshlets =
        meshopt_buildMeshletsBound(lod.num_indices, glsl::NUM_MESHLET_VERTICES,
                                   glsl::NUM_MESHLET_TRIANGLES);
    lod_meshlets.resize(num_lod_meshlets);

    u32 base_meshlet = opts.meshlets->size();
    u32 base_index = opts.meshlet_indices->size();
    u32 base_triangle = opts.meshlet_triangles->size();
    ren_assert(base_triangle == lod.base_index);
    opts.meshlet_indices->resize(base_index +
                                 num_lod_meshlets * glsl::NUM_MESHLET_VERTICES);
    opts.meshlet_triangles->resize(
        base_triangle + num_lod_meshlets * glsl::NUM_MESHLET_TRIANGLES * 3);

    num_lod_meshlets = meshopt_buildMeshlets(
        lod_meshlets.data(), &(*opts.meshlet_indices)[base_index],
        &(*opts.meshlet_triangles)[base_triangle],
        &opts.indices[lod.base_index], lod.num_indices,
        (const float *)opts.positions.data(), opts.positions.size(),
        sizeof(glm::vec3), glsl::NUM_MESHLET_VERTICES,
        glsl::NUM_MESHLET_TRIANGLES, opts.cone_weight);

    opts.meshlets->resize(base_meshlet + num_lod_meshlets);

    opts.mesh->lods[l] = {
        .base_meshlet = base_meshlet,
        .num_meshlets = num_lod_meshlets,
        .num_triangles = lod.num_indices / 3,
    };

    u32 num_lod_triangles = 0;
    for (usize m = 0; m < num_lod_meshlets; ++m) {
      const meshopt_Meshlet lod_meshlet = lod_meshlets[m];

      glsl::Meshlet meshlet = {
          .base_index = base_index,
          .base_triangle = base_triangle + num_lod_triangles * 3,
          .num_triangles = lod_meshlet.triangle_count,
      };

      auto indices = Span(*opts.meshlet_indices)
                         .subspan(base_index, lod_meshlet.vertex_count);

      auto triangles = Span(*opts.meshlet_triangles)
                           .subspan(base_triangle + lod_meshlet.triangle_offset,
                                    lod_meshlet.triangle_count * 3);

      opt_triangles = triangles;

      // Optimize meshlet.
      // TODO: replace with meshopt_optimizeMeshlet

      meshopt_optimizeVertexCache(opt_triangles.data(), opt_triangles.data(),
                                  opt_triangles.size(),
                                  lod_meshlet.vertex_count);

      meshopt_optimizeVertexFetch(indices.data(), opt_triangles.data(),
                                  opt_triangles.size(), indices.data(),
                                  indices.size(), sizeof(u32));

      // Compact triangle buffer.
      triangles =
          Span(*opts.meshlet_triangles)
              .subspan(meshlet.base_triangle, meshlet.num_triangles * 3);
      std::ranges::copy(opt_triangles, triangles.data());

      meshopt_Bounds bounds = meshopt_computeMeshletBounds(
          indices.data(), triangles.data(), lod_meshlet.triangle_count,
          (const float *)opts.positions.data(), opts.positions.size(),
          sizeof(glm::vec3));
      glm::vec3 cone_apex = glm::make_vec3(bounds.cone_apex);
      glm::vec3 cone_axis = glm::make_vec3(bounds.cone_axis);

      meshlet.cone_apex =
          glsl::encode_position(cone_apex, opts.mesh->pos_enc_bb),
      meshlet.cone_axis =
          glsl::encode_position(cone_axis, opts.mesh->pos_enc_bb),
      meshlet.cone_cutoff = bounds.cone_cutoff;

      glsl::BoundingBox bb = {
          .min = glm::vec3(std::numeric_limits<float>::infinity()),
          .max = -glm::vec3(std::numeric_limits<float>::infinity()),
      };

      for (u8 t : triangles) {
        u32 index = indices[t];
        const glm::vec3 &position = opts.positions[index];
        bb.min = glm::min(bb.min, position);
        bb.max = glm::max(bb.max, position);
      }

      meshlet.bb = glsl::encode_bounding_box(bb, opts.mesh->pos_enc_bb);

      (*opts.meshlets)[base_meshlet + m] = meshlet;

      base_index += lod_meshlet.vertex_count;
      num_lod_triangles += lod_meshlet.triangle_count;
    }

    ren_assert(num_lod_triangles * 3 == lod.num_indices);

    opts.meshlet_indices->resize(base_index);
    opts.meshlet_triangles->resize(base_triangle + num_lod_triangles * 3);
  }

  ren_assert(opts.meshlet_triangles->size() == opts.indices.size());
}

} // namespace ren
