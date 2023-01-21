#include "MaterialPipelineCompiler.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Formats.inl"
#include "Material.hpp"
#include "Support/Variant.hpp"
#include "Support/Views.hpp"

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

  auto get_shader_name = [&, blob_suffix = m_device->get_shader_blob_suffix()](
                             std::string_view base_name) {
    return fmt::format("{0}_{1}{2}", base_name,
                       get_albedo_str(config.material.albedo), blob_suffix);
  };

  Vector<std::byte> vs, fs;
  m_asset_loader->load_file(get_shader_name("VertexShader"), vs);
  m_asset_loader->load_file(get_shader_name("FragmentShader"), fs);

  Vector<VertexBinding> vertex_bindings;
  Vector<VertexAttribute> vertex_attributes;

  return m_pipelines[config.material] =
             GraphicsPipelineBuilder(*m_device)
                 .set_signature(config.signature)
                 .set_vertex_shader(vs)
                 .set_fragment_shader(fs)
                 .set_ia_vertex_bindings(std::move(vertex_bindings))
                 .set_ia_vertex_attributes(std::move(vertex_attributes))
                 .set_render_target(config.rt_format)
                 .build();
}
} // namespace ren
