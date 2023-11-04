#pragma once
#include "Support/Span.hpp"

namespace ren {

class RgBuilder;
struct Mesh;
struct MeshInstance;

namespace glsl {
struct Material;
struct DirLight;
} // namespace glsl

void setup_upload_pass(RgBuilder &rgb);

struct UploadPassData {
  Span<const glsl::Material> materials;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::DirLight> directional_lights;
};

} // namespace ren
