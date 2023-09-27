#include "Passes/Upload.hpp"
#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/Lighting.hpp"
#include "glsl/Material.hpp"
#include "glsl/Mesh.hpp"

namespace ren {

namespace {

struct UploadPassResources {
  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
};

void run_upload_pass(const RgRuntime &rg, const UploadPassResources &rcs,
                     const UploadPassData &data) {
  assert(rcs.meshes);
  assert(rcs.materials);
  assert(rcs.mesh_instances);
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);

  auto *meshes = rg.map_buffer<glsl::Mesh>(rcs.meshes);
  ranges::transform(data.meshes, meshes, [](const Mesh &mesh) -> glsl::Mesh {
    return {
        .base_tangent_vertex = mesh.base_tangent_vertex,
        .base_color_vertex = mesh.base_color_vertex,
        .base_uv_vertex = mesh.base_uv_vertex,
    };
  });

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(data.materials, materials);

  auto *mesh_instances = rg.map_buffer<glsl::MeshInstance>(rcs.mesh_instances);
  auto *transform_matrices = rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  auto *normal_matrices = rg.map_buffer<glm::mat3>(rcs.normal_matrices);
  for (const auto &[i, mesh_instance] : data.mesh_instances | enumerate) {
    mesh_instances[i] = {
        .mesh = mesh_instance.mesh,
        .material = mesh_instance.material,
    };
    transform_matrices[i] = mesh_instance.matrix;
    normal_matrices[i] =
        glm::transpose(glm::inverse(glm::mat3(mesh_instance.matrix)));
  }

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(data.directional_lights, directional_lights);
}

} // namespace

void setup_upload_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass("upload");

  UploadPassResources rcs;

  rcs.meshes = pass.create_buffer({.name = "meshes"}, RG_HOST_WRITE_BUFFER);

  rcs.materials =
      pass.create_buffer({.name = "materials"}, RG_HOST_WRITE_BUFFER);

  rcs.mesh_instances =
      pass.create_buffer({.name = "mesh-instances"}, RG_HOST_WRITE_BUFFER);

  rcs.transform_matrices =
      pass.create_buffer({.name = "transform-matrices"}, RG_HOST_WRITE_BUFFER);

  rcs.normal_matrices =
      pass.create_buffer({.name = "normal-matrices"}, RG_HOST_WRITE_BUFFER);

  rcs.directional_lights =
      pass.create_buffer({.name = "directional-lights"}, RG_HOST_WRITE_BUFFER);

  pass.set_update_callback(ren_rg_update_callback(UploadPassData) {
    rg.resize_buffer(rcs.meshes, sizeof(glsl::Mesh) * data.meshes.size());
    rg.resize_buffer(rcs.materials,
                     sizeof(glsl::Material) * data.materials.size());
    rg.resize_buffer(rcs.mesh_instances,
                     sizeof(glsl::MeshInstance) * data.mesh_instances.size());
    rg.resize_buffer(rcs.transform_matrices,
                     sizeof(glm::mat4x3) * data.mesh_instances.size());
    rg.resize_buffer(rcs.normal_matrices,
                     sizeof(glm::mat3) * data.mesh_instances.size());
    rg.resize_buffer(rcs.directional_lights,
                     data.directional_lights.size_bytes());
    return true;
  });

  pass.set_host_callback(
      ren_rg_host_callback(UploadPassData) { run_upload_pass(rg, rcs, data); });
}

} // namespace ren
