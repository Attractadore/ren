#pragma once
#include "Mesh.hpp"
#include "Support/NotNull.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "glsl/Vertex.h"

#include <glm/glm.hpp>

namespace ren {

struct MeshProcessingOptions {
  Span<const glm::vec3> positions;
  Span<const glm::vec3> normals;
  Span<const glm::vec4> tangents;
  Span<const glm::vec2> uvs;
  Span<const glm::vec4> colors;
  NotNull<Vector<glsl::Position> *> enc_positions;
  NotNull<Vector<glsl::Normal> *> enc_normals;
  NotNull<Vector<glsl::Tangent> *> enc_tangents;
  NotNull<Vector<glsl::UV> *> enc_uvs;
  NotNull<Vector<glsl::Color> *> enc_colors;
  NotNull<Vector<u32> *> indices;
  NotNull<Vector<glsl::Meshlet> *> meshlets;
  NotNull<Vector<u32> *> meshlet_indices;
  NotNull<Vector<u8> *> meshlet_triangles;
};

[[nodiscard]] auto mesh_process(const MeshProcessingOptions &opts) -> Mesh;

struct MeshGenerateIndicesOptions {
  NotNull<Vector<glm::vec3> *> positions;
  NotNull<Vector<glm::vec3> *> normals;
  Vector<glm::vec4> *tangents = nullptr;
  Vector<glm::vec2> *uvs = nullptr;
  Vector<glm::vec4> *colors = nullptr;
  NotNull<Vector<u32> *> indices;
};

void mesh_generate_indices(const MeshGenerateIndicesOptions &opts);

struct MeshRemapVertexStreamsOptions {
  NotNull<Vector<glm::vec3> *> positions;
  NotNull<Vector<glm::vec3> *> normals;
  Vector<glm::vec4> *tangents = nullptr;
  Vector<glm::vec2> *uvs = nullptr;
  Vector<glm::vec4> *colors = nullptr;
  u32 num_vertices = 0;
  Span<const u32> remap;
};

void mesh_remap_vertex_streams(const MeshRemapVertexStreamsOptions &opts);

struct MeshGenerateTangentsOptions {
  NotNull<Vector<glm::vec3> *> positions;
  NotNull<Vector<glm::vec3> *> normals;
  NotNull<Vector<glm::vec4> *> tangents;
  NotNull<Vector<glm::vec2> *> uvs;
  Vector<glm::vec4> *colors = nullptr;
  NotNull<Vector<u32> *> indices;
};

void mesh_generate_tangents(const MeshGenerateTangentsOptions &opts);

[[nodiscard]] auto
mesh_encode_positions(Span<const glm::vec3> positions,
                      NotNull<glsl::PositionBoundingBox *> bb,
                      NotNull<glm::vec3 *> enc_bb) -> Vector<glsl::Position>;

[[nodiscard]] auto mesh_encode_normals(Span<const glm::vec3> normals,
                                       const glm::vec3 &pos_enc_bb)
    -> Vector<glsl::Normal>;

[[nodiscard]] auto mesh_encode_tangents(Span<const glm::vec4> tangents,
                                        const glm::vec3 &pos_enc_bb,
                                        Span<const glsl::Normal> enc_normals)
    -> Vector<glsl::Tangent>;

[[nodiscard]] auto mesh_encode_uvs(Span<const glm::vec2> uvs,
                                   NotNull<glsl::BoundingSquare *> uv_bs)
    -> Vector<glsl::UV>;

[[nodiscard]] auto mesh_encode_colors(Span<const glm::vec4> colors)
    -> Vector<glsl::Color>;

struct MeshGenerateMeshletsOptions {
  Span<const glm::vec3> positions;
  Span<const u32> indices;
  Span<glsl::MeshLOD> lods;
  NotNull<Vector<glsl::Meshlet> *> meshlets;
  NotNull<Vector<u32> *> meshlet_indices;
  NotNull<Vector<u8> *> meshlet_triangles;
};

void mesh_generate_meshlets(const MeshGenerateMeshletsOptions &opts);

} // namespace ren
