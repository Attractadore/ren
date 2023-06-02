#include "Passes/Upload.hpp"
#include "Device.hpp"

namespace ren {
namespace {

struct UploadPassResources {
  std::span<const MeshInst> mesh_insts;
  std::span<const glsl::DirLight> directional_lights;
  std::span<const glsl::Material> materials;
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID directional_lights_buffer;
  RGBufferID materials_buffer;
};

void run_upload_pass(Device &device, RenderGraph &rg, CommandBuffer &cmd,
                     const UploadPassResources &rcs) {
  if (rcs.transform_matrix_buffer) {
    assert(not rcs.mesh_insts.empty());
    assert(rcs.normal_matrix_buffer);

    auto *transform_matrices = device.map_buffer<glm::mat4x3>(
        rg.get_buffer(rcs.transform_matrix_buffer));
    ranges::transform(
        rcs.mesh_insts, transform_matrices,
        [](const MeshInst &mesh_inst) { return mesh_inst.matrix; });

    auto *normal_matrices =
        device.map_buffer<glm::mat3>(rg.get_buffer(rcs.normal_matrix_buffer));
    ranges::transform(
        rcs.mesh_insts, normal_matrices, [](const MeshInst &mesh_inst) {
          return glm::transpose(glm::inverse(glm::mat3(mesh_inst.matrix)));
        });
  };

  if (rcs.directional_lights_buffer) {
    assert(not rcs.directional_lights.empty());
    auto *directional_lights = device.map_buffer<glsl::DirLight>(
        rg.get_buffer(rcs.directional_lights_buffer));
    ranges::copy(rcs.directional_lights, directional_lights);
  };

  if (rcs.materials_buffer) {
    assert(not rcs.materials.empty());
    auto *materials =
        device.map_buffer<glsl::Material>(rg.get_buffer(rcs.materials_buffer));
    ranges::copy(rcs.materials, materials);
  };
}

} // namespace

auto setup_upload_pass(Device &device, RenderGraph::Builder &rgb,
                       const UploadPassConfig &cfg) -> UploadPassOutput {
  auto pass = rgb.create_pass({
      .name = "Upload",
  });

  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;

  auto num_mesh_insts = cfg.mesh_insts.size();
  if (num_mesh_insts > 0) {
    transform_matrix_buffer = pass.create_buffer({
        .name = "Transform matrices",
        .heap = BufferHeap::Upload,
        .size = sizeof(glm::mat4x3) * num_mesh_insts,
    });
    normal_matrix_buffer = pass.create_buffer({
        .name = "Normal matrices",
        .heap = BufferHeap::Upload,
        .size = sizeof(glm::mat3) * num_mesh_insts,
    });
  }

  RGBufferID directional_lights_buffer;
  if (not cfg.directional_lights.empty()) {
    directional_lights_buffer = pass.create_buffer({
        .name = "Directional lights",
        .heap = BufferHeap::Upload,
        .size = cfg.directional_lights.size_bytes(),
    });
  }

  RGBufferID materials_buffer;
  if (not cfg.materials.empty()) {
    materials_buffer = pass.create_buffer({
        .name = "Materials",
        .heap = BufferHeap::Upload,
        .size = cfg.materials.size_bytes(),
    });
  }

  UploadPassResources rcs = {
      .mesh_insts = cfg.mesh_insts,
      .directional_lights = cfg.directional_lights,
      .materials = cfg.materials,
      .transform_matrix_buffer = transform_matrix_buffer,
      .normal_matrix_buffer = normal_matrix_buffer,
      .directional_lights_buffer = directional_lights_buffer,
      .materials_buffer = materials_buffer,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_upload_pass(device, rg, cmd, rcs);
  });

  return {
      .transform_matrix_buffer = transform_matrix_buffer,
      .normal_matrix_buffer = normal_matrix_buffer,
      .dir_lights_buffer = directional_lights_buffer,
      .materials_buffer = materials_buffer,
  };
}

} // namespace ren
