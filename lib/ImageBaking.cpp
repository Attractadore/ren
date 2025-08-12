#include "core/Errors.hpp"
#include "core/Math.hpp"
#include "core/Result.hpp"
#include "core/StdDef.hpp"
#include "core/Vector.hpp"
#include "core/Views.hpp"
#include "ren/baking/image.hpp"

#include <DirectXTex.h>
#include <ktx.h>

namespace ren {

namespace {

auto fail(HRESULT hres) -> Failure<Error> {
  ren_assert(hres != E_INVALIDARG);
  return Failure([&]() {
    switch (hres) {
    default:
      return Error::Unknown;
    }
  }());
}

} // namespace

auto to_dxtex_image(const TextureInfo &info) -> DirectX::Image {
  ren_assert(info.depth == 1 and not info.cube_map);
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

auto to_dxtex_images(const TextureInfo &info, Vector<DirectX::Image> &images)
    -> DirectX::TexMetadata {
  u32 num_faces = info.cube_map ? 6 : 1;
  DirectX::TexMetadata mdata = {
      .width = info.width,
      .height = info.height,
      .depth = info.depth,
      .arraySize = num_faces,
      .mipLevels = info.num_mips,
      .miscFlags = info.cube_map ? DirectX::TEX_MISC_TEXTURECUBE : 0,
      .format = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(info.format),
      .dimension = info.depth > 1 ? DirectX::TEX_DIMENSION_TEXTURE3D
                                  : DirectX::TEX_DIMENSION_TEXTURE2D,
  };
  images.resize(mdata.mipLevels * mdata.depth * mdata.arraySize);
  u8 *data = (u8 *)info.data;
  for (u32 mip : range(mdata.mipLevels)) {
    glm::uvec3 size = {mdata.width, mdata.height, mdata.depth};
    size = glm::max(size >> mip, glm::uvec3(1));
    usize row_pitch, slice_pitch;
    HRESULT hres = DirectX::ComputePitch(mdata.format, size.x, size.y,
                                         row_pitch, slice_pitch);
    ren_assert(SUCCEEDED(hres));
    for (u32 item : range(mdata.arraySize)) {
      for (u32 plane : range(mdata.depth)) {
        images[mdata.ComputeIndex(mip, item, plane)] = {
            .width = size.x,
            .height = size.y,
            .format = mdata.format,
            .rowPitch = row_pitch,
            .slicePitch = slice_pitch,
            .pixels = data,
        };
        data += slice_pitch;
      }
    }
  }
  return mdata;
};

auto create_ktx_texture(const DirectX::ScratchImage &mip_chain)
    -> expected<ktxTexture2 *> {
  ktx_error_code_e err = KTX_SUCCESS;

  const DirectX::TexMetadata &mdata = mip_chain.GetMetadata();

  u32 num_faces = (mdata.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) ? 6 : 1;
  u32 num_layers = (u32)mdata.arraySize / num_faces;
  ren_assert(num_layers == 1);

  ktxTextureCreateInfo create_info = {
      .vkFormat =
          (u32)TinyImageFormat_ToVkFormat(TinyImageFormat_FromDXGI_FORMAT(
              (TinyImageFormat_DXGI_FORMAT)mdata.format)),
      .baseWidth = (u32)mdata.width,
      .baseHeight = (u32)mdata.height,
      .baseDepth = (u32)mdata.depth,
      .numDimensions =
          (u32)(mdata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? 3 : 2),
      .numLevels = (u32)mdata.mipLevels,
      .numLayers = 1,
      .numFaces = num_faces,
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
    for (u32 face : range(num_faces)) {
      for (u32 plane : range(ktx_texture->baseDepth)) {
        const DirectX::Image &image = *mip_chain.GetImage(mip, face, plane);
        ren_assert(image.rowPitch == ktxTexture_GetRowPitch(ktx_texture, mip));
        err = ktxTexture_SetImageFromMemory(ktx_texture, mip, 0, face + plane,
                                            image.pixels, image.slicePitch);
        if (err) {
          ktxTexture_Destroy(ktx_texture);
          return std::unexpected(Error::Unknown);
        }
      }
    }
  }

  return ktx_texture2;
}

auto create_ktx_texture(const TextureInfo &info) -> expected<ktxTexture2 *> {
  ktx_error_code_e err = KTX_SUCCESS;

  ktxTextureCreateInfo create_info = {
      .vkFormat = (u32)TinyImageFormat_ToVkFormat(info.format),
      .baseWidth = info.width,
      .baseHeight = info.height,
      .baseDepth = info.depth,
      .numDimensions = (u32)(info.depth > 1 ? 3 : 2),
      .numLevels = info.num_mips,
      .numLayers = 1,
      .numFaces = u32(info.cube_map ? 6 : 1),
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

  const u8 *data = (const u8 *)info.data;
  for (u32 mip : range(info.num_mips)) {
    usize size = ktxTexture_GetImageSize(ktx_texture, mip) *
                 ktx_texture->numFaces * ktx_texture->numLayers *
                 ktx_texture->baseDepth;
    err = ktxTexture_SetImageFromMemory(ktx_texture, mip, 0,
                                        KTX_FACESLICE_WHOLE_LEVEL, data, size);
    if (err) {
      ktxTexture_Destroy(ktx_texture);
      return std::unexpected(Error::Unknown);
    }
    data += size;
  }

  return ktx_texture2;
}

auto write_ktx_to_memory(const DirectX::ScratchImage &mip_chain)
    -> expected<Blob> {
  ren_try(ktxTexture2 * ktx_texture2, create_ktx_texture(mip_chain));
  ktxTexture *ktx_texture = ktxTexture(ktx_texture2);
  Blob blob;
  ktx_error_code_e err =
      ktxTexture_WriteToMemory(ktx_texture, (u8 **)&blob.data, &blob.size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }
  return blob;
}

auto write_ktx_to_memory(const TextureInfo &info) -> expected<Blob> {
  ren_try(ktxTexture2 * ktx_texture2, create_ktx_texture(info));
  ktxTexture *ktx_texture = ktxTexture(ktx_texture2);
  u8 *blob_data;
  size_t blob_size;
  ktx_error_code_e err =
      ktxTexture_WriteToMemory(ktx_texture, &blob_data, &blob_size);
  ktxTexture_Destroy(ktx_texture);
  if (err) {
    return std::unexpected(Error::Unknown);
  }
  return {{blob_data, blob_size}};
}

auto bake_color_map(const TextureInfo &info)
    -> expected<DirectX::ScratchImage> {
  HRESULT hres = S_OK;
  DirectX::ScratchImage mip_chain;
  hres = DirectX::GenerateMipMaps(to_dxtex_image(info),
                                  DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }
  return mip_chain;
}

auto bake_color_map_to_memory(const TextureInfo &info) -> expected<Blob> {
  ren_try(DirectX::ScratchImage mip_chain, bake_color_map(info));
  return write_ktx_to_memory(mip_chain);
}

auto bake_normal_map(const TextureInfo &info)
    -> expected<DirectX::ScratchImage> {
  HRESULT hres = S_OK;
  DirectX::ScratchImage mip_chain;
  hres = DirectX::GenerateMipMaps(to_dxtex_image(info),
                                  DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }
  return mip_chain;
}

auto bake_normal_map_to_memory(const TextureInfo &info) -> expected<Blob> {
  ktx_error_code_e err = KTX_SUCCESS;
  ren_try(DirectX::ScratchImage mip_chain, bake_normal_map(info));
  return write_ktx_to_memory(mip_chain);
}

auto bake_orm_map(const TextureInfo &roughness_metallic_info,
                  const TextureInfo &occlusion_info)
    -> expected<DirectX::ScratchImage> {
  HRESULT hres = S_OK;
  ktx_error_code_e err = KTX_SUCCESS;

  DirectX::Image src = to_dxtex_image(roughness_metallic_info);

  DirectX::ScratchImage merged_data;
  if (!occlusion_info.data) {
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
    src = merged_data.GetImages()[0];
  } else if (occlusion_info.data != roughness_metallic_info.data) {
    todo("Separate roughness-metallic and occlusions maps");
  }

  DirectX::ScratchImage mip_chain;
  hres =
      DirectX::GenerateMipMaps(src, DirectX::TEX_FILTER_LINEAR, 0, mip_chain);
  if (FAILED(hres)) {
    return std::unexpected(Error::Unknown);
  }

  return mip_chain;
}

auto bake_orm_map_to_memory(const TextureInfo &roughness_metallic_info,
                            const TextureInfo &occlusion_info)
    -> expected<Blob> {
  ren_try(DirectX::ScratchImage mip_chain,
          bake_orm_map(roughness_metallic_info, occlusion_info));
  return write_ktx_to_memory(mip_chain);
}

} // namespace ren
