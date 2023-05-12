#pragma once
#include "Formats.hpp"
#include "Pipeline.hpp"
#include "Support/Enum.hpp"
#include "Support/HashMap.hpp"
#include "Support/Optional.hpp"
#include "ren/ren.h"

namespace ren {

class AssetLoader;

struct MaterialConfig {
public:
  MaterialConfig(const RenMaterialDesc &desc) {}

  auto operator<=>(const MaterialConfig &other) const = default;
};

struct MaterialPipelineConfig {
  MaterialConfig material;
  Handle<PipelineLayout> layout;
  VkFormat rt_format;
  VkFormat depth_format;
};

} // namespace ren

template <> struct std::hash<ren::MaterialConfig> {
  std::size_t operator()(ren::MaterialConfig const &cfg) const noexcept;
};

namespace ren {

class MaterialPipelineCompiler {
  HashMap<MaterialConfig, Handle<GraphicsPipeline>> m_pipelines;

public:
  auto get_material_pipeline(const MaterialConfig &config) const
      -> Optional<Handle<GraphicsPipeline>>;

  auto compile_material_pipeline(ResourceArena &arena,
                                 const AssetLoader &loader,
                                 const MaterialPipelineConfig &config)
      -> Handle<GraphicsPipeline>;
};

} // namespace ren
