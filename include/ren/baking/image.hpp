#pragma once
#include "../ren.hpp"
#include "../tiny_imageformat.h"
#include "baking.hpp"

namespace ren {

struct TextureInfo {
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  unsigned width = 1;
  unsigned height = 1;
  unsigned depth = 1;
  bool cube_map = false;
  unsigned num_mips = 1;
  const void *data = nullptr;
};

auto bake_color_map_to_file(const TextureInfo &info, FILE *out)
    -> expected<void>;

auto bake_color_map_to_memory(const TextureInfo &info) -> expected<Blob>;

auto bake_normal_map_to_file(const TextureInfo &info, FILE *out)
    -> expected<void>;

auto bake_normal_map_to_memory(const TextureInfo &info) -> expected<Blob>;

auto bake_orm_map_to_file(const TextureInfo &roughness_metallic_info,
                          const TextureInfo &occlusion_info, FILE *out)
    -> expected<void>;

auto bake_orm_map_to_memory(const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info = {})
    -> expected<Blob>;

} // namespace ren
