#pragma once
#include "../ren.hpp"
#include "../tiny_imageformat.h"
#include "baking.hpp"

namespace ren {

struct TextureInfo {
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  unsigned width = 0;
  unsigned height = 0;
  bool cube_map = false;
  unsigned num_mips = 1;
  const void *data = nullptr;
};

auto bake_color_map_to_file(const TextureInfo &info, FILE *out)
    -> expected<void>;

auto bake_color_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>>;

auto bake_normal_map_to_file(const TextureInfo &info, FILE *out)
    -> expected<void>;

auto bake_normal_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>>;

auto bake_orm_map_to_file(const TextureInfo &roughness_metallic_info,
                          const TextureInfo &occlusion_info, FILE *out)
    -> expected<void>;

auto bake_orm_map_to_memory(const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info = {})
    -> expected<std::tuple<void *, size_t>>;

} // namespace ren
