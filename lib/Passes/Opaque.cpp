#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "glsl/InstanceCullingAndLODPass.h"

namespace ren {

namespace {

struct UploadPassResources {
  RgRWVariableToken<Vector<BufferView>> index_pools;

  RgBufferToken meshes;
  RgBufferToken materials;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken normal_matrices;
  RgBufferToken directional_lights;
};

void run_upload_pass(Renderer &renderer, const RgRuntime &rg,
                     const Scene &scene, const UploadPassResources &rcs) {
  ren_assert(rcs.materials);
  ren_assert(rcs.transform_matrices);
  ren_assert(rcs.normal_matrices);
  ren_assert(rcs.directional_lights);

  const Camera &camera = scene.get_camera();
  glm::uvec2 viewport = scene.get_viewport();

  glm::mat4 proj = get_projection_matrix(camera, viewport);
  glm::mat4 view = get_view_matrix(camera);

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

  Vector<BufferView> &index_pools = rg.get_variable(rcs.index_pools);
  index_pools.clear();
  for (const IndexPool &pool : scene.get_index_pools()) {
    index_pools.push_back(renderer.get_buffer_view(pool.indices));
  }
}

struct UploadPassConfig {
  u32 num_meshes;
  u32 num_mesh_instances;
  u32 num_materials;
  u32 num_directional_lights;
};

struct UploadPassOutput {
  RgVariableId<Vector<BufferView>> index_pools;

  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
};

auto setup_upload_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                       const UploadPassConfig &cfg) -> UploadPassOutput {
  UploadPassResources rcs;
  UploadPassOutput out;

  auto pass = rgb.create_pass({.name = "upload"});

  std::tie(out.index_pools, rcs.index_pools) =
      pass.create_variable<Vector<BufferView>>("index-pools");

  std::tie(out.materials, rcs.materials) = pass.create_buffer(
      {
          .name = "materials",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::Material) * cfg.num_materials,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.meshes, rcs.meshes) = pass.create_buffer(
      {
          .name = "meshes",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::Mesh) * cfg.num_meshes,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.mesh_instances, rcs.mesh_instances) = pass.create_buffer(
      {
          .name = "mesh-instances",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::MeshInstance) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.transform_matrices, rcs.transform_matrices) = pass.create_buffer(
      {
          .name = "transform-matrices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::mat4x3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.normal_matrices, rcs.normal_matrices) = pass.create_buffer(
      {
          .name = "normal-matrices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glm::mat3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.directional_lights, rcs.directional_lights) = pass.create_buffer(
      {
          .name = "directional-lights",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::DirLight) * cfg.num_directional_lights,
      },
      RG_HOST_WRITE_BUFFER);

  pass.set_host_callback([=](Renderer &renderer, const RgRuntime &rt) {
    run_upload_pass(renderer, rt, *scene, rcs);
  });

  return out;
}

struct EarlyZPassResources {
  RgVariableToken<Vector<BufferView>> index_pools;
  RgRWVariableToken<DepthOnlyMeshPassClass> mesh_pass;
  RgBufferToken meshes;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken commands;
  RgTextureToken depth_buffer;
};

void run_early_z_pass(Renderer &renderer, const RgRuntime &rg,
                      CommandRecorder &cmd, const Scene &scene,
                      const EarlyZPassResources &rcs) {
  u32 feature_mask = 0;
  if (scene.is_frustum_culling_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
  }
  if (scene.is_lod_selection_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
  }

  auto &mesh_pass = rg.get_variable(rcs.mesh_pass);
  mesh_pass.execute(
      renderer, cmd,
      DepthOnlyMeshPassClass::BeginInfo{
          .base =
              {
                  .host_meshes = &scene.get_meshes(),
                  .host_materials = &scene.get_materials(),
                  .host_mesh_instances = &scene.get_mesh_instances(),
                  .index_pools = rg.get_variable(rcs.index_pools),
                  .pipelines = &scene.get_pipelines(),
                  .draw_size = scene.get_draw_size(),
                  .num_draw_meshlets = scene.get_num_draw_meshlets(),
                  .meshes = rg.get_buffer(rcs.meshes),
                  .mesh_instances = rg.get_buffer(rcs.mesh_instances),
                  .transform_matrices = rg.get_buffer(rcs.transform_matrices),
                  .commands = rg.get_buffer(rcs.commands),
                  .device_allocator = &rg.get_device_allocator(),
                  .upload_allocator = &rg.get_upload_allocator(),
                  .instance_culling_and_lod_settings =
                      {
                          .feature_mask = feature_mask,
                          .lod_triangle_pixel_count =
                              scene.get_lod_triangle_pixel_count(),
                          .lod_bias = scene.get_lod_bias(),
                      },
                  .meshlet_culling_feature_mask =
                      scene.get_meshlet_culling_feature_mask(),
                  .depth_stencil_attachment =
                      DepthStencilAttachment{
                          .texture = renderer.get_texture_view(
                              rg.get_texture(rcs.depth_buffer)),
                          .depth_ops =
                              DepthAttachmentOperations{
                                  .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                  .store = VK_ATTACHMENT_STORE_OP_STORE,
                              },

                      },
                  .viewport = scene.get_viewport(),
                  .proj_view = scene.get_camera_proj_view(),
                  .eye = scene.get_camera().position,
              },

      });
}

struct EarlyZPassConfig {
  RgVariableId<Vector<BufferView>> index_pools;
  RgBufferId meshes;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId commands;
};

struct EarlyZPassOutput {
  RgBufferId commands;
  RgTextureId depth_buffer;
};

auto setup_early_z_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                        const EarlyZPassConfig &cfg) -> EarlyZPassOutput {
  EarlyZPassResources rcs;
  EarlyZPassOutput out;

  auto pass = rgb.create_pass({.name = "early-z"});

  rcs.index_pools = pass.read_variable(cfg.index_pools);

  std::tie(std::ignore, rcs.mesh_pass) =
      pass.create_variable<DepthOnlyMeshPassClass>("early-z-mesh-pass");

  rcs.meshes = pass.read_buffer(cfg.meshes, RG_VS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer(cfg.mesh_instances, RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_VS_READ_BUFFER);

  // TODO/FIXME: resource is used for scratch space, so don't need to track
  // dependencies.
  std::tie(out.commands, rcs.commands) =
      pass.write_buffer("indirect-commands-after-early-z", cfg.commands,
                        RG_INDIRECT_COMMAND_BUFFER | RG_CS_WRITE_BUFFER);

  glm::uvec2 viewport = scene->get_viewport();

  std::tie(out.depth_buffer, rcs.depth_buffer) = pass.create_texture(
      {
          .name = "depth-buffer",
          .format = DEPTH_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      RG_READ_WRITE_DEPTH_ATTACHMENT);

  pass.set_callback(
      [=](Renderer &renderer, const RgRuntime &rt, CommandRecorder &cmd) {
        run_early_z_pass(renderer, rt, cmd, *scene, rcs);
      });

  return out;
}

struct OpaquePassResources {
  RgVariableToken<Vector<BufferView>> index_pools;
  RgRWVariableToken<OpaqueMeshPassClass> mesh_pass;
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
                     const OpaquePassResources &rcs) {
  u32 feature_mask = 0;
  if (scene.is_frustum_culling_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
  }
  if (scene.is_lod_selection_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
  }

  auto &mesh_pass = rg.get_variable(rcs.mesh_pass);
  mesh_pass
      .execute(
          renderer, cmd,
          OpaqueMeshPassClass::BeginInfo{
              .base =
                  {
                      .host_meshes = &scene.get_meshes(),
                      .host_materials = &scene.get_materials(),
                      .host_mesh_instances = &scene.get_mesh_instances(),
                      .index_pools = rg.get_variable(rcs.index_pools),
                      .pipelines = &scene.get_pipelines(),
                      .draw_size = scene.get_draw_size(),
                      .num_draw_meshlets = scene.get_num_draw_meshlets(),
                      .meshes = rg.get_buffer(rcs.meshes),
                      .materials = rg.get_buffer(rcs.materials),
                      .mesh_instances = rg.get_buffer(rcs.mesh_instances),
                      .transform_matrices =
                          rg.get_buffer(rcs.transform_matrices),
                      .normal_matrices = rg.get_buffer(rcs.normal_matrices),
                      .commands = rg.get_buffer(rcs.commands),
                      .textures = rg.get_texture_set(),
                      .device_allocator = &rg.get_device_allocator(),
                      .upload_allocator = &rg.get_upload_allocator(),
                      .instance_culling_and_lod_settings =
                          {
                              .feature_mask = feature_mask,
                              .lod_triangle_pixel_count =
                                  scene.get_lod_triangle_pixel_count(),
                              .lod_bias = scene.get_lod_bias(),
                          },
                      .meshlet_culling_feature_mask =
                          scene.get_meshlet_culling_feature_mask(),
                      .color_attachments = {ColorAttachment{
                          .texture = renderer.get_texture_view(
                              rg.get_texture(rcs.hdr)),
                          .ops =
                              {
                                  .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                  .store = VK_ATTACHMENT_STORE_OP_STORE,
                                  .clear_color =
                                      glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                              },
                      }},
                      .depth_stencil_attachment =
                          DepthStencilAttachment{
                              .texture = renderer.get_texture_view(rg.get_texture(rcs.depth_buffer)),
                              .depth_ops =
                                  DepthAttachmentOperations{
                                      .load = scene
                                                      .is_early_z_enabled()
                                                  ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                  : VK_ATTACHMENT_LOAD_OP_CLEAR,
                                      .store = scene
                                                       .is_early_z_enabled()
                                                   ? VK_ATTACHMENT_STORE_OP_NONE
                                                   : VK_ATTACHMENT_STORE_OP_STORE,
                                  },

                          },
                      .viewport = scene.get_viewport(),
                      .proj_view = scene.get_camera_proj_view(),
                      .eye = scene.get_camera().position,
                  },
              .num_directional_lights =
                  u32(scene.get_directional_lights().size()),
              .directional_lights = rg.get_buffer(rcs.directional_lights),
              .exposure = rg.get_storage_texture_descriptor(rcs.exposure),
          });
}

struct OpaquePassConfig {
  ExposurePassOutput exposure;
  RgVariableId<Vector<BufferView>> index_pools;
  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgBufferId commands;
  RgTextureId depth_buffer;
};

auto setup_opaque_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                       const OpaquePassConfig &cfg) -> RgTextureId {
  OpaquePassResources rcs;
  auto pass = rgb.create_pass({.name = "opaque"});

  rcs.index_pools = pass.read_variable(cfg.index_pools);

  std::tie(std::ignore, rcs.mesh_pass) =
      pass.create_variable<OpaqueMeshPassClass>("opaque-mesh-pass");

  rcs.meshes = pass.read_buffer(cfg.meshes, RG_VS_READ_BUFFER);

  rcs.materials = pass.read_buffer(cfg.materials, RG_FS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer(cfg.mesh_instances, RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_VS_READ_BUFFER);

  rcs.normal_matrices =
      pass.read_buffer(cfg.normal_matrices, RG_VS_READ_BUFFER);

  rcs.directional_lights =
      pass.read_buffer(cfg.directional_lights, RG_FS_READ_BUFFER);

  std::tie(std::ignore, rcs.commands) =
      pass.write_buffer("indirect-commands-after-opaque", cfg.commands,
                        RG_INDIRECT_COMMAND_BUFFER | RG_CS_WRITE_BUFFER);

  glm::uvec2 viewport = scene->get_viewport();

  RgTextureId hdr;
  std::tie(hdr, rcs.hdr) = pass.create_texture(
      {
          .name = "hdr",
          .format = HDR_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      RG_COLOR_ATTACHMENT);

  if (scene->is_early_z_enabled()) {
    rcs.depth_buffer =
        pass.read_texture(cfg.depth_buffer, RG_READ_DEPTH_ATTACHMENT);
  } else {
    std::tie(std::ignore, rcs.depth_buffer) = pass.create_texture(
        {
            .name = "depth-buffer",
            .format = DEPTH_FORMAT,
            .width = viewport.x,
            .height = viewport.y,
        },
        RG_READ_WRITE_DEPTH_ATTACHMENT);
  }

  rcs.exposure = pass.read_texture(cfg.exposure.exposure, RG_FS_READ_TEXTURE,
                                   cfg.exposure.temporal_layer);

  pass.set_callback(
      [=](Renderer &renderer, const RgRuntime &rt, CommandRecorder &cmd) {
        run_opaque_pass(renderer, rt, cmd, *scene, rcs);
      });

  return hdr;
}

} // namespace

auto setup_opaque_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                         const OpaquePassesConfig &cfg) -> RgTextureId {
  UploadPassOutput upload = setup_upload_pass(
      rgb, scene,
      UploadPassConfig{
          .num_meshes = cfg.num_meshes,
          .num_mesh_instances = cfg.num_mesh_instances,
          .num_materials = cfg.num_materials,
          .num_directional_lights = cfg.num_directional_lights,
      });

  RgBufferId commands = rgb.create_buffer({
      .name = "indirect-commands",
      .heap = BufferHeap::Static,
      .size = sizeof(glsl::DrawIndexedIndirectCommand) *
              scene->get_num_draw_meshlets(),
  });

  RgTextureId depth_buffer;
  if (scene->is_early_z_enabled()) {
    EarlyZPassOutput early_z =
        setup_early_z_pass(rgb, scene,
                           EarlyZPassConfig{
                               .index_pools = upload.index_pools,
                               .meshes = upload.meshes,
                               .mesh_instances = upload.mesh_instances,
                               .transform_matrices = upload.transform_matrices,
                               .commands = commands,
                           });
    commands = early_z.commands;
    depth_buffer = early_z.depth_buffer;
  }

  return setup_opaque_pass(rgb, scene,
                           OpaquePassConfig{
                               .exposure = cfg.exposure,
                               .index_pools = upload.index_pools,
                               .meshes = upload.meshes,
                               .materials = upload.materials,
                               .mesh_instances = upload.mesh_instances,
                               .transform_matrices = upload.transform_matrices,
                               .normal_matrices = upload.normal_matrices,
                               .directional_lights = upload.directional_lights,
                               .commands = commands,
                               .depth_buffer = depth_buffer,
                           });
}

} // namespace ren
