#include "GpuSceneUpdate.hpp"
#include "CommandRecorder.hpp"
#include "Profiler.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/CalculateNormalMatricesPass.h"

#include <algorithm>

namespace ren {

auto rg_import_gpu_scene(RgBuilder &rgb, const GpuScene &gpu_scene)
    -> RgGpuScene {
  return {
      .meshes = rgb.create_buffer("meshes", gpu_scene.meshes),
      .mesh_instances =
          rgb.create_buffer("mesh-instances", gpu_scene.mesh_instances),
      .transform_matrices = rgb.create_buffer<glm::mat4x3>({
          .heap = BufferHeap::Static,
          .size = MAX_NUM_MESH_INSTANCES,
      }),
      .normal_matrices = rgb.create_buffer<glm::mat3>({
          .heap = BufferHeap::Static,
          .size = MAX_NUM_MESH_INSTANCES,
      }),
      .mesh_instance_visibility = rgb.create_buffer(
          "mesh-instance-visibility", gpu_scene.mesh_instance_visibility),
      .materials = rgb.create_buffer("materials", gpu_scene.materials),
      .directional_lights =
          rgb.create_buffer("directional-lights", gpu_scene.directional_lights),
  };
}

void rg_export_gpu_scene(const RgBuilder &rgb, const RgGpuScene &rg_gpu_scene,
                         NotNull<GpuScene *> gpu_scene) {
  gpu_scene->meshes.state = rgb.get_final_buffer_state(rg_gpu_scene.meshes);
  gpu_scene->mesh_instances.state =
      rgb.get_final_buffer_state(rg_gpu_scene.mesh_instances);
  gpu_scene->mesh_instance_visibility.state =
      rgb.get_final_buffer_state(rg_gpu_scene.mesh_instance_visibility);
  gpu_scene->materials.state =
      rgb.get_final_buffer_state(rg_gpu_scene.materials);
  gpu_scene->directional_lights.state =
      rgb.get_final_buffer_state(rg_gpu_scene.directional_lights);
}

void setup_calculate_normal_matrices_pass(const PassCommonConfig &ccfg,
                                          NotNull<RgGpuScene *> gpu_scene) {

  RgBuilder &rgb = *ccfg.rgb;
  NotNull<const SceneData *> scene = ccfg.scene;

  auto pass = rgb.create_pass({"gpu-scene-calculate-normal-matrices"});

  struct Resources {
    Handle<ComputePipeline> pipeline;
    u32 num_mesh_instances;
    RgBufferToken<glm::mat4x3> transforms;
    RgBufferToken<glm::mat3> normals;
  } rcs;

  rcs.pipeline = ccfg.pipelines->calculate_normal_matrices;

  rcs.num_mesh_instances = ccfg.scene->mesh_instances.raw_size();

  rcs.transforms =
      pass.read_buffer(gpu_scene->transform_matrices, CS_READ_BUFFER);

  std::tie(gpu_scene->normal_matrices, rcs.normals) = pass.write_buffer(
      "normal-matrices", gpu_scene->normal_matrices, CS_WRITE_BUFFER);

  pass.set_compute_callback(
      [rcs](Renderer &renderer, const RgRuntime &rg, ComputePass &cmd) {
        cmd.bind_compute_pipeline(rcs.pipeline);
        cmd.set_push_constants(glsl::CalculateNormalMatricesPassArgs{
            .transforms = rg.get_buffer_device_ptr(rcs.transforms),
            .normals = rg.get_buffer_device_ptr(rcs.normals),
        });
        cmd.dispatch_threads(rcs.num_mesh_instances,
                             glsl::CALCULATE_NORMAL_MATRICES_THREADS);
      });
}

void setup_gpu_scene_update_pass(const PassCommonConfig &ccfg,
                                 const GpuSceneUpdatePassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;
  NotNull<const SceneData *> scene = ccfg.scene;

  auto pass = rgb.create_pass({"gpu-scene-update"});

  RgBufferToken<glsl::Mesh> meshes;
  if (not scene->update_meshes.empty()) {
    std::tie(cfg.gpu_scene->meshes, meshes) = pass.write_buffer(
        "meshes-updated", cfg.gpu_scene->meshes, TRANSFER_DST_BUFFER);
  }

  RgBufferToken<glsl::MeshInstance> mesh_instances;
  if (not scene->update_mesh_instances.empty()) {
    std::tie(cfg.gpu_scene->mesh_instances, mesh_instances) =
        pass.write_buffer("mesh-instances-updated",
                          cfg.gpu_scene->mesh_instances, TRANSFER_DST_BUFFER);
  }

  RgBufferToken<glm::mat4x3> transform_matrices;
  std::tie(cfg.gpu_scene->transform_matrices, transform_matrices) =
      pass.write_buffer("transform-matrices", cfg.gpu_scene->transform_matrices,
                        TRANSFER_DST_BUFFER);

  RgBufferToken<glsl::Material> materials;
  if (not scene->update_materials.empty()) {
    std::tie(cfg.gpu_scene->materials, materials) = pass.write_buffer(
        "materials-updated", cfg.gpu_scene->materials, TRANSFER_DST_BUFFER);
  }

  RgBufferToken<glsl::DirectionalLight> directional_lights;
  if (not scene->update_directional_lights.empty()) {
    std::tie(cfg.gpu_scene->directional_lights, directional_lights) =
        pass.write_buffer("directional-lights-updated",
                          cfg.gpu_scene->directional_lights,
                          TRANSFER_DST_BUFFER);
  }

  pass.set_callback([=](Renderer &renderer, const RgRuntime &rg,
                        CommandRecorder &cmd) {
    if (meshes) {
      ren_prof_zone("Update meshes");
      BufferSlice<glsl::Mesh> buffer = rg.get_buffer(meshes);
      auto [ptr, _, staging_buffer] =
          rg.allocate<glsl::Mesh>(scene->update_meshes.size());
      std::ranges::copy(scene->mesh_update_data, ptr);
      for (auto i : range(scene->update_meshes.size())) {
        cmd.copy_buffer(staging_buffer.slice(i, 1),
                        buffer.slice(scene->update_meshes[i], 1));
      }
    }

    if (mesh_instances) {
      ren_prof_zone("Update mesh instances");
      BufferSlice<glsl::MeshInstance> buffer = rg.get_buffer(mesh_instances);
      auto [ptr, _, staging_buffer] =
          rg.allocate<glsl::MeshInstance>(scene->update_mesh_instances.size());
      std::ranges::copy(scene->mesh_instance_update_data, ptr);
      for (auto i : range(scene->update_mesh_instances.size())) {
        cmd.copy_buffer(staging_buffer.slice(i, 1),
                        buffer.slice(scene->update_mesh_instances[i], 1));
      }
    }

    {
      ren_prof_zone("Update mesh instance transforms");
      usize count = scene->mesh_instances.raw_size();

      auto [transforms, _0, transforms_staging_buffer] =
          ccfg.allocator->allocate<glm::mat4x3>(count);

      std::ranges::copy_n(scene->mesh_instance_transforms.raw_data(), count,
                          transforms);

      cmd.copy_buffer(transforms_staging_buffer,
                      rg.get_buffer(transform_matrices));
    }

    if (materials) {
      ren_prof_zone("Update materials");
      BufferSlice<glsl::Material> buffer = rg.get_buffer(materials);
      auto [ptr, _, staging_buffer] =
          rg.allocate<glsl::Material>(scene->update_materials.size());
      std::ranges::copy(scene->material_update_data, ptr);
      for (auto i : range(scene->update_materials.size())) {
        cmd.copy_buffer(staging_buffer.slice(i, 1),
                        buffer.slice(scene->update_materials[i], 1));
      }
    }

    if (directional_lights) {
      ren_prof_zone("Update directional_lights");
      BufferSlice<glsl::DirectionalLight> buffer =
          rg.get_buffer(directional_lights);
      auto [ptr, _, staging_buffer] = rg.allocate<glsl::DirectionalLight>(
          scene->update_directional_lights.size());
      std::ranges::copy(scene->directional_light_update_data, ptr);
      for (auto i : range(scene->update_directional_lights.size())) {
        cmd.copy_buffer(staging_buffer.slice(i, 1),
                        buffer.slice(scene->update_directional_lights[i], 1));
      }
    }
  });

  setup_calculate_normal_matrices_pass(ccfg, cfg.gpu_scene);
}

} // namespace ren
