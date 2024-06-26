#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"

namespace ren {

namespace {

struct UploadPassResources {
  NotNull<const Scene *> scene;
  RgBufferToken meshes;
  RgBufferToken materials;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken normal_matrices;
  RgBufferToken directional_lights;
};

void run_upload_pass(Renderer &renderer, const RgRuntime &rg,
                     const UploadPassResources &rcs) {
  const Scene &scene = *rcs.scene;

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  for (const auto &[h, material] : scene.get_materials()) {
    materials[h] = material;
  }

  auto *meshes_ptr = rg.map_buffer<glsl::Mesh>(rcs.meshes);
  const GenArray<Mesh> &meshes = scene.get_meshes();

  for (const auto &[h, mesh] : meshes) {
    meshes_ptr[h] = {
        .positions =
            renderer.get_buffer_device_ptr<glsl::Position>(mesh.positions),
        .normals = renderer.get_buffer_device_ptr<glsl::Normal>(mesh.normals),
        .tangents =
            renderer.try_get_buffer_device_ptr<glsl::Tangent>(mesh.tangents),
        .uvs = renderer.try_get_buffer_device_ptr<glsl::UV>(mesh.uvs),
        .colors = renderer.try_get_buffer_device_ptr<glsl::Color>(mesh.colors),
        .meshlets =
            renderer.get_buffer_device_ptr<glsl::Meshlet>(mesh.meshlets),
        .meshlet_indices =
            renderer.get_buffer_device_ptr<u32>(mesh.meshlet_indices),
        .bb = mesh.bb,
        .uv_bs = mesh.uv_bs,
        .index_pool = mesh.index_pool,
        .num_lods = u32(mesh.lods.size()),
    };
    std::ranges::copy(mesh.lods, meshes_ptr[h].lods);
  }

  auto *mesh_instances_ptr =
      rg.map_buffer<glsl::MeshInstance>(rcs.mesh_instances);
  auto *transform_matrices_ptr =
      rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  auto *normal_matrices_ptr = rg.map_buffer<glm::mat3>(rcs.normal_matrices);

  const GenArray<MeshInstance> &mesh_instances = scene.get_mesh_instances();
  const GenMap<glm::mat4x3, Handle<MeshInstance>> &transforms =
      scene.get_mesh_instance_transforms();

  for (const auto &[h, mesh_instance] : mesh_instances) {
    mesh_instances_ptr[h] = {
        .mesh = mesh_instance.mesh,
        .material = mesh_instance.material,
    };
    transform_matrices_ptr[h] = transforms[h];
    normal_matrices_ptr[h] =
        glm::transpose(glm::inverse(glm::mat3(transforms[h])));
  }

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  for (const auto &[h, light] : scene.get_directional_lights()) {
    directional_lights[h] = light;
  }
}

struct UploadPassConfig {
  NotNull<RgBufferId *> meshes;
  NotNull<RgBufferId *> materials;
  NotNull<RgBufferId *> mesh_instances;
  NotNull<RgBufferId *> transform_matrices;
  NotNull<RgBufferId *> normal_matrices;
  NotNull<RgBufferId *> directional_lights;
};

void setup_upload_pass(const PassCommonConfig &ccfg,
                       const UploadPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;
  const Scene &scene = *ccfg.scene;

  UploadPassResources rcs = {.scene = &scene};

  auto pass = rgb.create_pass({.name = "upload"});

  *cfg.materials = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glsl::Material) * scene.get_materials().size(),
  });

  std::tie(*cfg.materials, rcs.materials) =
      pass.write_buffer("materials", *cfg.materials, RG_HOST_WRITE_BUFFER);

  *cfg.meshes = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glsl::Mesh) * scene.get_meshes().size(),
  });

  std::tie(*cfg.meshes, rcs.meshes) =
      pass.write_buffer("meshes", *cfg.meshes, RG_HOST_WRITE_BUFFER);

  usize num_mesh_instances = scene.get_mesh_instances().size();

  *cfg.mesh_instances = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glsl::MeshInstance) * num_mesh_instances,
  });

  std::tie(*cfg.mesh_instances, rcs.mesh_instances) = pass.write_buffer(
      "mesh-instances", *cfg.mesh_instances, RG_HOST_WRITE_BUFFER);

  *cfg.transform_matrices = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glsl::mat4x3) * num_mesh_instances,
  });

  std::tie(*cfg.transform_matrices, rcs.transform_matrices) = pass.write_buffer(
      "transform-matrices", *cfg.transform_matrices, RG_HOST_WRITE_BUFFER);

  *cfg.normal_matrices = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glm::mat3) * num_mesh_instances,
  });

  std::tie(*cfg.normal_matrices, rcs.normal_matrices) = pass.write_buffer(
      "normal-matrices", *cfg.normal_matrices, RG_HOST_WRITE_BUFFER);

  *cfg.directional_lights = rgb.create_buffer({
      .heap = BufferHeap::Dynamic,
      .size = sizeof(glsl::DirLight) * scene.get_directional_lights().size(),
  });

  std::tie(*cfg.directional_lights, rcs.directional_lights) = pass.write_buffer(
      "directional-lights", *cfg.directional_lights, RG_HOST_WRITE_BUFFER);

  pass.set_host_callback([rcs](Renderer &renderer, const RgRuntime &rg) {
    run_upload_pass(renderer, rg, rcs);
  });
}

struct EarlyZPassResources {
  RgBufferToken meshes;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken commands;
  RgTextureToken depth_buffer;
};

void run_early_z_pass(Renderer &renderer, const RgRuntime &rg,
                      CommandRecorder &cmd, const Scene &scene,
                      const EarlyZPassResources &rcs) {}

struct EarlyZPassConfig {
  RgBufferId meshes;
  Span<const BufferView> index_pools;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  NotNull<RgTextureId *> depth_buffer;
};

void setup_early_z_pass(const PassCommonConfig &ccfg,
                        const EarlyZPassConfig &cfg) {
  const Scene &scene = *ccfg.scene;
  DepthOnlyMeshPassClass mesh_pass;
  mesh_pass.record(
      *ccfg.rgb,
      DepthOnlyMeshPassClass::BeginInfo{
          .base =
              {
                  .pass_name = "early-z",
                  .depth_attachment = cfg.depth_buffer,
                  .depth_attachment_ops =
                      {
                          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                          .store = VK_ATTACHMENT_STORE_OP_STORE,
                      },
                  .depth_attachment_name = "depth-buffer",
                  .host_meshes = &scene.get_meshes(),
                  .host_materials = &scene.get_materials(),
                  .host_mesh_instances = &scene.get_mesh_instances(),
                  .index_pools = cfg.index_pools,
                  .pipelines = ccfg.pipelines,
                  .draw_size = scene.get_draw_size(),
                  .num_draw_meshlets = scene.get_num_draw_meshlets(),
                  .meshes = cfg.meshes,
                  .mesh_instances = cfg.mesh_instances,
                  .transform_matrices = cfg.transform_matrices,
                  .upload_allocator = ccfg.allocator,
                  .instance_culling_and_lod_settings =
                      {
                          .feature_mask =
                              scene.get_instance_culling_and_lod_feature_mask(),
                          .lod_triangle_pixel_count =
                              scene.get_lod_triangle_pixel_count(),
                          .lod_bias = scene.get_lod_bias(),
                      },
                  .meshlet_culling_feature_mask =
                      scene.get_meshlet_culling_feature_mask(),
                  .viewport = scene.get_viewport(),
                  .proj_view = scene.get_camera_proj_view(),
                  .eye = scene.get_camera().position,
              },

      });
}

struct OpaquePassResources {
  RgBufferToken commands;
  RgBufferToken meshes;
  RgBufferToken materials;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken normal_matrices;
  RgBufferToken directional_lights;
  RgTextureToken hdr;
  RgTextureToken depth_buffer;
  RgTextureToken exposure;
};

void run_opaque_pass(Renderer &renderer, const RgRuntime &rg,
                     CommandRecorder &cmd, const Scene &scene,
                     const OpaquePassResources &rcs) {}

struct OpaquePassConfig {
  RgBufferId meshes;
  Span<const BufferView> index_pools;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  NotNull<RgTextureId *> hdr;
  RgTextureId depth_buffer;
  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
};

void setup_opaque_pass(const PassCommonConfig &ccfg,
                       const OpaquePassConfig &cfg) {
  const Scene &scene = *ccfg.scene;
  RgTextureId depth_buffer = cfg.depth_buffer;
  OpaqueMeshPassClass mesh_pass;
  mesh_pass
      .record(*ccfg.rgb,
              OpaqueMeshPassClass::BeginInfo{
                  .base =
                      {
                          .pass_name = "opaque",
                          .color_attachments = {cfg.hdr},
                          .color_attachment_ops = {{
                              .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                              .store = VK_ATTACHMENT_STORE_OP_STORE,
                              .clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                          }},
                          .color_attachment_names = {"hdr"},
                          .depth_attachment = &depth_buffer,
                          .depth_attachment_ops = scene.is_early_z_enabled() ?
                           DepthAttachmentOperations {
                                .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                .store = VK_ATTACHMENT_STORE_OP_NONE,
                           } :
                           DepthAttachmentOperations {
                               .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                               .store = VK_ATTACHMENT_STORE_OP_STORE,
                           },
                          .depth_attachment_name = "depth-buffer",
                          .host_meshes = &scene.get_meshes(),
                          .host_materials = &scene.get_materials(),
                          .host_mesh_instances = &scene.get_mesh_instances(),
                          .index_pools = cfg.index_pools,
                          .pipelines = ccfg.pipelines,
                          .draw_size = scene.get_draw_size(),
                          .num_draw_meshlets = scene.get_num_draw_meshlets(),
                          .meshes = cfg.meshes,
                          .materials = cfg.materials,
                          .mesh_instances = cfg.mesh_instances,
                          .transform_matrices = cfg.transform_matrices,
                          .normal_matrices = cfg.normal_matrices,
                          .upload_allocator = ccfg.allocator,
                          .instance_culling_and_lod_settings =
                              {
                                  .feature_mask = scene.get_instance_culling_and_lod_feature_mask(),
                                  .lod_triangle_pixel_count =
                                      scene.get_lod_triangle_pixel_count(),
                                  .lod_bias = scene.get_lod_bias(),
                              },
                          .meshlet_culling_feature_mask =
                              scene.get_meshlet_culling_feature_mask(),
                          .viewport = scene.get_viewport(),
                          .proj_view = scene.get_camera_proj_view(),
                          .eye = scene.get_camera().position,
                      },
                  .directional_lights = cfg.directional_lights,
                  .num_directional_lights =
                      u32(scene.get_directional_lights().size()),
                  .exposure = cfg.exposure,
                  .exposure_temporal_layer = cfg.exposure_temporal_layer,
              });
}

} // namespace

} // namespace ren

void ren::setup_opaque_passes(const PassCommonConfig &ccfg,
                              const OpaquePassesConfig &cfg) {
  const Scene &scene = *ccfg.scene;

  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;

  SmallVector<BufferView> index_pools;
  for (const IndexPool &pool : scene.get_index_pools()) {
    index_pools.push_back({
        .buffer = pool.indices,
        .size = sizeof(u8[glsl::INDEX_POOL_SIZE]),
    });
  }

  setup_upload_pass(ccfg, UploadPassConfig{
                              .meshes = &meshes,
                              .materials = &materials,
                              .mesh_instances = &mesh_instances,
                              .transform_matrices = &transform_matrices,
                              .normal_matrices = &normal_matrices,
                              .directional_lights = &directional_lights,
                          });

  glm::uvec2 viewport = scene.get_viewport();

  if (!ccfg.rcs->depth_buffer) {
    ccfg.rcs->depth_buffer = ccfg.rgp->create_texture({
        .name = "depth-buffer",
        .format = DEPTH_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId depth_buffer = ccfg.rcs->depth_buffer;

  if (scene.is_early_z_enabled()) {
    setup_early_z_pass(ccfg, EarlyZPassConfig{
                                 .meshes = meshes,
                                 .index_pools = index_pools,
                                 .mesh_instances = mesh_instances,
                                 .transform_matrices = transform_matrices,
                                 .depth_buffer = &depth_buffer,
                             });
  }

  if (!ccfg.rcs->hdr) {
    ccfg.rcs->hdr = ccfg.rgp->create_texture({
        .name = "hdr",
        .format = HDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  *cfg.hdr = ccfg.rcs->hdr;

  setup_opaque_pass(ccfg,
                    OpaquePassConfig{
                        .meshes = meshes,
                        .index_pools = index_pools,
                        .materials = materials,
                        .mesh_instances = mesh_instances,
                        .transform_matrices = transform_matrices,
                        .normal_matrices = normal_matrices,
                        .directional_lights = directional_lights,
                        .hdr = cfg.hdr,
                        .depth_buffer = depth_buffer,
                        .exposure = cfg.exposure,
                        .exposure_temporal_layer = cfg.exposure_temporal_layer,
                    });
}
