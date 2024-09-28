#include "GpuScene.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"
#include "glsl/Mesh.h"
#include "ren/ren.hpp"

namespace ren {

auto init_gpu_scene(ResourceArena &arena) -> GpuScene {
  GpuScene gpu_scene;
  gpu_scene.meshes = {arena.create_buffer<glsl::Mesh>({
      .name = "Scene meshes",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .count = MAX_NUM_MESHES,
  })};
  gpu_scene.mesh_instances = {arena.create_buffer<glsl::MeshInstance>({
      .name = "Scene mesh instances",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .count = MAX_NUM_MESH_INSTANCES,
  })};
  gpu_scene.materials = {arena.create_buffer<glsl::Material>({
      .name = "Scene materials",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .count = MAX_NUM_MATERIALS,
  })};
  gpu_scene.directional_lights = {arena.create_buffer<glsl::DirectionalLight>({
      .name = "Scene directional lights",
      .heap = BufferHeap::Static,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      .count = MAX_NUM_DIRECTIONAL_LIGHTS,
  })};
  return gpu_scene;
}
} // namespace ren
