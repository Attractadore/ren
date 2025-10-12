#include "MeshSimplification.hpp"

#include <meshoptimizer.h>

namespace ren {

void mesh_simplify(const MeshSimplificationOptions &opts) {
  usize max_num_indices =
      opts.indices->size() * 1.0f / (1.0f - opts.threshold) + 1;
  opts.indices->reserve(max_num_indices);

  u32 num_lods = 1;
  opts.lods[0] = {.num_indices = u32(opts.indices->size())};

  Vector<u32> lod_indices;
  for (; num_lods < *opts.num_lods; ++num_lods) {
    u32 num_prev_lod_indices = opts.lods[num_lods - 1].num_indices;

    u32 num_lod_target_indices = num_prev_lod_indices * opts.threshold;
    num_lod_target_indices -= num_lod_target_indices % 3;
    num_lod_target_indices =
        std::max(num_lod_target_indices, opts.min_num_triangles * 3);
    if (num_lod_target_indices == num_prev_lod_indices) {
      break;
    }

    constexpr float LOD_ERROR = 0.001f;

    lod_indices.resize(num_prev_lod_indices);
    u32 num_lod_indices = meshopt_simplify(
        lod_indices.data(), opts.indices->data(), num_prev_lod_indices,
        (const float *)opts.positions->data(), opts.positions->size(),
        sizeof(glm::vec3), num_lod_target_indices, LOD_ERROR, 0, nullptr);
    if (num_lod_indices > num_lod_target_indices) {
      break;
    }
    lod_indices.resize(num_lod_indices);

    // Insert coarser LODs in front for vertex fetch optimization
    opts.indices->insert(opts.indices->begin(), lod_indices.begin(),
                         lod_indices.end());

    opts.lods[num_lods] = {.num_indices = num_lod_indices};
  }
  *opts.num_lods = num_lods;

  for (u32 lod = num_lods - 1; lod > 0; --lod) {
    opts.lods[lod - 1].base_index =
        opts.lods[lod].base_index + opts.lods[lod].num_indices;
  }
}

} // namespace ren
