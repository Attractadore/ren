#include "Passes/Upload.hpp"
#include "Model.hpp"
#include "RenderGraph.hpp"
#include "glsl/Lighting.hpp"
#include "glsl/Material.hpp"

namespace ren {

namespace {

struct UploadPassResources {
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgBufferId materials;
};

void run_upload_pass(Device &device, const RgRuntime &rg,
                     const UploadPassResources &rcs,
                     const UploadPassData &data) {
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);
  assert(rcs.materials);

  auto *transform_matrices = rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  ranges::transform(data.mesh_insts, transform_matrices,
                    [](const MeshInst &mesh_inst) { return mesh_inst.matrix; });

  auto *normal_matrices = rg.map_buffer<glm::mat3>(rcs.normal_matrices);
  ranges::transform(
      data.mesh_insts, normal_matrices, [](const MeshInst &mesh_inst) {
        return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
      });

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(data.directional_lights, directional_lights);

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(data.materials, materials);
}

} // namespace

void setup_upload_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass("upload");

  UploadPassResources rcs;

  rcs.transform_matrices = pass.create_buffer(
      {
          .name = "transform-matrices",
          .heap = BufferHeap::Upload,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.normal_matrices = pass.create_buffer(
      {
          .name = "normal-matrices",
          .heap = BufferHeap::Upload,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.directional_lights = pass.create_buffer(
      {
          .name = "directional-lights",
          .heap = BufferHeap::Upload,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.materials = pass.create_buffer(
      {
          .name = "materials",
          .heap = BufferHeap::Upload,
      },
      RG_HOST_WRITE_BUFFER);

  pass.set_update_callback(ren_rg_update_callback(UploadPassData) {
    rg.resize_buffer(rcs.transform_matrices,
                     sizeof(glm::mat4x3) * data.mesh_insts.size());
    rg.resize_buffer(rcs.normal_matrices,
                     sizeof(glm::mat3) * data.mesh_insts.size());
    rg.resize_buffer(rcs.directional_lights,
                     data.directional_lights.size_bytes());
    rg.resize_buffer(rcs.materials, data.materials.size_bytes());
  });

  pass.set_host_callback(ren_rg_host_callback(UploadPassData) {
    run_upload_pass(device, rg, rcs, data);
  });
}

} // namespace ren
