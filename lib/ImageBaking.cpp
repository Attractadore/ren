#include "core/Math.hpp"
#include "core/StdDef.hpp"
#include "core/Views.hpp"
#include "ren/baking/image.hpp"

#include <DirectXTex.h>
#include <ktx.h>

namespace ren {

auto bake_color_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  HRESULT hres = S_OK;
  ktx_error_code_e err = KTX_SUCCESS;

  u32 block_width = TinyImageFormat_WidthOfBlock(info.format);
  u32 block_height = TinyImageFormat_HeightOfBlock(info.format);
  u32 block_size = TinyImageFormat_BitSizeOfBlock(info.format) / 8;

  u32 num_blocks_x = ceil_div(info.width, block_width);
  u32 num_blocks_y = ceil_div(info.height, block_height);

  DirectX::ScratchImage mip_chain;
  hres = DirectX::GenerateMipMaps(
      {
          .width = info.width,
          .height = info.height,
          .format = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(info.format),
          .rowPitch = num_blocks_x * block_size,
          .slicePitch = num_blocks_x * num_blocks_y * block_size,
          .pixels = (u8 *)info.data,
      },
      DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }

  ktxTextureCreateInfo create_info = {
      .vkFormat = (u32)TinyImageFormat_ToVkFormat(info.format),
      .baseWidth = info.width,
      .baseHeight = info.height,
      .baseDepth = 1,
      .numDimensions = 2,
      .numLevels = (u32)mip_chain.GetMetadata().mipLevels,
      .numLayers = 1,
      .numFaces = 1,
      .isArray = false,
      .generateMipmaps = false,
  };

  ktxTexture2 *ktx_texture2 = nullptr;
  err = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE,
                           &ktx_texture2);
  if (err) {
    return std::unexpected(Error::Unknown);
  }
  ktxTexture *ktx_texture = ktxTexture(ktx_texture2);

  for (u32 mip : range(ktx_texture->numLevels)) {
    const DirectX::Image &image = *mip_chain.GetImage(mip, 0, 0);
    u32 num_blocks_x = ceil_div(image.width, block_width);
    u32 num_blocks_y = ceil_div(image.height, block_height);
    ren_assert(image.rowPitch == num_blocks_x * block_size);
    ren_assert(image.slicePitch == num_blocks_x * num_blocks_y * block_size);
    err = ktxTexture_SetImageFromMemory(ktx_texture, mip, 0, 0, image.pixels,
                                        image.slicePitch);
    if (err) {
      ktxTexture_Destroy(ktx_texture);
      return std::unexpected(Error::Unknown);
    }
  }
  unsigned char *buffer;
  size_t size;
  err = ktxTexture_WriteToMemory(ktx_texture, &buffer, &size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }

  return {{buffer, size}};
}

auto bake_normal_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  return bake_color_map_to_memory(info);
}

auto bake_metallic_roughness_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  return bake_color_map_to_memory(info);
}

} // namespace ren
