#include "Baking.hpp"
#include "core/IO.hpp"
#include "core/Views.hpp"
#include "ren/baking/baking.hpp"
#include "ren/core/StdDef.hpp"

#include "BakeIrradianceMap.comp.hpp"
#include "BakeReflectionMap.comp.hpp"
#include "BakeSpecularMap.comp.hpp"

#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/base.h>
#include <fmt/std.h>
#include <ktx.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <DirectXTex.h>

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

auto bake_ibl(Baker *baker, const TextureInfo &info, bool compress)
    -> Result<DirectX::ScratchImage, Error> {
  HRESULT hres = S_OK;

  if (!baker->pipelines.reflection_map) {
    ren_try(baker->pipelines.reflection_map,
            load_compute_pipeline(baker->rcs_arena, BakeReflectionMapCS,
                                  "Bake reflection environment map"));
  }
  if (!baker->pipelines.specular_map) {
    ren_try(baker->pipelines.specular_map,
            load_compute_pipeline(baker->rcs_arena, BakeSpecularMapCS,
                                  "Bake specular environment map"));
  }
  if (!baker->pipelines.irradiance_map) {
    ren_try(baker->pipelines.irradiance_map,
            load_compute_pipeline(baker->rcs_arena, BakeIrradianceMapCS,
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

    float d_theta_src = sh::PI / src_image.height;
    float d_theta_dst = sh::PI / dst_image.height;

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
          baker->uploader.create_texture(
              &baker->frame_arena, baker->frame_rcs_arena,
              baker->upload_allocator, ktx_texture2));
  ktxTexture_Destroy(ktxTexture(ktx_texture2));
  ren_try_to(baker->uploader.upload(*baker->renderer, baker->cmd_pool));

  constexpr TinyImageFormat CUBE_MAP_FORMAT =
      TinyImageFormat_R32G32B32A32_SFLOAT;
  constexpr usize CUBE_MAP_SIZE = 512;
  constexpr usize IRRADIANCE_SIZE = 32;
  constexpr usize NUM_CUBE_MAP_MIPS =
      ilog2(CUBE_MAP_SIZE / IRRADIANCE_SIZE) + 1;

  RgBuilder rgb;
  rgb.init(&baker->frame_arena, &baker->rg, baker->renderer,
           &baker->frame_descriptor_allocator);

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
      sh::Handle<sh::Sampler2D> equirectangular_map;
      RgTextureToken cube_map;
    } args;

    args.equirectangular_map =
        baker->frame_descriptor_allocator
            .allocate_sampled_texture<sh::Sampler2D>(
                *baker->renderer, SrvDesc{env_map},
                {
                    .mag_filter = rhi::Filter::Linear,
                    .min_filter = rhi::Filter::Linear,
                    .mipmap_mode = rhi::SamplerMipmapMode::Linear,
                    .address_mode_u = rhi::SamplerAddressMode::Repeat,
                    .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
                    .max_anisotropy = 16.0f,
                });
    args.cube_map = pass.write_texture("cube-map", &cube_map,
                                       rhi::CS_UNORDERED_ACCESS_IMAGE);

    pass.set_callback(
        [args, pipelines = &baker->pipelines](Renderer &, const RgRuntime &rg,
                                              CommandRecorder &cmd) {
          cmd.bind_compute_pipeline(pipelines->reflection_map);
          cmd.push_constants(sh::BakeReflectionMapArgs{
              .equirectangular_map = args.equirectangular_map,
              .reflectance_map =
                  rg.get_storage_texture_descriptor<sh::RWTexture2DArray>(
                      args.cube_map, 0),
          });
          cmd.dispatch_grid_3d({CUBE_MAP_SIZE, CUBE_MAP_SIZE, 6});

          cmd.bind_compute_pipeline(pipelines->specular_map);
          for (u32 mip : range<u32>(1, NUM_CUBE_MAP_MIPS - 1)) {
            cmd.push_constants(sh::BakeSpecularMapArgs{
                .equirectangular_map = args.equirectangular_map,
                .specular_map =
                    rg.get_storage_texture_descriptor<sh::RWTexture2DArray>(
                        args.cube_map, mip),
                .roughness = float(mip) / (NUM_CUBE_MAP_MIPS - 2),
            });
            u32 size = CUBE_MAP_SIZE >> mip;
            cmd.dispatch_grid_3d({size, size, 6});
          }

          cmd.bind_compute_pipeline(pipelines->irradiance_map);
          {
            cmd.push_constants(sh::BakeIrradianceMapArgs{
                .equirectangular_map = args.equirectangular_map,
                .irradiance_map =
                    rg.get_storage_texture_descriptor<sh::RWTexture2DArray>(
                        args.cube_map, NUM_CUBE_MAP_MIPS - 1),
            });
            u32 size = CUBE_MAP_SIZE >> (NUM_CUBE_MAP_MIPS - 1);
            cmd.dispatch_grid_3d({size, size, 6});
          }
        });
  }

  ren_try(BufferView readback,
          baker->frame_rcs_arena.create_buffer({
              .heap = rhi::MemoryHeap::Readback,
              .size = get_mip_chain_byte_size(CUBE_MAP_FORMAT,
                                              {CUBE_MAP_SIZE, CUBE_MAP_SIZE, 1},
                                              6, 0, NUM_CUBE_MAP_MIPS),
          }));
  RgUntypedBufferId cube_map_readback =
      rgb.create_buffer("cube-map-readback", readback);
  rgb.copy_texture_to_buffer(cube_map, &cube_map_readback);

  ren_try(RenderGraph rg, rgb.build({}));
  ren_try_to(execute(rg, {.gfx_cmd_pool = baker->cmd_pool}));
  baker->renderer->wait_idle();

  ScratchArena scratch;
  Span<DirectX::Image> images;
  DirectX::TexMetadata mdata =
      to_dxtex_images(scratch,
                      {
                          .format = CUBE_MAP_FORMAT,
                          .width = CUBE_MAP_SIZE,
                          .height = CUBE_MAP_SIZE,
                          .cube_map = true,
                          .num_mips = NUM_CUBE_MAP_MIPS,
                          .data = baker->renderer->map_buffer(readback),
                      },
                      &images);

  DirectX::ScratchImage compressed;

#if !_OPENMP
  if (compress) {
    fmt::println("OpenMP not available, skip environment map compression");
    compress = false;
  }
#endif
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

auto bake_ibl_to_memory(Baker *baker, const TextureInfo &info, bool compress)
    -> Result<Blob, Error> {
  ren_try(DirectX::ScratchImage image, bake_ibl(baker, info, compress));
  ren_try(auto blob, write_ktx_to_memory(image));
  reset_baker(baker);
  auto [blob_data, blob_size] = blob;
  return {{blob_data, blob_size}};
}

} // namespace ren

using namespace ren;

int main(int argc, const char *argv[]) {
  cxxopts::Options options("bake-ibl", "Bake IBL for ren");
  // clang-format off
  options.add_options()
    ("in", "input HDR environment map path", cxxopts::value<fs::path>())
    ("out", "output filtered HDR environment cube map path", cxxopts::value<fs::path>())
    ("no-compress", "don't compress")
    ("h,help", "show this message")
  ;
  // clang-format on
  options.parse_positional({"in", "out"});
  options.positional_help("in out");
  cxxopts::ParseResult result = options.parse(argc, argv);
  if (result.count("help") or not result.count("out")) {
    fmt::println("{}", options.help());
    return 0;
  }

  auto in_path = result["in"].as<fs::path>();
  auto out_path = result["out"].as<fs::path>();

  ScratchArena::init_allocator();
  ren::Arena arena = ren::make_arena();
  Renderer *renderer =
      ren_export::create_renderer(&arena, {.type = RendererType::Headless})
          .value();

  Baker *baker = create_baker(&arena, renderer).value();

  FILE *f = fopen(in_path, "rb");
  if (!f) {
    fmt::println(stderr, "Failed to open {} for reading", in_path);
    return -1;
  }

  int w, h;
  const float *buffer = stbi_loadf_from_file(f, &w, &h, nullptr, 4);
  if (!buffer) {
    fmt::println(stderr, "Failed to read HDR environment map from {}: {}",
                 in_path, stbi_failure_reason());
    return -1;
  }
  std::fclose(f);

  Blob blob =
      bake_ibl_to_memory(baker,
                         {
                             .format = TinyImageFormat_R32G32B32A32_SFLOAT,
                             .width = (u32)w,
                             .height = (u32)h,
                             .data = buffer,
                         },
                         not result.count("no-compress"))
          .value();

  fs::path out_dir = out_path.parent_path();
  if (not out_dir.empty()) {
    fs::create_directory(out_dir);
  }

  write_to_file(blob.data, blob.size, out_path).value();

  destroy_baker(baker);
}
