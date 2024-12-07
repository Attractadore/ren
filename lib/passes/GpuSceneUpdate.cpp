#include "GpuSceneUpdate.hpp"
#include "CommandRecorder.hpp"
#include "Profiler.hpp"
#include "Scene.hpp"
#include "core/Views.hpp"
#include "glsl/CalculateNormalMatrices.h"

#include <algorithm>
#include <fmt/format.h>

namespace ren {

auto rg_import_gpu_scene(RgBuilder &rgb, const GpuScene &gpu_scene)
    -> RgGpuScene {
  RgGpuScene rg_gpu_scene = {
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

  for (auto i : range(NUM_DRAW_SETS)) {
    const DrawSetData &ds = gpu_scene.draw_sets[i];
    rg_gpu_scene.draw_sets[i] = {
        .cull_data = rgb.create_buffer(
            fmt::format("{}-draw-set", get_draw_set_name((DrawSet)(1 << i))),
            ds.cull_data),
    };
  }

  return rg_gpu_scene;
}

void rg_export_gpu_scene(const RgBuilder &rgb, const RgGpuScene &rg_gpu_scene,
                         NotNull<GpuScene *> gpu_scene) {
  gpu_scene->meshes.state = rgb.get_final_buffer_state(rg_gpu_scene.meshes);
  gpu_scene->mesh_instances.state =
      rgb.get_final_buffer_state(rg_gpu_scene.mesh_instances);
  gpu_scene->mesh_instance_visibility.state =
      rgb.get_final_buffer_state(rg_gpu_scene.mesh_instance_visibility);
  for (auto i : range(NUM_DRAW_SETS)) {
    gpu_scene->draw_sets[i].cull_data.state =
        rgb.get_final_buffer_state(rg_gpu_scene.draw_sets[i].cull_data);
  }
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
        cmd.set_push_constants(glsl::CalculateNormalMatricesArgs{
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

  auto pass = rgb.create_pass({"gpu-scene-update"});

  struct {
    const SceneData *scene = nullptr;
    GpuScene *gpu_scene = nullptr;
    RgBufferToken<glsl::Mesh> meshes;
    RgBufferToken<glsl::MeshInstance> mesh_instances;
    std::array<RgBufferToken<glsl::InstanceCullData>, NUM_DRAW_SETS> draw_sets;
    RgBufferToken<glm::mat4x3> transform_matrices;
    RgBufferToken<glsl::Material> materials;
    RgBufferToken<glsl::DirectionalLight> directional_lights;
  } rcs;

  rcs.scene = ccfg.scene;
  rcs.gpu_scene = cfg.gpu_scene;

  if (not cfg.gpu_scene->update_meshes.empty()) {
    std::tie(cfg.rg_gpu_scene->meshes, rcs.meshes) = pass.write_buffer(
        "meshes-updated", cfg.rg_gpu_scene->meshes, TRANSFER_DST_BUFFER);
  }

  if (not cfg.gpu_scene->update_mesh_instances.empty()) {
    std::tie(cfg.rg_gpu_scene->mesh_instances, rcs.mesh_instances) =
        pass.write_buffer("mesh-instances-updated",
                          cfg.rg_gpu_scene->mesh_instances,
                          TRANSFER_DST_BUFFER);
  }

  for (auto i : range(NUM_DRAW_SETS)) {
    const DrawSetData &ds = cfg.gpu_scene->draw_sets[i];
    if (not ds.update_cull_data.empty() or not ds.delete_ids.empty()) {
      std::tie(cfg.rg_gpu_scene->draw_sets[i].cull_data, rcs.draw_sets[i]) =
          pass.write_buffer(fmt::format("{}-draw-set-updated",
                                        get_draw_set_name((DrawSet)(1 << i))),
                            cfg.rg_gpu_scene->draw_sets[i].cull_data,
                            TRANSFER_SRC_BUFFER | TRANSFER_DST_BUFFER);
    }
  }

  std::tie(cfg.rg_gpu_scene->transform_matrices, rcs.transform_matrices) =
      pass.write_buffer("transform-matrices",
                        cfg.rg_gpu_scene->transform_matrices,
                        TRANSFER_DST_BUFFER);

  if (not cfg.gpu_scene->update_materials.empty()) {
    std::tie(cfg.rg_gpu_scene->materials, rcs.materials) = pass.write_buffer(
        "materials-updated", cfg.rg_gpu_scene->materials, TRANSFER_DST_BUFFER);
  }

  if (not cfg.gpu_scene->update_directional_lights.empty()) {
    std::tie(cfg.rg_gpu_scene->directional_lights, rcs.directional_lights) =
        pass.write_buffer("directional-lights-updated",
                          cfg.rg_gpu_scene->directional_lights,
                          TRANSFER_DST_BUFFER);
  }

  pass.set_callback([rcs](Renderer &renderer, const RgRuntime &rg,
                          CommandRecorder &cmd) {
    if (rcs.meshes) {
      ren_prof_zone("Update meshes");
      auto update_meshes =
          rg.allocate<glsl::Mesh>(rcs.gpu_scene->update_meshes.size());
      std::ranges::copy(rcs.gpu_scene->mesh_update_data,
                        update_meshes.host_ptr);
      BufferSlice<glsl::Mesh> meshes = rg.get_buffer(rcs.meshes);
      for (auto i : range(rcs.gpu_scene->update_meshes.size())) {
        cmd.copy_buffer(update_meshes.slice.slice(i, 1),
                        meshes.slice(rcs.gpu_scene->update_meshes[i], 1));
      }
      rcs.gpu_scene->update_meshes.clear();
      rcs.gpu_scene->mesh_update_data.clear();
    }

    if (rcs.mesh_instances) {
      ren_prof_zone("Update mesh instances");
      auto update_mesh_instances = rg.allocate<glsl::MeshInstance>(
          rcs.gpu_scene->update_mesh_instances.size());
      std::ranges::copy(rcs.gpu_scene->mesh_instance_update_data,
                        update_mesh_instances.host_ptr);
      BufferSlice<glsl::MeshInstance> buffer =
          rg.get_buffer(rcs.mesh_instances);
      for (auto i : range(rcs.gpu_scene->update_mesh_instances.size())) {
        cmd.copy_buffer(
            update_mesh_instances.slice.slice(i, 1),
            buffer.slice(rcs.gpu_scene->update_mesh_instances[i], 1));
      }
      rcs.gpu_scene->update_mesh_instances.clear();
      rcs.gpu_scene->mesh_instance_update_data.clear();
    }

    {
      ren_prof_zone("Update mesh instance transforms");
      usize count = rcs.scene->mesh_instances.raw_size();
      auto transforms = rg.allocate<glm::mat4x3>(count);
      std::ranges::copy_n(rcs.scene->mesh_instance_transforms.raw_data(), count,
                          transforms.host_ptr);
      cmd.copy_buffer(transforms.slice, rg.get_buffer(rcs.transform_matrices));
    }

    for (auto s : range(NUM_DRAW_SETS)) {
      if (!rcs.draw_sets[s]) {
        continue;
      }

      DrawSetData &ds = rcs.gpu_scene->draw_sets[s];

      auto update_cull_data =
          rg.allocate<glsl::InstanceCullData>(ds.update_cull_data.size());
      std::ranges::copy(ds.update_cull_data, update_cull_data.host_ptr);
      BufferSlice<glsl::InstanceCullData> cull_data =
          rg.get_buffer(rcs.draw_sets[s]);

      usize num_add = ds.update_cull_data.size();
      usize num_delete = ds.delete_ids.size();

      auto update_mesh_instance_draw_set_id = [&](DrawSetId id) {
        auto &mi =
            (MeshInstance &)rcs.scene->mesh_instances[ds.mesh_instances[id]];
        ren_assert(mi.draw_sets.is_set((DrawSet)(1 << s)));
        mi.draw_set_ids[s] = id;
      };

      // Copy added items into holes from deleted items.
      usize num_replace = std::min(num_add, num_delete);
      for (auto i : range(num_replace)) {
        DrawSetId delete_id = ds.delete_ids[i];
        ds.mesh_instances[delete_id] = ds.mesh_instances.back();
        ds.mesh_instances.pop_back();
        update_mesh_instance_draw_set_id(delete_id);
        cmd.copy_buffer(update_cull_data.slice.slice(num_add - i - 1, 1),
                        cull_data.slice(delete_id, 1));
      }

      if (num_delete > num_replace) {
        // Swap items at the back with deleted items.
        Span<DrawSetId> delete_ids = Span(ds.delete_ids).subspan(num_replace);
        std::ranges::sort(delete_ids);
        for (auto i : range(delete_ids.size())) {
          DrawSetId last_id(ds.mesh_instances.size() - 1);
          if (last_id == delete_ids.back()) {
            // If the last item will be deleted, don't swap anything
            // (overlapping copy source and destination is UB in Vulkan).
            delete_ids = delete_ids.subspan(0, delete_ids.size() - 1);
          } else {
            // If the last item won't be deleted, swap it with the deleted item.
            DrawSetId delete_id = delete_ids.front();
            delete_ids = delete_ids.subspan(1);
            ds.mesh_instances[delete_id] = ds.mesh_instances.back();
            update_mesh_instance_draw_set_id(delete_id);
            cmd.copy_buffer(cull_data.slice(last_id, 1),
                            cull_data.slice(delete_id, 1));
          }
          ds.mesh_instances.pop_back();
        }
      } else if (num_add > num_replace) {
        // Copy leftover new items to the end.
        usize num = num_add - num_replace;
        usize prev_end = ds.mesh_instances.size() - num;
        cmd.copy_buffer(update_cull_data.slice.slice(0, num),
                        cull_data.slice(prev_end, num));
      }

      ds.update_cull_data.clear();
      ds.delete_ids.clear();
    }

    if (rcs.materials) {
      ren_prof_zone("Update materials");
      auto update_materials =
          rg.allocate<glsl::Material>(rcs.gpu_scene->update_materials.size());
      std::ranges::copy(rcs.gpu_scene->material_update_data,
                        update_materials.host_ptr);
      BufferSlice<glsl::Material> materials = rg.get_buffer(rcs.materials);
      for (auto i : range(rcs.gpu_scene->update_materials.size())) {
        cmd.copy_buffer(update_materials.slice.slice(i, 1),
                        materials.slice(rcs.gpu_scene->update_materials[i], 1));
      }
      rcs.gpu_scene->update_materials.clear();
      rcs.gpu_scene->material_update_data.clear();
    }

    if (rcs.directional_lights) {
      ren_prof_zone("Update directional_lights");
      auto update_directional_lights = rg.allocate<glsl::DirectionalLight>(
          rcs.gpu_scene->update_directional_lights.size());
      std::ranges::copy(rcs.gpu_scene->directional_light_update_data,
                        update_directional_lights.host_ptr);
      BufferSlice<glsl::DirectionalLight> buffer =
          rg.get_buffer(rcs.directional_lights);
      for (auto i : range(rcs.gpu_scene->update_directional_lights.size())) {
        cmd.copy_buffer(
            update_directional_lights.slice.slice(i, 1),
            buffer.slice(rcs.gpu_scene->update_directional_lights[i], 1));
      }
      rcs.gpu_scene->update_directional_lights.clear();
      rcs.gpu_scene->directional_light_update_data.clear();
    }
  });

  setup_calculate_normal_matrices_pass(ccfg, cfg.rg_gpu_scene);
}

} // namespace ren
