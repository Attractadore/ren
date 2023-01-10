#pragma once
#include "AssetLoader.hpp"
#include "Def.hpp"
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/HashMap.hpp"
#include "Support/Optional.hpp"
#include "Support/Vector.hpp"
#include "hlsl/interface.hpp"

#include <span>

namespace ren {

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

template <hlsl::VertexFetch VF> struct VertexFetch {
  static constexpr auto type = VF;
};

template <> struct VertexFetch<hlsl::VertexFetch::Attribute> {
  static constexpr auto type = hlsl::VertexFetch::Attribute;
  const HashMap<std::string_view, Format> *semantic_formats;
};

struct MaterialPipelineConfig {
  MaterialConfig material;
  PipelineSignatureRef signature;
  std::variant<VertexFetch<hlsl::VertexFetch::Physical>,
               VertexFetch<hlsl::VertexFetch::Logical>,
               VertexFetch<hlsl::VertexFetch::Attribute>>
      vertex_fetch;
  Format rt_format;
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
