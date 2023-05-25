#include "MaterialPipelineCompiler.hpp"
#include "Device.hpp"
#include "ResourceArena.hpp"

#include "FragmentShader.h"
#include "VertexShader.h"

#include <fmt/format.h>

std::size_t std::hash<ren::MaterialConfig>::operator()(
    ren::MaterialConfig const &cfg) const noexcept {
  return 0;
}

namespace ren {

auto MaterialPipelineCompiler::get_material_pipeline(
    const MaterialConfig &config) const -> Optional<Handle<GraphicsPipeline>> {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }
  return None;
};

auto MaterialPipelineCompiler::compile_material_pipeline(
    ResourceArena &arena, const MaterialPipelineConfig &config)
    -> Handle<GraphicsPipeline> {
  std::array color_attachments = {ColorAttachmentInfo{
      .format = config.rt_format,
  }};
  return m_pipelines[config.material] = arena.create_graphics_pipeline({
             .layout = config.layout,
             .vertex_shader =
                 {
                     .code = std::as_bytes(
                         std::span(VertexShader, VertexShader_count)),
                 },
             .fragment_shader =
                 ShaderInfo{
                     .code = std::as_bytes(
                         std::span(FragmentShader, FragmentShader_count)),
                 },
             .depth_test =
                 DepthTestInfo{
                     .format = config.depth_format,
                 },
             .color_attachments = color_attachments,
         });
}

} // namespace ren
