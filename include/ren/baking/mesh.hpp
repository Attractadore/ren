#pragma once
#include "../core/FileSystem.hpp"
#include "../core/Span.hpp"
#include "../ren.hpp"

namespace ren {

struct MeshInfo {
  size_t num_vertices = 0;
  NotNull<const glm::vec3 *> positions;
  NotNull<const glm::vec3 *> normals;
  const glm::vec4 *tangents = nullptr;
  const glm::vec2 *uvs = nullptr;
  const glm::vec4 *colors = nullptr;
  Span<const u32> indices;
};

[[nodiscard]] IoResult<void> bake_mesh_to_file(const MeshInfo &info, File file);

[[nodiscard]] Blob bake_mesh_to_memory(NotNull<Arena *> arena,
                                       const MeshInfo &info);

} // namespace ren
