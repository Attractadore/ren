#include "ImageBaking.hpp"
#include "Baking.hpp"
#include "core/Errors.hpp"
#include "core/Math.hpp"
#include "core/Result.hpp"
#include "core/StdDef.hpp"
#include "core/Views.hpp"
#include "ren/baking/image.hpp"

#include "BakeDhrLut.comp.hpp"
#include "BakeIrradianceMap.comp.hpp"
#include "BakeReflectionMap.comp.hpp"
#include "BakeSpecularMap.comp.hpp"

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
    glm::uvec3 size =
        get_mip_size({mdata.width, mdata.height, mdata.depth}, mip);
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
      const DirectX::Image &image = *mip_chain.GetImage(mip, face, 0);
      if (ktx_texture->baseDepth > 1) {
        ren_assert(image.pixels + image.slicePitch ==
                   mip_chain.GetImage(mip, face, 1)->pixels);
      }
      ren_assert(image.rowPitch == ktxTexture_GetRowPitch(ktx_texture, mip));
      err = ktxTexture_SetImageFromMemory(
          ktx_texture, mip, 0, face, image.pixels,
          ktx_texture->baseDepth * image.slicePitch);
      if (err) {
        ktxTexture_Destroy(ktx_texture);
        return std::unexpected(Error::Unknown);
      }
    }
  }

  return ktx_texture2;
}

auto create_ktx_texture(const TextureInfo &info) -> expected<ktxTexture *> {
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

  return ktx_texture;
}

auto write_ktx_to_memory(const DirectX::ScratchImage &mip_chain)
    -> expected<std::tuple<void *, size_t>> {
  ren_try(ktxTexture2 * ktx_texture2, create_ktx_texture(mip_chain));
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

auto write_ktx_to_memory(const TextureInfo &info) -> expected<Blob> {
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
    ren_try(baker->pipelines.dhr_lut,
            load_compute_pipeline(baker->session_arena, BakeDhrLutCS,
                                  "Compute DHR LUT"));
  }

  RgBuilder rgb(baker->rg, *baker->renderer, baker->descriptor_allocator);

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

  ren_try(BufferSlice readback, baker->arena.create_buffer({
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

auto bake_dhr_lut_to_memory(IBaker *baker) -> Result<Blob, Error> {
  ren_try(TextureInfo image, bake_dhr_lut(baker));
  ren_try(auto blob, write_ktx_to_memory(image));
  reset_baker(*baker);
  return blob;
}

auto bake_ibl(IBaker *baker, const TextureInfo &info, bool compress)
    -> Result<DirectX::ScratchImage, Error> {
  HRESULT hres = S_OK;

  if (!baker->pipelines.reflection_map) {
    ren_try(baker->pipelines.reflection_map,
            load_compute_pipeline(baker->session_arena, BakeReflectionMapCS,
                                  "Bake reflection environment map"));
  }
  if (!baker->pipelines.specular_map) {
    ren_try(baker->pipelines.specular_map,
            load_compute_pipeline(baker->session_arena, BakeSpecularMapCS,
                                  "Bake specular environment map"));
  }
  if (!baker->pipelines.irradiance_map) {
    ren_try(baker->pipelines.irradiance_map,
            load_compute_pipeline(baker->session_arena, BakeIrradianceMapCS,
                                  "Bake irradiance environment map"));
  }
  const DirectX::Image &src_image = to_dxtex_image(info);
  DirectX::ScratchImage mip_chain;
  hres = mip_chain.Initialize2D(src_image.format, src_image.width,
                                src_image.height, 1, 0);
  if (FAILED(hres)) {
    return fail(hres);
  }
  std::memcpy(mip_chain.GetImages()[0].pixels, src_image.pixels,
              src_image.slicePitch);
  for (u32 mip : range<u32>(1, mip_chain.GetMetadata().mipLevels)) {
    // Preserve flux: L' * dA' = L1 * dA1 + L2 * dA2 + L3 * dA3 + L4 * dA4.
    // dA = dPhi * dTheta * sin(Theta)

    const DirectX::Image &src_image = *mip_chain.GetImage(mip - 1, 0, 0);
    const DirectX::Image &dst_image = *mip_chain.GetImage(mip, 0, 0);
    const glm::vec4 *src_pixels = (const glm::vec4 *)src_image.pixels;
    glm::vec4 *dst_pixels = (glm::vec4 *)dst_image.pixels;

    float d_theta_src = std::numbers::pi / src_image.height;
    float d_theta_dst = std::numbers::pi / dst_image.height;

    for (usize y : range(dst_image.height)) {
      usize y12 = 2 * y;
      usize y34 = std::min<usize>(2 * y + 1, src_image.height - 1);
      float sin_theta12 = glm::sin((y12 + 0.5f) * d_theta_src);
      float sin_theta34 = glm::sin((y34 + 0.5f) * d_theta_src);
      float sin_theta = glm::sin((y + 0.5f) * d_theta_dst);
      for (usize x : range(dst_image.width)) {
        usize x13 = 2 * x;
        usize x24 = std::min<usize>(2 * x + 1, src_image.width - 1);

        glm::vec3 L1 = src_pixels[src_image.width * y12 + x13];
        glm::vec3 L2 = src_pixels[src_image.width * y12 + x24];
        glm::vec3 L3 = src_pixels[src_image.width * y34 + x13];
        glm::vec3 L4 = src_pixels[src_image.width * y34 + x24];

        glm::vec3 L = ((L1 + L2) * sin_theta12 + (L3 + L4) * sin_theta34) /
                      4.0f / sin_theta;

        dst_pixels[dst_image.width * y + x] = glm::vec4(L, 1.0f);
      }
    }
  }

  ren_try(ktxTexture2 * ktx_texture2, create_ktx_texture(mip_chain));
  ren_try(Handle<Texture> env_map,
          baker->uploader.create_texture(baker->arena, baker->upload_allocator,
                                         ktx_texture2));
  ktxTexture_Destroy(ktxTexture(ktx_texture2));
  ren_try_to(baker->uploader.upload(*baker->renderer, baker->cmd_pool));

  constexpr TinyImageFormat CUBE_MAP_FORMAT =
      TinyImageFormat_R32G32B32A32_SFLOAT;
  constexpr usize CUBE_MAP_SIZE = 512;
  constexpr usize IRRADIANCE_SIZE = 32;
  constexpr usize NUM_CUBE_MAP_MIPS =
      ilog2(CUBE_MAP_SIZE / IRRADIANCE_SIZE) + 1;

  RgBuilder rgb(baker->rg, *baker->renderer, baker->descriptor_allocator);

  RgTextureId cube_map = baker->rg.create_texture({
      .name = "cube-map",
      .format = CUBE_MAP_FORMAT,
      .width = CUBE_MAP_SIZE,
      .height = CUBE_MAP_SIZE,
      .cube_map = true,
      .num_mips = NUM_CUBE_MAP_MIPS,
  });
  {
    auto pass = rgb.create_pass({"filter-cube-map"});

    struct {
      glsl::SampledTexture2D equirectangular_map;
      RgTextureToken cube_map;
    } args;

    ren_try(args.equirectangular_map,
            baker->descriptor_allocator
                .allocate_sampled_texture<glsl::SampledTexture2D>(
                    *baker->renderer, SrvDesc{env_map},
                    {
                        .mag_filter = rhi::Filter::Linear,
                        .min_filter = rhi::Filter::Linear,
                        .mipmap_mode = rhi::SamplerMipmapMode::Linear,
                        .address_mode_u = rhi::SamplerAddressMode::Repeat,
                        .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
                        .max_anisotropy = 16.0f,
                    }));
    args.cube_map = pass.write_texture("cube-map", &cube_map,
                                       rhi::CS_UNORDERED_ACCESS_IMAGE);

    pass.set_callback(
        [args, pipelines = &baker->pipelines](Renderer &, const RgRuntime &rg,
                                              CommandRecorder &cmd) {
          cmd.bind_compute_pipeline(pipelines->reflection_map);
          cmd.push_constants(glsl::BakeReflectionMapArgs{
              .equirectangular_map = args.equirectangular_map,
              .reflectance_map =
                  (glsl::StorageTextureCube)rg.get_storage_texture_descriptor(
                      args.cube_map, 0),
          });
          cmd.dispatch_grid_3d({CUBE_MAP_SIZE, CUBE_MAP_SIZE, 6});

          cmd.bind_compute_pipeline(pipelines->specular_map);
          for (u32 mip : range<u32>(1, NUM_CUBE_MAP_MIPS - 1)) {
            cmd.push_constants(glsl::BakeSpecularMapArgs{
                .equirectangular_map = args.equirectangular_map,
                .specular_map =
                    (glsl::StorageTextureCube)rg.get_storage_texture_descriptor(
                        args.cube_map, mip),
                .roughness = float(mip) / (NUM_CUBE_MAP_MIPS - 2),
            });
            u32 size = CUBE_MAP_SIZE >> mip;
            cmd.dispatch_grid_3d({size, size, 6});
          }

          cmd.bind_compute_pipeline(pipelines->irradiance_map);
          {
            cmd.push_constants(glsl::BakeIrradianceMapArgs{
                .equirectangular_map = args.equirectangular_map,
                .irradiance_map =
                    (glsl::StorageTextureCube)rg.get_storage_texture_descriptor(
                        args.cube_map, NUM_CUBE_MAP_MIPS - 1),
            });
            u32 size = CUBE_MAP_SIZE >> (NUM_CUBE_MAP_MIPS - 1);
            cmd.dispatch_grid_3d({size, size, 6});
          }
        });
  }

  ren_try(BufferView readback,
          baker->arena.create_buffer({
              .heap = rhi::MemoryHeap::Readback,
              .size = get_mip_chain_byte_size(CUBE_MAP_FORMAT,
                                              {CUBE_MAP_SIZE, CUBE_MAP_SIZE, 1},
                                              6, 0, NUM_CUBE_MAP_MIPS),
          }));
  RgUntypedBufferId cube_map_readback =
      rgb.create_buffer("cube-map-readback", readback);
  rgb.copy_texture_to_buffer(cube_map, &cube_map_readback);

  ren_try(RenderGraph rg, rgb.build({}));
  ren_try_to(rg.execute({.gfx_cmd_pool = baker->cmd_pool}));
  baker->renderer->wait_idle();

  Vector<DirectX::Image> images;
  DirectX::TexMetadata mdata = to_dxtex_images(
      {
          .format = CUBE_MAP_FORMAT,
          .width = CUBE_MAP_SIZE,
          .height = CUBE_MAP_SIZE,
          .cube_map = true,
          .num_mips = NUM_CUBE_MAP_MIPS,
          .data = baker->renderer->map_buffer(readback),
      },
      images);

  DirectX::ScratchImage compressed;
  if (compress) {
    fmt::println("Compress environment map");
    hres = DirectX::Compress(images.data(), images.size(), mdata,
                             DXGI_FORMAT_BC6H_UF16,
                             DirectX::TEX_COMPRESS_PARALLEL, 0.0f, compressed);
  } else {
    hres = DirectX::Convert(images.data(), images.size(), mdata,
                            DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
                            DirectX::TEX_FILTER_DEFAULT, 0.0f, compressed);
  }
  if (FAILED(hres)) {
    return fail(hres);
  }

  return compressed;
}

auto bake_ibl_to_memory(IBaker *baker, const TextureInfo &info, bool compress)
    -> Result<Blob, Error> {
  ren_try(DirectX::ScratchImage image, bake_ibl(baker, info, compress));
  ren_try(auto blob, write_ktx_to_memory(image));
  reset_baker(*baker);
  auto [blob_data, blob_size] = blob;
  return {{blob_data, blob_size}};
}

} // namespace ren
