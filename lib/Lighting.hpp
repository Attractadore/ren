#pragma once
#include "glsl/Lighting.h"

namespace ren {

using DirectionalLight = glsl::DirectionalLight;

constexpr u32 ENV_LIGHTING_PACKAGE_MAGIC =
    ('l' << 24) | ('n' << 16) | ('e' << 8) | 'r';
constexpr u32 ENV_LIGHTING_PACKAGE_VERSION = 0;

struct EnvironmentPackageHeader {
  u32 magic = ENV_LIGHTING_PACKAGE_MAGIC;
  u32 version = ENV_LIGHTING_PACKAGE_VERSION;
  u64 num_sgs = 0;
  u64 sgs_offset = 0;
  u64 ktx_size = 0;
  u64 ktx_offset = 0;
};

} // namespace ren
