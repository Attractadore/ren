#include "MaterialPipelineCompiler.hpp"
#include "AssetLoader.hpp"
#include "Device.hpp"
#include "Material.hpp"

#include <boost/container_hash/hash.hpp>
#include <fmt/format.h>

std::size_t std::hash<ren::MaterialConfig>::operator()(
    ren::MaterialConfig const &cfg) const noexcept {
  std::size_t seed = 0;
  boost::hash_combine(seed, cfg.albedo);
  return seed;
}

namespace ren {

namespace {

std::string_view get_albedo_str(MaterialAlbedo albedo) {
  switch (albedo) {
    using enum MaterialAlbedo;
  case Const:
    return "ALBEDO_CONST";
  case Vertex:
    return "ALBEDO_VERTEX";
  }
}

} // namespace

MaterialPipelineCompiler::MaterialPipelineCompiler(
    Device &device, const AssetLoader *asset_loader)
    : m_device(&device), m_asset_loader(asset_loader) {}

auto MaterialPipelineCompiler::get_material_pipeline(
    const MaterialConfig &config) const -> Optional<GraphicsPipelineRef> {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }
  return None;
};

auto MaterialPipelineCompiler::compile_material_pipeline(
    const MaterialPipelineConfig &config) -> GraphicsPipelineRef {
  auto get_shader_name = [&](std::string_view base_name) {
    return fmt::format("{0}_{1}.spv", base_name,
                       get_albedo_str(config.material.albedo));
  };

  Vector<std::byte> buffer;
  m_asset_loader->load_file(get_shader_name("VertexShader"), buffer);
  auto vs = m_device->create_shader_module(buffer);
  m_asset_loader->load_file(get_shader_name("FragmentShader"), buffer);
  auto fs = m_device->create_shader_module(buffer);

  return m_pipelines[config.material] =
             GraphicsPipelineBuilder(*m_device)
                 .set_layout(config.layout)
                 .add_vertex_shader(vs.get())
                 .add_fragment_shader(fs.get())
                 .add_render_target(config.rt_format)
                 .add_depth_target(config.depth_format)
                 .build();
}

} // namespace ren
