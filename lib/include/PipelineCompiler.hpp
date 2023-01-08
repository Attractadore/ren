#pragma once
#include "AssetLoader.hpp"
#include "Def.hpp"
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/HashMap.hpp"
#include "Support/Vector.hpp"

#include <span>

namespace ren {

enum class MaterialAlbedo {
  Const = REN_MATERIAL_ALBEDO_CONST,
  Vertex = REN_MATERIAL_ALBEDO_VERTEX,
};

struct MaterialConfig {
  MaterialAlbedo albedo;

  auto operator<=>(const MaterialConfig &other) const = default;
};

} // namespace ren

template <> struct std::hash<ren::MaterialConfig> {
  std::size_t operator()(ren::MaterialConfig const &cfg) const noexcept;
};

namespace ren {

class MaterialPipelineCompiler {
  Device *m_device;
  PipelineSignature m_signature;
  HashMap<MaterialConfig, Pipeline> m_pipelines;
  const AssetLoader *m_asset_loader;

public:
  MaterialPipelineCompiler(Device &device, PipelineSignature signature,
                           const AssetLoader *asset_loader);

  auto get_signature() const -> const PipelineSignature & {
    return m_signature;
  }
  auto get_material_pipeline(const MaterialConfig &config, Format rt_format)
      -> const Pipeline &;
};

} // namespace ren
