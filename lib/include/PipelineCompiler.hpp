#pragma once
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/HashMap.hpp"
#include "Support/Vector.hpp"
#include "ren/ren.h"

#include <boost/functional/hash.hpp>

#include <span>

namespace ren {

enum class MaterialAlbedo {
  Const = REN_MATERIAL_ALBEDO_CONST,
  Vertex = REN_MATERIAL_ALBEDO_VERTEX,
};

struct MaterialConfig {
  Format rt_format;
  MaterialAlbedo albedo;

  auto operator<=>(const MaterialConfig &other) const = default;
};

} // namespace ren

template <> struct std::hash<ren::MaterialConfig> {
  std::size_t operator()(ren::MaterialConfig const &cfg) const noexcept {
    std::size_t seed = 0;
    boost::hash_combine(seed, cfg.albedo);
    return seed;
  }
};

namespace ren {

class PipelineCompiler {
  HashMap<MaterialConfig, Pipeline> m_pipelines;
  const char *m_blob_suffix;
  Vector<std::byte> m_vs_code;
  Vector<std::byte> m_fs_code;

protected:
  struct PipelineConfig {
    Format rt_format;
    std::span<const std::byte> vs_code;
    std::span<const std::byte> fs_code;
  };

  virtual Pipeline compile_pipeline(const PipelineConfig &config) = 0;

public:
  PipelineCompiler(const char *blob_suffix);
  virtual ~PipelineCompiler() = default;

  PipelineRef get_material_pipeline(const MaterialConfig &config);
};

} // namespace ren
