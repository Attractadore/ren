#include "PipelineCompiler.hpp"
#include "Config.hpp"
#include "Device.hpp"

#include <boost/container_hash/hash.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>

std::size_t std::hash<ren::MaterialConfig>::operator()(
    ren::MaterialConfig const &cfg) const noexcept {
  std::size_t seed = 0;
  boost::hash_combine(seed, cfg.albedo);
  return seed;
}

namespace ren {

namespace {

void load_shader_code(const char *path, Vector<std::byte> &code) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error{fmt::format("Failed read shader from {0}", path)};
  }
  code.resize(std::filesystem::file_size(path));
  file.read(reinterpret_cast<char *>(code.data()), code.size());
}

const char *get_albedo_str(MaterialAlbedo albedo) {
  switch (albedo) {
    using enum MaterialAlbedo;
  case Const:
    return "CONST_COLOR";
  case Vertex:
    return "VERTEX_COLOR";
  }
}

std::string get_vertex_shader_path(const MaterialConfig &config,
                                   std::string_view blob_suffix) {
  return fmt::format("{0}/VertexShader_{1}{2}", c_assets_dir,
                     get_albedo_str(config.albedo), blob_suffix);
}

std::string get_fragment_shader_path(const MaterialConfig &config,
                                     std::string_view blob_suffix) {
  return fmt::format("{0}/FragmentShader_{1}{2}", c_assets_dir,
                     get_albedo_str(config.albedo), blob_suffix);
}

struct PipelineConfig {
  PipelineSignatureRef signature;
  std::span<const std::byte> vs_code;
  std::span<const std::byte> fs_code;
  Format rt_format;
};

Pipeline compile_pipeline(Device &device, const PipelineConfig &config) {
  return GraphicsPipelineBuilder(device)
      .set_signature(config.signature)
      .set_vertex_shader(config.vs_code)
      .set_fragment_shader(config.fs_code)
      .add_render_target(config.rt_format)
      .build();
}
} // namespace

MaterialPipelineCompiler::MaterialPipelineCompiler(Device &device,
                                                   PipelineSignature signature)
    : m_device(&device), m_signature(std::move(signature)) {}

auto MaterialPipelineCompiler::get_material_pipeline(
    const MaterialConfig &config, Format rt_format) -> const Pipeline & {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }

  auto blob_suffix = m_device->get_shader_blob_suffix();

  auto vs = get_vertex_shader_path(config, blob_suffix);
  auto fs = get_fragment_shader_path(config, blob_suffix);
  load_shader_code(vs.c_str(), m_vs_code);
  load_shader_code(fs.c_str(), m_fs_code);

  PipelineConfig pipeline_config = {
      .signature = m_signature,
      .vs_code = m_vs_code,
      .fs_code = m_fs_code,
      .rt_format = rt_format,
  };

  return m_pipelines[config] = compile_pipeline(*m_device, pipeline_config);
}
} // namespace ren
