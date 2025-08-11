#include "Opaque.hpp"
#include "../Scene.hpp"
#include "MeshPass.hpp"

namespace ren {

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
                                .load = rhi::RenderPassLoadOp::Clear,
                                .store = rhi::RenderPassStoreOp::Store,
                            },
                        .depth_attachment_name = "depth-buffer",
                        .camera = ccfg.scene->get_camera(),
                        .viewport = ccfg.viewport,
                        .gpu_scene = cfg.gpu_scene,
                        .rg_gpu_scene = cfg.rg_gpu_scene,
                        .occlusion_culling_mode = cfg.occlusion_culling_mode,
                        .hi_z = cfg.hi_z,
                    },

            });
}

void setup_opaque_pass(const PassCommonConfig &ccfg,
                       const OpaquePassConfig &cfg) {
  record_mesh_pass(
      ccfg, OpaqueMeshPassInfo{
                .base =
                    {
                        .pass_name = "opaque",
                        .color_attachments = {cfg.hdr},
                        .color_attachment_ops = {{
                            .load = rhi::RenderPassLoadOp::Discard,
                            .store = rhi::RenderPassStoreOp::Store,
                        }},
                        .color_attachment_names = {"hdr"},
                        .depth_attachment = cfg.depth_buffer,
                        .depth_attachment_ops =
                            rhi::DepthTargetOperations{
                                .load = rhi::RenderPassLoadOp::Load,
                                .store = rhi::RenderPassStoreOp::None,
                            },
                        .depth_attachment_name = "depth-buffer",
                        .camera = ccfg.scene->get_camera(),
                        .viewport = ccfg.viewport,
                        .gpu_scene = cfg.gpu_scene,
                        .rg_gpu_scene = cfg.rg_gpu_scene,
                        .occlusion_culling_mode = cfg.occlusion_culling_mode,
                        .hi_z = cfg.hi_z,
                    },
                .ssao = cfg.ssao,
                .exposure = cfg.exposure,
            });
}

} // namespace ren
