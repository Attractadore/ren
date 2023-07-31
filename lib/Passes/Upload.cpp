#include "Passes/Upload.hpp"
#include "Device.hpp"

namespace ren {
namespace {

struct UploadPassResources {
  RgRtBuffer transform_matrices;
  RgRtBuffer normal_matrices;
  RgRtBuffer directional_lights;
  RgRtBuffer materials;
};

void run_upload_pass(Device &device, const RgRuntime &rg,
                     const UploadPassResources &rcs,
                     const UploadPassData &data) {
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);
  assert(rcs.materials);

  auto *transform_matrices =
      device.map_buffer<glm::mat4x3>(rg.get_buffer(rcs.transform_matrices));
  ranges::transform(data.mesh_insts, transform_matrices,
                    [](const MeshInst &mesh_inst) { return mesh_inst.matrix; });

  auto *normal_matrices =
      device.map_buffer<glm::mat3>(rg.get_buffer(rcs.normal_matrices));
  ranges::transform(
      data.mesh_insts, normal_matrices, [](const MeshInst &mesh_inst) {
        return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
      });

  auto *directional_lights =
      device.map_buffer<glsl::DirLight>(rg.get_buffer(rcs.directional_lights));
  ranges::copy(data.directional_lights, directional_lights);

  auto *materials =
      device.map_buffer<glsl::Material>(rg.get_buffer(rcs.materials));
  ranges::copy(data.materials, materials);
}

} // namespace

auto setup_upload_pass(RenderGraph::Builder &rgb) -> UploadPassOutput {
  auto pass = rgb.create_pass({.name = "Upload"});

  auto [transform_matrices, rt_transform_matrices] = pass.create_upload_buffer({
      .name = "Transform matrices",
  });

  auto [normal_matrices, rt_normal_matrices] = pass.create_upload_buffer({
      .name = "Normal matrices",
  });

  auto [directional_lights, rt_directional_lights] = pass.create_upload_buffer({
      .name = "Directional lights",
  });

  auto [materials, rt_materials] = pass.create_upload_buffer({
      .name = "Materials",
  });

  pass.set_size_callback(ren_rg_size_callback(UploadPassData) {
    rg.resize_buffer({
        .buffer = transform_matrices,
        .size = sizeof(glm::mat4x3) * data.mesh_insts.size(),
    });
    rg.resize_buffer({
        .buffer = normal_matrices,
        .size = sizeof(glm::mat3) * data.mesh_insts.size(),
    });
    rg.resize_buffer({
        .buffer = directional_lights,
        .size = data.directional_lights.size_bytes(),
    });
    rg.resize_buffer({
        .buffer = materials,
        .size = data.materials.size_bytes(),
    });
  });

  UploadPassResources rcs = {
      .transform_matrices = rt_transform_matrices,
      .normal_matrices = rt_normal_matrices,
      .directional_lights = rt_directional_lights,
      .materials = rt_materials,
  };

  pass.set_host_callback(ren_rg_host_callback(UploadPassData) {
    run_upload_pass(device, rg, rcs, data);
  });

  return {
      .pass = pass,
      .transform_matrices = transform_matrices,
      .normal_matrices = normal_matrices,
      .directional_lights = directional_lights,
      .materials = materials,
  };
}

} // namespace ren
