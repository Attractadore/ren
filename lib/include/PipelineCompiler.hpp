#pragma once
#include "Pipeline.hpp"
#include "ren/ren.h"

namespace ren {

enum class MaterialAlbedo {
  Const = REN_MATERIAL_ALBEDO_CONST,
  Vertex = REN_MATERIAL_ALBEDO_VERTEX,
};

struct MaterialConfig {
  MaterialAlbedo albedo;
};

class PipelineCompiler {
public:
  const PipelineRef &get_material_pipeline(const MaterialConfig &config);
};

} // namespace ren
