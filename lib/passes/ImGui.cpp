#if REN_IMGUI
#include "ImGui.hpp"
#include "../CommandRecorder.hpp"
#include "../ImGuiConfig.hpp"
#include "../Scene.hpp"
#include "../Swapchain.hpp"
#include "../glsl/ImGui.h"

namespace ren {

namespace {

struct ImGuiPassResources {
  ImGuiContext *ctx = nullptr;
  Handle<GraphicsPipeline> pipeline;
  glm::uvec2 viewport;
};

void run_imgui_pass(Renderer &renderer, const RgRuntime &rg,
                    RenderPass &render_pass, const ImGuiPassResources &rcs) {
  ren_ImGuiScope(rcs.ctx);

  const ImDrawData *draw_data = ImGui::GetDrawData();
  if (!draw_data->TotalVtxCount) {
    return;
  }

  auto [vertices, vertices_ptr, _] =
      rg.allocate<ImDrawVert>(draw_data->TotalVtxCount);
  auto [indices, indices_ptr, index_buffer] =
      rg.allocate<ImDrawIdx>(draw_data->TotalIdxCount);

  for (const ImDrawList *cmd_list : draw_data->CmdLists) {
    std::ranges::copy_n(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size,
                        vertices);
    vertices += cmd_list->VtxBuffer.Size;
    std::ranges::copy_n(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size,
                        indices);
    indices += cmd_list->IdxBuffer.Size;
  }

  render_pass.set_descriptor_heaps(rg.get_resource_descriptor_heap(), rg.get_sampler_descriptor_heap());

  render_pass.bind_graphics_pipeline(rcs.pipeline);

  render_pass.bind_index_buffer(index_buffer);

  glm::vec2 clip_offset = {draw_data->DisplayPos.x, draw_data->DisplayPos.y};
  glm::vec2 clip_scale = {draw_data->FramebufferScale.x,
                          draw_data->FramebufferScale.y};

  // Flip viewport
  glm::vec2 display_offset = {draw_data->DisplayPos.x,
                              draw_data->DisplayPos.y +
                                  draw_data->DisplaySize.y};
  glm::vec2 display_size = {draw_data->DisplaySize.x,
                            -draw_data->DisplaySize.y};
  glm::vec2 scale = 2.0f / display_size;
  glm::vec2 translate = glm::vec2(-1.0f) - display_offset * scale;
  glm::vec2 fb_size = rcs.viewport;

  usize vertex_offset = 0;
  usize index_offset = 0;
  for (const ImDrawList *cmd_list : draw_data->CmdLists) {
    for (const ImDrawCmd &cmd : cmd_list->CmdBuffer) {
      ren_assert(!cmd.UserCallback);

      glm::vec2 clip_min = {cmd.ClipRect.x, cmd.ClipRect.y};
      clip_min = (clip_min - clip_offset) * clip_scale;
      glm::vec2 clip_max = {cmd.ClipRect.z, cmd.ClipRect.w};
      clip_max = (clip_max - clip_offset) * clip_scale;

      clip_min = glm::max(clip_min, 0.0f);
      clip_max = glm::min(clip_max, fb_size);
      if (glm::any(glm::lessThanEqual(clip_max, clip_min))) {
        continue;
      }

      VkRect2D scissor = {
          .offset = {i32(clip_min.x), i32(clip_min.y)},
          .extent = {u32(clip_max.x - clip_min.x),
                     u32(clip_max.y - clip_min.y)},
      };
      render_pass.set_scissor_rects({scissor});

      glsl::SampledTexture2D texture((uintptr_t)cmd.TextureId);

      render_pass.set_push_constants(glsl::ImGuiArgs{
          .vertices = DevicePtr<glsl::ImGuiVertex>(vertices_ptr),
          .scale = scale,
          .translate = translate,
          .tex = texture,
      });

      render_pass.draw_indexed({
          .num_indices = cmd.ElemCount,
          .num_instances = 1,
          .first_index = u32(cmd.IdxOffset + index_offset),
          .vertex_offset = i32(cmd.VtxOffset + vertex_offset),
      });
    }
    index_offset += cmd_list->IdxBuffer.Size;
    vertex_offset += cmd_list->VtxBuffer.Size;
  }
}

} // namespace

} // namespace ren

void ren::setup_imgui_pass(const PassCommonConfig &ccfg,
                           const ImGuiPassConfig &cfg) {
  auto pass = ccfg.rgb->create_pass({.name = "imgui"});

  std::tie(*cfg.sdr, std::ignore) =
      pass.write_render_target("sdr-imgui", *cfg.sdr,
                               {
                                   .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                   .store = VK_ATTACHMENT_STORE_OP_STORE,
                               });

  ImGuiPassResources rcs = {
      .ctx = cfg.ctx,
      .pipeline = ccfg.pipelines->imgui_pass,
      .viewport = ccfg.swapchain->get_size(),
  };

  pass.set_render_pass_callback(
      [rcs](Renderer &renderer, const RgRuntime &rt, RenderPass &render_pass) {
        run_imgui_pass(renderer, rt, render_pass, rcs);
      });
}
#endif // REN_IMGUI
