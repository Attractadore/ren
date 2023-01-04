#include "PipelineCompiler.hpp"
#include "Config.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <fstream>

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
                                   const char *blob_suffix) {
  return fmt::format("{0}/VertexShader_{1}{2}", c_assets_dir,
                     get_albedo_str(config.albedo), blob_suffix);
}

std::string get_fragment_shader_path(const MaterialConfig &config,
                                     const char *blob_suffix) {
  return fmt::format("{0}/FragmentShader_{1}{2}", c_assets_dir,
                     get_albedo_str(config.albedo), blob_suffix);
}

} // namespace

PipelineCompiler::PipelineCompiler(const char *blob_suffix)
    : m_blob_suffix(blob_suffix) {}

PipelineRef
PipelineCompiler::get_material_pipeline(const MaterialConfig &config) {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }

  auto vs_path = get_vertex_shader_path(config, m_blob_suffix);
  auto fs_path = get_fragment_shader_path(config, m_blob_suffix);
  load_shader_code(vs_path.c_str(), m_vs_code);
  load_shader_code(fs_path.c_str(), m_fs_code);

  PipelineConfig pipeline_config = {
      .rt_format = config.rt_format,
      .vs_code = m_vs_code,
      .fs_code = m_fs_code,
  };

  return m_pipelines[config] = compile_pipeline(pipeline_config);
}
} // namespace ren
