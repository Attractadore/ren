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

IoResult<void> bake_color_map_to_file(File file, const TextureInfo &info);

Blob bake_color_map_to_memory(NotNull<Arena *> arena, const TextureInfo &info);

IoResult<void> bake_normal_map_to_file(File file, const TextureInfo &info);

Blob bake_normal_map_to_memory(NotNull<Arena *> arena, const TextureInfo &info);

IoResult<void> bake_orm_map_to_file(File file,
                                    const TextureInfo &roughness_metallic_info,
                                    const TextureInfo &occlusion_info);

Blob bake_orm_map_to_memory(NotNull<Arena *> arena,
                            const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info = {});

} // namespace ren
