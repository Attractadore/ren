#include "Opaque.hpp"
#include "../Formats.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../Swapchain.hpp"
#include "HiZ.hpp"
#include "MeshPass.hpp"

namespace ren {

struct EarlyZPassConfig {
  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
  OcclusionCullingMode occlusion_culling_mode = OcclusionCullingMode::Disabled;
  NotNull<RgTextureId *> depth_buffer;
  RgTextureId hi_z;
};

void setup_early_z_pass(const PassCommonConfig &ccfg,
                        const EarlyZPassConfig &cfg) {
  record_mesh_pass(
      ccfg, DepthOnlyMeshPassInfo{
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
                        .camera = ccfg.scene->get_camera(),
                        .viewport = ccfg.swapchain->get_size(),
                        .gpu_scene = cfg.gpu_scene,
                        .rg_gpu_scene = cfg.rg_gpu_scene,
                        .occlusion_culling_mode = cfg.occlusion_culling_mode,
                        .hi_z = cfg.hi_z,
                    },

            });
}

struct OpaquePassConfig {
  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
  OcclusionCullingMode occlusion_culling_mode = OcclusionCullingMode::Disabled;
  NotNull<RgTextureId *> hdr;
  NotNull<RgTextureId *> depth_buffer;
  RgTextureId hi_z;
  RgTextureId exposure;
};

void setup_opaque_pass(const PassCommonConfig &ccfg,
                       const OpaquePassConfig &cfg) {
  record_mesh_pass(ccfg,
              OpaqueMeshPassInfo{
                  .base =
                      {
                          .pass_name = "opaque",
                          .color_attachments = {cfg.hdr},
                          .color_attachment_ops = {{
                              .load = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                              .store = VK_ATTACHMENT_STORE_OP_STORE,
                          }},
                          .color_attachment_names = {"hdr"},
                          .depth_attachment = cfg.depth_buffer,
                          .depth_attachment_ops = ccfg.scene->settings.early_z ?
                           DepthAttachmentOperations {
                                .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                .store = VK_ATTACHMENT_STORE_OP_NONE,
                           } :
                           DepthAttachmentOperations {
                               .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
                               .store = VK_ATTACHMENT_STORE_OP_STORE,
                           },
                          .depth_attachment_name = "depth-buffer",
                          .camera = ccfg.scene->get_camera(),
                          .viewport = ccfg.swapchain->get_size(),
                          .gpu_scene = cfg.gpu_scene,
                          .rg_gpu_scene = cfg.rg_gpu_scene,
                          .occlusion_culling_mode = cfg.occlusion_culling_mode,
                          .hi_z = cfg.hi_z,
                      },
                  .exposure = cfg.exposure,
                  .env_luminance = ccfg.scene->env_luminance,
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
  *cfg.depth_buffer = ccfg.rcs->depth_buffer;

  auto occlusion_culling_mode = OcclusionCullingMode::Disabled;
  if (ccfg.scene->settings.instance_occulusion_culling) {
    occlusion_culling_mode = OcclusionCullingMode::FirstPhase;
  }

  RgTextureId hi_z;
  auto setup_hi_z_pass = [&] {
    ren::setup_hi_z_pass(
        ccfg, HiZPassConfig{.depth_buffer = *cfg.depth_buffer, .hi_z = &hi_z});
  };

  if (scene.settings.early_z) {
    setup_early_z_pass(ccfg,
                       EarlyZPassConfig{
                           .gpu_scene = cfg.gpu_scene,
                           .rg_gpu_scene = cfg.rg_gpu_scene,
                           .occlusion_culling_mode = occlusion_culling_mode,
                           .depth_buffer = cfg.depth_buffer,
                       });
    if (occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
      setup_hi_z_pass();
      setup_early_z_pass(
          ccfg, EarlyZPassConfig{
                    .gpu_scene = cfg.gpu_scene,
                    .rg_gpu_scene = cfg.rg_gpu_scene,
                    .occlusion_culling_mode = OcclusionCullingMode::SecondPhase,
                    .depth_buffer = cfg.depth_buffer,
                    .hi_z = hi_z,
                });
      occlusion_culling_mode = OcclusionCullingMode::ThirdPhase;
    }
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

  setup_opaque_pass(ccfg, OpaquePassConfig{
                              .gpu_scene = cfg.gpu_scene,
                              .rg_gpu_scene = cfg.rg_gpu_scene,
                              .occlusion_culling_mode = occlusion_culling_mode,
                              .hdr = cfg.hdr,
                              .depth_buffer = cfg.depth_buffer,
                              .hi_z = hi_z,
                              .exposure = cfg.exposure,
                          });
  if (occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
    setup_hi_z_pass();
    setup_opaque_pass(
        ccfg, OpaquePassConfig{
                  .gpu_scene = cfg.gpu_scene,
                  .rg_gpu_scene = cfg.rg_gpu_scene,
                  .occlusion_culling_mode = OcclusionCullingMode::SecondPhase,
                  .hdr = cfg.hdr,
                  .depth_buffer = cfg.depth_buffer,
                  .hi_z = hi_z,
                  .exposure = cfg.exposure,
              });
  }
}
