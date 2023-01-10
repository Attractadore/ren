#pragma once
#include "Descriptors.hpp"
#include "Mesh.hpp"
#include "Pipeline.hpp"

namespace ren {

struct Material {
  GraphicsPipelineRef pipeline;
  unsigned index;
  StaticVector<MeshAttribute, MESH_ATTRIBUTE_COUNT> bindings;
};

} // namespace ren
