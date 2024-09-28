#include "Passes/Opaque.hpp"
#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"

namespace ren {

struct EarlyZPassConfig {
  RgGpuScene gpu_scene;
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
                               .gpu_scene = cfg.gpu_scene,
                               .upload_allocator = ccfg.allocator,
                           },

                   });
}

struct OpaquePassConfig {
  RgGpuScene gpu_scene;
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
                          .gpu_scene = cfg.gpu_scene,
                          .upload_allocator = ccfg.allocator,
                      },
                  .exposure = cfg.exposure,
                  .exposure_temporal_layer = cfg.exposure_temporal_layer,
              });
}

} // namespace ren

void ren::setup_opaque_passes(const PassCommonConfig &ccfg,
                              const OpaquePassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

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
                                 .gpu_scene = cfg.gpu_scene,
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
                        .gpu_scene = cfg.gpu_scene,
                        .hdr = cfg.hdr,
                        .depth_buffer = depth_buffer,
                        .exposure = cfg.exposure,
                        .exposure_temporal_layer = cfg.exposure_temporal_layer,
                    });
}
