#include "Skybox.hpp"
#include "../Scene.hpp"
#include "Skybox.frag.hpp"
#include "Swapchain.hpp"

namespace ren {

void setup_skybox_pass(const PassCommonConfig &ccfg,
                       const SkyboxPassConfig &cfg) {
  RgBuilder &rgb = *ccfg.rgb;
  auto pass = rgb.create_pass({.name = "skybox"});
  std::tie(*cfg.hdr, std::ignore) =
      pass.write_render_target("hdr-skybox", *cfg.hdr,
                               {
                                   .load = rhi::RenderPassLoadOp::Load,
                                   .store = rhi::RenderPassStoreOp::Store,
                               });
  pass.read_depth_stencil_target(cfg.depth_buffer);
  const Camera &camera = ccfg.scene->get_camera();

  RgSkyboxArgs args = {
      .exposure = pass.read_texture(cfg.exposure),
      .env_luminance = ccfg.scene->env_luminance,
      .env_map = ccfg.scene->env_map,
      .inv_proj_view = glm::inverse(
          get_projection_view_matrix(camera, ccfg.swapchain->get_size())),
      .eye = camera.position,
  };
  pass.set_render_pass_callback(
      [args, pipeline = ccfg.pipelines->skybox_pass](
          Renderer &renderer, const RgRuntime &rg, RenderPass &rp) {
        rp.bind_graphics_pipeline(pipeline);
        rg.push_constants(rp, args);
        rp.draw({.num_vertices = glsl::NUM_SKYBOX_VERTICES});
      });
}

} // namespace ren
