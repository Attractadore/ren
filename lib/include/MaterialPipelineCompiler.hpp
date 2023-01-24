#pragma once
#include "Def.hpp"
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/HashMap.hpp"
#include "Support/Optional.hpp"

namespace ren {

class AssetLoader;

enum class MaterialAlbedo {
  Const = REN_MATERIAL_ALBEDO_CONST,
  Vertex = REN_MATERIAL_ALBEDO_VERTEX,
};

struct MaterialConfig {
  MaterialAlbedo albedo;

  MaterialConfig(const MaterialDesc &desc)
      : albedo(static_cast<MaterialAlbedo>(desc.albedo_type)) {}
  auto operator<=>(const MaterialConfig &other) const = default;
};

struct MaterialPipelineConfig {
  MaterialConfig material;
  PipelineLayoutRef layout;
  VkFormat rt_format;
};

} // namespace ren

template <> struct std::hash<ren::MaterialConfig> {
  std::size_t operator()(ren::MaterialConfig const &cfg) const noexcept;
};

namespace ren {

class MaterialPipelineCompiler {
  Device *m_device;
  HashMap<MaterialConfig, GraphicsPipeline> m_pipelines;
  const AssetLoader *m_asset_loader;

public:
  MaterialPipelineCompiler(Device &device, const AssetLoader *asset_loader);

  auto get_material_pipeline(const MaterialConfig &config) const
      -> Optional<GraphicsPipelineRef>;

  auto compile_material_pipeline(const MaterialPipelineConfig &config)
      -> GraphicsPipelineRef;
};

} // namespace ren
