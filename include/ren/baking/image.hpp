#pragma once
#include "../core/FileSystem.hpp"
#include "../ren.hpp"
#include "../tiny_imageformat.h"

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

IoResult<void> bake_color_map_to_file(const TextureInfo &info, File file);

Blob bake_color_map_to_memory(const TextureInfo &info);

IoResult<void> bake_normal_map_to_file(const TextureInfo &info, File file);

Blob bake_normal_map_to_memory(const TextureInfo &info);

IoResult<void> bake_orm_map_to_file(const TextureInfo &roughness_metallic_info,
                                    const TextureInfo &occlusion_info,
                                    File file);

Blob bake_orm_map_to_memory(const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info = {});

} // namespace ren
