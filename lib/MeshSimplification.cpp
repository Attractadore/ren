#include "MeshSimplification.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Array.hpp"

#include <meshoptimizer.h>

namespace ren {

void mesh_simplify(NotNull<Arena *> arena,
                   const MeshSimplificationInput &input) {
  ScratchArena scratch(arena);

  DynamicArray<Span<const u32>> lods;
  lods.push(scratch, *input.indices);
  for (u32 lod = 1; lod < *input.num_lods; ++lod) {
    Span<const u32> prev_lod = lods.back();

    u32 num_lod_target_indices = prev_lod.m_size * input.threshold;
    num_lod_target_indices -= num_lod_target_indices % 3;
    num_lod_target_indices =
        max(num_lod_target_indices, input.min_num_triangles * 3);
    if (num_lod_target_indices == prev_lod.m_size) {
      break;
    }

    constexpr float LOD_ERROR = 0.001f;

    u32 *indices = scratch->allocate<u32>(prev_lod.m_size);
    u32 num_lod_indices = meshopt_simplify(
        indices, prev_lod.m_data, prev_lod.m_size,
        (const float *)input.positions.get(), input.num_vertices,
        sizeof(glm::vec3), num_lod_target_indices, LOD_ERROR, 0, nullptr);
    lods.push(scratch, {indices, num_lod_indices});
    if (num_lod_indices > num_lod_target_indices) {
      break;
    }
  }
  *input.num_lods = lods.m_size;

  usize num_indices = 0;
  for (Span<const u32> lod : lods) {
    num_indices += lod.m_size;
  }
  *input.indices = Span<u32>::allocate(arena, num_indices);

  // Insert coarser LODs in front for vertex fetch optimization
  u32 base_index = 0;
  for (isize lod = isize(lods.m_size) - 1; lod >= 0; --lod) {
    u32 lod_size = lods[lod].m_size;
    copy(lods[lod], &(*input.indices)[base_index]);
    input.lods[lod] = {
        .base_index = base_index,
        .num_indices = lod_size,
    };
    base_index += lod_size;
  }
}

} // namespace ren
