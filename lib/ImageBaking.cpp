#include "ImageBaking.hpp"
#include "BakeDhrLut.comp.hpp"
#include "Baking.hpp"
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

auto create_ktx_texture(const TextureInfo &info) -> expected<ktxTexture *> {
  ktx_error_code_e err = KTX_SUCCESS;

  ktxTextureCreateInfo create_info = {
      .vkFormat = (u32)TinyImageFormat_ToVkFormat(info.format),
      .baseWidth = info.width,
      .baseHeight = info.height,
      .baseDepth = 1,
      .numDimensions = 2,
      .numLevels = 1,
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
  err =
      ktxTexture_SetImageFromMemory(ktx_texture, 0, 0, 0, (const u8 *)info.data,
                                    ktxTexture_GetImageSize(ktx_texture, 0));
  if (err) {
    ktxTexture_Destroy(ktx_texture);
    return std::unexpected(Error::Unknown);
  }

  return ktx_texture;
}

auto write_ktx_to_memory(const DirectX::ScratchImage &mip_chain)
    -> expected<std::tuple<void *, size_t>> {
  ren_try(ktxTexture * ktx_texture, create_ktx_texture(mip_chain));
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

auto write_ktx_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
  ren_try(ktxTexture * ktx_texture, create_ktx_texture(info));
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

} // namespace

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

auto bake_color_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
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

auto bake_normal_map_to_memory(const TextureInfo &info)
    -> expected<std::tuple<void *, size_t>> {
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
    -> expected<std::tuple<void *, size_t>> {
  ren_try(DirectX::ScratchImage mip_chain,
          bake_orm_map(roughness_metallic_info, occlusion_info));
  return write_ktx_to_memory(mip_chain);
}

auto bake_dhr_lut(IBaker *baker) -> expected<TextureInfo> {
  ren_assert(baker);

  if (!baker->pipelines.dhr_lut) {
    ren_try(
        baker->pipelines.dhr_lut,
        load_compute_pipeline(baker->arena, BakeDhrLutCS, "Compute DHR LUT"));
  }

  RgBuilder rgb(baker->rg, *baker->renderer, baker->bake_descriptor_allocator);

  constexpr usize DHR_LUT_SIZE = 128;
  constexpr TinyImageFormat DHR_LUT_FORMAT = TinyImageFormat_R16G16_UNORM;
  usize DHR_LUT_PIXEL_SIZE = TinyImageFormat_BitSizeOfBlock(DHR_LUT_FORMAT) / 8;
  usize DHR_LUT_BYTE_SIZE = DHR_LUT_SIZE * DHR_LUT_SIZE * DHR_LUT_PIXEL_SIZE;

  RgTextureId dhr_lut = baker->rg.create_texture({
      .name = "dhr-lut",
      .format = DHR_LUT_FORMAT,
      .width = DHR_LUT_SIZE,
      .height = DHR_LUT_SIZE,
  });
  {
    auto pass = rgb.create_pass({.name = "compute-dhr-lut"});
    RgComputeDHRLutArgs args = {
        .lut = pass.write_texture("dhr-lut", &dhr_lut),
    };
    pass.dispatch_grid_2d(baker->pipelines.dhr_lut, args,
                          {DHR_LUT_SIZE, DHR_LUT_SIZE});
  }

  ren_try(BufferSlice readback, baker->bake_arena.create_buffer({
                                    .name = "DHR LUT readback buffer",
                                    .heap = rhi::MemoryHeap::Readback,
                                    .size = DHR_LUT_BYTE_SIZE,
                                }));
  RgUntypedBufferId dhr_lut_readback =
      rgb.create_buffer("dhr-lut-readback", readback);
  rgb.copy_texture_to_buffer(dhr_lut, &dhr_lut_readback);

  ren_try(RenderGraph rg, rgb.build({}));
  ren_try_to(rg.execute({.gfx_cmd_pool = baker->cmd_pool}));
  baker->renderer->wait_idle();

  return TextureInfo{
      .format = DHR_LUT_FORMAT,
      .width = DHR_LUT_SIZE,
      .height = DHR_LUT_SIZE,
      .data = baker->renderer->map_buffer(readback),
  };
}

auto bake_dhr_lut_to_memory(IBaker *baker)
    -> expected<std::tuple<void *, size_t>> {
  ren_try(TextureInfo image, bake_dhr_lut(baker));
  ren_try(auto blob, write_ktx_to_memory(image));
  reset_baker(*baker);
  return blob;
}

} // namespace ren
