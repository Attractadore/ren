#include "MaterialPipelineCompiler.hpp"
#include "AssetLoader.hpp"
#include "Device.hpp"

#include <fmt/format.h>

std::size_t std::hash<ren::MaterialConfig>::operator()(
    ren::MaterialConfig const &cfg) const noexcept {
  return 0;
}

namespace ren {

auto MaterialPipelineCompiler::get_material_pipeline(
    const MaterialConfig &config) const -> Optional<GraphicsPipelineRef> {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }
  return None;
};

auto MaterialPipelineCompiler::compile_material_pipeline(
    Device &device, const AssetLoader &loader,
    const MaterialPipelineConfig &config) -> GraphicsPipelineRef {
  Vector<std::byte> buffer;
  loader.load_file("VertexShader.spv", buffer);
  auto vs = device.create_shader_module(buffer);
  loader.load_file("FragmentShader.spv", buffer);
  auto fs = device.create_shader_module(buffer);
  return m_pipelines[config.material] =
             GraphicsPipelineBuilder(device)
                 .set_layout(config.layout)
                 .add_vertex_shader(vs.get())
                 .add_fragment_shader(fs.get())
                 .add_render_target(config.rt_format)
                 .add_depth_target(config.depth_format)
                 .build();
}

} // namespace ren
