#include "GpuScene.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"
#include "glsl/Mesh.h"
#include "ren/ren.hpp"

namespace ren {

auto init_gpu_scene(ResourceArena &arena) -> GpuScene {
  constexpr BufferHeap HEAP = BufferHeap::Static;
  constexpr VkBufferUsageFlags USAGE =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

#define create_buffer(T, n, c)                                                 \
  StatefulBufferSlice<T> {                                                     \
    arena.create_buffer<T>({                                                   \
        .name = n,                                                             \
        .heap = HEAP,                                                          \
        .usage = USAGE,                                                        \
        .count = c,                                                            \
    })                                                                         \
  }

  constexpr usize NUM_MESH_INSTANCE_VISIBILITY_MASKS = ceil_div(
      MAX_NUM_MESH_INSTANCES, glsl::MESH_INSTANCE_VISIBILITY_MASK_BIT_SIZE);

  return GpuScene{
      .meshes = create_buffer(glsl::Mesh, "Scene meshes", MAX_NUM_MESHES),
      .mesh_instances = create_buffer(
          glsl::MeshInstance, "Scene mesh instances", MAX_NUM_MESH_INSTANCES),
      .mesh_instance_visibility = create_buffer(
          MeshInstanceVisibilityMask, "Scene mesh instance visibility",
          NUM_MESH_INSTANCE_VISIBILITY_MASKS),
      .materials =
          create_buffer(glsl::Material, "Scene materials", MAX_NUM_MATERIALS),
      .directional_lights =
          create_buffer(glsl::DirectionalLight, "Scene directional lights",
                        MAX_NUM_DIRECTIONAL_LIGHTS),
  };

#undef create_buffer
}

} // namespace ren
