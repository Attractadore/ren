#pragma once
#include "core/Result.hpp"
#include "core/Vector.hpp"
#include "ren/baking/baking.hpp"
#include "ren/baking/image.hpp"

namespace DirectX {

struct Image;
struct TexMetadata;
struct ScratchImage;

} // namespace DirectX

namespace ren {

auto bake_so_lut_to_memory(IBaker *baker, bool compress = true)
    -> Result<Blob, Error>;

auto bake_ibl_to_memory(IBaker *baker, const TextureInfo &info,
                        bool compress = true) -> Result<Blob, Error>;

auto to_dxtex_image(const TextureInfo &info) -> DirectX::Image;

auto to_dxtex_images(const TextureInfo &info, Vector<DirectX::Image> &images)
    -> DirectX::TexMetadata;

auto write_ktx_to_memory(const DirectX::ScratchImage &mip_chain)
    -> expected<Blob>;

auto write_ktx_to_memory(const TextureInfo &info) -> expected<Blob>;

} // namespace ren
