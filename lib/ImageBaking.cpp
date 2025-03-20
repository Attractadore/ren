#include "core/Errors.hpp"
#include "core/Math.hpp"
#include "core/Result.hpp"
#include "core/StdDef.hpp"
#include "core/Views.hpp"
#include "ren/baking/image.hpp"

#include <DirectXTex.h>
#include <ktx.h>

namespace ren {

namespace {

auto to_dxtex_image(const TextureInfo &info) -> DirectX::Image {
  u32 block_width = TinyImageFormat_WidthOfBlock(info.format);
  u32 block_height = TinyImageFormat_HeightOfBlock(info.format);
  u32 block_size = TinyImageFormat_BitSizeOfBlock(info.format) / 8;
  u32 num_blocks_x = ceil_div(info.width, block_width);
  u32 num_blocks_y = ceil_div(info.height, block_height);
  return {
      .width = info.width,
      .height = info.height,
      .format = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(info.format),
      .rowPitch = num_blocks_x * block_size,
      .slicePitch = num_blocks_x * num_blocks_y * block_size,
      .pixels = (u8 *)info.data,
  };
}

auto create_ktx_texture(const DirectX::ScratchImage &mip_chain)
    -> expected<ktxTexture *> {
  ktx_error_code_e err = KTX_SUCCESS;

  const DirectX::TexMetadata &mdata = mip_chain.GetMetadata();

  ktxTextureCreateInfo create_info = {
      .vkFormat =
          (u32)TinyImageFormat_ToVkFormat(TinyImageFormat_FromDXGI_FORMAT(
              (TinyImageFormat_DXGI_FORMAT)mdata.format)),
      .baseWidth = (u32)mdata.width,
      .baseHeight = (u32)mdata.height,
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
    ren_assert(image.rowPitch == ktxTexture_GetRowPitch(ktx_texture, mip));
    err = ktxTexture_SetImageFromMemory(ktx_texture, mip, 0, 0, image.pixels,
                                        image.slicePitch);
    if (err) {
      ktxTexture_Destroy(ktx_texture);
      return std::unexpected(Error::Unknown);
    }
  }

  return ktx_texture;
}

} // namespace

auto bake_color_map(const TextureInfo &info) -> expected<ktxTexture *> {
  HRESULT hres = S_OK;
  DirectX::ScratchImage mip_chain;
  hres = DirectX::GenerateMipMaps(to_dxtex_image(info),
                                  DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }
  return create_ktx_texture(mip_chain);
}

auto bake_color_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  ktx_error_code_e err = KTX_SUCCESS;
  ren_try(ktxTexture * ktx_texture, bake_color_map(info));
  unsigned char *buffer;
  size_t size;
  err = ktxTexture_WriteToMemory(ktx_texture, &buffer, &size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }

  return {{buffer, size}};
}

auto bake_normal_map(const TextureInfo &info) -> expected<ktxTexture *> {
  HRESULT hres = S_OK;
  DirectX::ScratchImage mip_chain;
  hres = DirectX::GenerateMipMaps(to_dxtex_image(info),
                                  DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }
  return create_ktx_texture(mip_chain);
}

auto bake_normal_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  ktx_error_code_e err = KTX_SUCCESS;
  ren_try(ktxTexture * ktx_texture, bake_normal_map(info));
  unsigned char *buffer;
  size_t size;
  err = ktxTexture_WriteToMemory(ktx_texture, &buffer, &size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }

  return {{buffer, size}};
}

auto bake_orm_map(const TextureInfo &roughness_metallic_info,
                  const TextureInfo &occlusion_info) -> expected<ktxTexture *> {
  HRESULT hres = S_OK;
  ktx_error_code_e err = KTX_SUCCESS;

  DirectX::Image src = to_dxtex_image(roughness_metallic_info);

  DirectX::ScratchImage merged_data;
  if (!occlusion_info.data) {
    hres = merged_data.InitializeFromImage(src);
    if (FAILED(hres)) {
      return std::unexpected(Error::Unknown);
    }
    src = merged_data.GetImages()[0];
    hres = DirectX::TransformImage(
        src,
        [](DirectX::XMVECTOR *out, const DirectX::XMVECTOR *in, size_t width,
           size_t) {
          for (usize y : range(width)) {
            out[y] = DirectX::XMVectorSetX(in[y], 1.0f);
          }
        },
        merged_data);
    if (FAILED(hres)) {
      return std::unexpected(Error::Unknown);
    }
  } else if (occlusion_info.data != roughness_metallic_info.data) {
    todo("Separate roughness-metallic and occlusions maps");
  }

  DirectX::ScratchImage mip_chain;
  hres =
      DirectX::GenerateMipMaps(src, DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }

  return create_ktx_texture(mip_chain);
}

auto bake_orm_map_to_memory(const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info)
    -> expected<std::tuple<void *, size_t>> {
  ktx_error_code_e err = KTX_SUCCESS;
  ren_try(ktxTexture * ktx_texture,
          bake_orm_map(roughness_metallic_info, occlusion_info));
  unsigned char *buffer;
  size_t size;
  err = ktxTexture_WriteToMemory(ktx_texture, &buffer, &size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }
  return {{buffer, size}};
}

} // namespace ren
