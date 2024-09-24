#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"

namespace ren {

namespace {

struct UploadPassResources {
  NotNull<const SceneData *> scene;
  RgBufferToken<glsl::Mesh> meshes;
  RgBufferToken<glsl::Material> materials;
  RgBufferToken<glsl::MeshInstance> mesh_instances;
  RgBufferToken<glm::mat4x3> transform_matrices;
  RgBufferToken<glm::mat3> normal_matrices;
  RgBufferToken<glsl::DirLight> directional_lights;
};

void run_upload_pass(Renderer &renderer, const RgRuntime &rg,
                     const UploadPassResources &rcs) {
  const SceneData &scene = *rcs.scene;

  auto *materials = rg.map_buffer(rcs.materials);
  for (const auto &[h, material] : scene.materials) {
    materials[h] = material;
  }

  auto *meshes_ptr = rg.map_buffer(rcs.meshes);

  for (const auto &[h, mesh] : scene.meshes) {
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

  auto *mesh_instances_ptr = rg.map_buffer(rcs.mesh_instances);
  auto *transform_matrices_ptr = rg.map_buffer(rcs.transform_matrices);
  auto *normal_matrices_ptr = rg.map_buffer(rcs.normal_matrices);

  for (const auto &[h, mesh_instance] : scene.mesh_instances) {
    mesh_instances_ptr[h] = {
        .mesh = mesh_instance.mesh,
        .material = mesh_instance.material,
    };
    glm::mat4 transform = scene.mesh_instance_transforms[h];
    transform_matrices_ptr[h] = transform;
    normal_matrices_ptr[h] = glm::transpose(glm::inverse(glm::mat3(transform)));
  }

  auto *directional_lights = rg.map_buffer(rcs.directional_lights);
  for (const auto &[h, light] : scene.dir_lights) {
    directional_lights[h] = light;
  }
}

struct UploadPassConfig {
  NotNull<RgBufferId<glsl::Mesh> *> meshes;
  NotNull<RgBufferId<glsl::Material> *> materials;
  NotNull<RgBufferId<glsl::MeshInstance> *> mesh_instances;
  NotNull<RgBufferId<glm::mat4x3> *> transform_matrices;
  NotNull<RgBufferId<glm::mat3> *> normal_matrices;
  NotNull<RgBufferId<glsl::DirLight> *> directional_lights;
};

void setup_upload_pass(const PassCommonConfig &ccfg,
                       const UploadPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;
  const SceneData &scene = *ccfg.scene;

  UploadPassResources rcs = {.scene = &scene};

  auto pass = rgb.create_pass({.name = "upload"});

  *cfg.materials = rgb.create_buffer<glsl::Material>({
      .heap = BufferHeap::Dynamic,
      .size = scene.materials.size(),
  });

  std::tie(*cfg.materials, rcs.materials) =
      pass.write_buffer("materials", *cfg.materials, HOST_WRITE_BUFFER);

  *cfg.meshes = rgb.create_buffer<glsl::Mesh>({
      .heap = BufferHeap::Dynamic,
      .size = scene.meshes.size(),
  });

  std::tie(*cfg.meshes, rcs.meshes) =
      pass.write_buffer("meshes", *cfg.meshes, HOST_WRITE_BUFFER);

  usize num_mesh_instances = scene.mesh_instances.size();

  *cfg.mesh_instances = rgb.create_buffer<glsl::MeshInstance>({
      .heap = BufferHeap::Dynamic,
      .size = num_mesh_instances,
  });

  std::tie(*cfg.mesh_instances, rcs.mesh_instances) = pass.write_buffer(
      "mesh-instances", *cfg.mesh_instances, HOST_WRITE_BUFFER);

  *cfg.transform_matrices = rgb.create_buffer<glm::mat4x3>({
      .heap = BufferHeap::Dynamic,
      .size = num_mesh_instances,
  });

  std::tie(*cfg.transform_matrices, rcs.transform_matrices) = pass.write_buffer(
      "transform-matrices", *cfg.transform_matrices, HOST_WRITE_BUFFER);

  *cfg.normal_matrices = rgb.create_buffer<glm::mat3>({
      .heap = BufferHeap::Dynamic,
      .size = num_mesh_instances,
  });

  std::tie(*cfg.normal_matrices, rcs.normal_matrices) = pass.write_buffer(
      "normal-matrices", *cfg.normal_matrices, HOST_WRITE_BUFFER);

  *cfg.directional_lights = rgb.create_buffer<glsl::DirLight>({
      .heap = BufferHeap::Dynamic,
      .size = scene.dir_lights.size(),
  });

  std::tie(*cfg.directional_lights, rcs.directional_lights) = pass.write_buffer(
      "directional-lights", *cfg.directional_lights, HOST_WRITE_BUFFER);

  pass.set_host_callback([rcs](Renderer &renderer, const RgRuntime &rg) {
    run_upload_pass(renderer, rg, rcs);
  });
}

struct EarlyZPassConfig {
  RgBufferId<glsl::Mesh> meshes;
  Span<const BufferView> index_pools;
  RgBufferId<glsl::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  NotNull<RgTextureId *> depth_buffer;
};

void setup_early_z_pass(const PassCommonConfig &ccfg,
                        const EarlyZPassConfig &cfg) {
  const SceneData &scene = *ccfg.scene;
  DepthOnlyMeshPassClass mesh_pass;
  mesh_pass.record(*ccfg.rgb,
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
                               .pipelines = ccfg.pipelines,
                               .scene = ccfg.scene,
                               .camera = ccfg.scene->get_camera(),
                               .viewport = ccfg.swapchain->get_size(),
                               .meshes = cfg.meshes,
                               .mesh_instances = cfg.mesh_instances,
                               .transform_matrices = cfg.transform_matrices,
                               .upload_allocator = ccfg.allocator,
                           },

                   });
}

struct OpaquePassConfig {
  RgBufferId<glsl::Mesh> meshes;
  Span<const BufferView> index_pools;
  RgBufferId<glsl::Material> materials;
  RgBufferId<glsl::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<glm::mat3> normal_matrices;
  RgBufferId<glsl::DirLight> directional_lights;
  NotNull<RgTextureId *> hdr;
  RgTextureId depth_buffer;
  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
};

void setup_opaque_pass(const PassCommonConfig &ccfg,
                       const OpaquePassConfig &cfg) {
  const SceneData &scene = *ccfg.scene;
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
                          .depth_attachment_ops = scene.settings.early_z ?
                           DepthAttachmentOperations {
                                .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                .store = VK_ATTACHMENT_STORE_OP_NONE,
                           } :
                           DepthAttachmentOperations {
                               .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                               .store = VK_ATTACHMENT_STORE_OP_STORE,
                           },
                          .depth_attachment_name = "depth-buffer",
                          .pipelines = ccfg.pipelines,
                          .scene = ccfg.scene,
                          .camera = ccfg.scene->get_camera(),
                          .viewport = ccfg.swapchain->get_size(),
                          .meshes = cfg.meshes,
                          .materials = cfg.materials,
                          .mesh_instances = cfg.mesh_instances,
                          .transform_matrices = cfg.transform_matrices,
                          .normal_matrices = cfg.normal_matrices,
                          .upload_allocator = ccfg.allocator,
                      },
                  .directional_lights = cfg.directional_lights,
                  .exposure = cfg.exposure,
                  .exposure_temporal_layer = cfg.exposure_temporal_layer,
              });
}

} // namespace

} // namespace ren

void ren::setup_opaque_passes(const PassCommonConfig &ccfg,
                              const OpaquePassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<glsl::Mesh> meshes;
  RgBufferId<glsl::Material> materials;
  RgBufferId<glsl::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<glm::mat3> normal_matrices;
  RgBufferId<glsl::DirLight> directional_lights;

  SmallVector<BufferView> index_pools;
  for (const IndexPool &pool : scene.index_pools) {
    index_pools.push_back({
        .buffer = pool.indices,
        .count = sizeof(u8[glsl::INDEX_POOL_SIZE]),
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

  glm::uvec2 viewport = ccfg.swapchain->get_size();

  if (!ccfg.rcs->depth_buffer) {
    ccfg.rcs->depth_buffer = ccfg.rgp->create_texture({
        .name = "depth-buffer",
        .format = DEPTH_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId depth_buffer = ccfg.rcs->depth_buffer;

  if (scene.settings.early_z) {
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
