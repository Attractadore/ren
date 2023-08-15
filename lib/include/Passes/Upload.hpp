#pragma once
#include "Support/Span.hpp"

namespace ren {

class RgBuilder;
struct MeshInst;

namespace glsl {
struct DirLight;
struct Material;
} // namespace glsl

void setup_upload_pass(RgBuilder &rgb);

struct UploadPassData {
  Span<const MeshInst> mesh_insts;
  Span<const glsl::DirLight> directional_lights;
  Span<const glsl::Material> materials;
};

} // namespace ren
