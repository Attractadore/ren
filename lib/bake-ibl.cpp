#include "BakeReflectionMap.comp.hpp"
#include "BakeSpecularMap.comp.hpp"
#include "Baking.hpp"
#include "ImageBaking.hpp"
#include "SgEnvLightingLoss.comp.hpp"
#include "SgEnvLightingPreview.comp.hpp"
#include "TextureFiltering.hpp"
#include "core/IO.hpp"
#include "core/StdDef.hpp"
#include "core/Views.hpp"
#include "glsl/BRDF.h"
#include "glsl/Random.h"
#include "glsl/SG.h"
#include "glsl/SgEnvLightingLoss.h"

#include <LBFGSB.h>
#include <cxxopts.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h>
#include <ktx.h>
#include <numeric>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <DirectXTex.h>

using namespace ren;

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
  if (!baker->pipelines.sg_env_lighting_loss) {
    ren_try(baker->pipelines.sg_env_lighting_loss,
            load_compute_pipeline(baker->session_arena, SgEnvLightingLossCS,
                                  "SG environment lighting loss"));
  }
  if (!baker->pipelines.sg_env_lighting_preview) {
    ren_try(baker->pipelines.sg_env_lighting_preview,
            load_compute_pipeline(baker->session_arena, SgEnvLightingPreviewCS,
                                  "SG environment lighting preview"));
  }

  fmt::println("Generate equirectangular map mip chain");

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

  fmt::println("Generate pre-convolved cube map");

  constexpr TinyImageFormat CUBE_MAP_FORMAT =
      TinyImageFormat_R32G32B32A32_SFLOAT;
  constexpr usize CUBE_MAP_SIZE = 512;
  constexpr usize NUM_CUBE_MAP_MIPS = ilog2(CUBE_MAP_SIZE) + 1;

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
          for (u32 mip : range<u32>(1, NUM_CUBE_MAP_MIPS)) {
            const float MIN_SHARPNESS = glsl::roughness_to_asg_sharpness(
                glsl::MIN_CONVOLVED_SG_CUBE_MAP_ROUGHNESS);
            float sharpness =
                MIN_SHARPNESS * (1 << (2 * (NUM_CUBE_MAP_MIPS - 1 - mip)));
            cmd.push_constants(glsl::BakeSpecularMapArgs{
                .equirectangular_map = args.equirectangular_map,
                .specular_map =
                    (glsl::StorageTextureCube)rg.get_storage_texture_descriptor(
                        args.cube_map, mip),
                .sharpness = sharpness,
            });
            u32 size = CUBE_MAP_SIZE >> mip;
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
  rhi::start_gfx_capture("Pre-convolve environment map");
  ren_try_to(rg.execute({.gfx_cmd_pool = baker->cmd_pool}));
  rhi::end_gfx_capture();
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

  constexpr usize IRRADIANCE_SIZE = 32;
  constexpr usize NUM_POINTS = IRRADIANCE_SIZE * IRRADIANCE_SIZE * 6;
  constexpr usize NUM_BATCHES = NUM_POINTS / glsl::SG_ENV_LIGHTING_LOSS_THREADS;
  static_assert(NUM_POINTS % glsl::SG_ENV_LIGHTING_LOSS_THREADS == 0);

  ren_try(BufferSlice<float> params_buffer,
          baker->arena.create_buffer<float>({
              .name = "Optimization parameters",
              .heap = rhi::MemoryHeap::Upload,
              .count = glsl::MAX_NUM_SG_ENV_LIGHTING_PARAMS,
          }));

  ren_try(BufferSlice<float> grad_buffer,
          baker->arena.create_buffer<float>({
              .name = "Gradient",
              .heap = rhi::MemoryHeap::Readback,
              .count = glsl::MAX_NUM_SG_ENV_LIGHTING_PARAMS * NUM_BATCHES,
          }));

  ren_try(BufferSlice<float> loss_buffer, baker->arena.create_buffer<float>({
                                              .name = "Loss",
                                              .heap = rhi::MemoryHeap::Readback,
                                              .count = NUM_BATCHES,
                                          }));

  ren_try(BufferSlice<glm::vec3> luminance_buffer,
          baker->arena.create_buffer<glm::vec3>({
              .name = "Luminance",
              .heap = rhi::MemoryHeap::Default,
              .count = NUM_POINTS,
          }));

  u32 num_sgs = glm::min<u32>(16, glsl::MAX_SG_ENV_LIGHTING_SIZE);

  Eigen::VectorXd params(num_sgs * glsl::NUM_SG_ENV_LIGHTING_PARAMS);
  Eigen::VectorXd params_lb = Eigen::VectorXd::Zero(params.size());
  Eigen::VectorXd params_ub =
      Eigen::VectorXd::Constant(params.size(), 1.0f / 0.0f);

  {
    fmt::println("Pre-sample environment map");

    DirectX::TexMetadata mdata = mip_chain.GetMetadata();

    auto upload_buffer =
        baker->upload_allocator.allocate<glm::vec3>(NUM_POINTS);
    for (usize i : range(NUM_POINTS)) {
      float dA = 4.0f * glsl::PI / NUM_POINTS;
      glm::vec2 Xi = glsl::r2_seq(i);
      glm::vec2 phi_z = glsl::uniform_sample_cylinder(Xi);
      float phi = phi_z[0];
      float z = glm::clamp(phi_z[1], -0.99f, 0.99f);
      float theta = glm::acos(z);
      glm::vec2 uv = {phi / glsl::TWO_PI, theta / glsl::PI};

      float jac = glm::sqrt(1.0f - z * z);
      // dA = sin(theta) * 2 * pi^2 / (W * H) =>
      // W * H = 2 * pi^2 * sin(theta) / dA
      // W0 * H0 * exp2(2 * -lod) = 2 * pi^2 * sin(theta) / dA
      float lod = -0.5f * glm::log2(2.0f * glsl::PI * glsl::PI * jac /
                                    (dA * mdata.width * mdata.height));

      u32 hi_lod = glm::clamp(lod, 0.0f, mdata.mipLevels - 1.0f);
      u32 lo_lod = glm::clamp(lod + 1.0f, 0.0f, mdata.mipLevels - 1.0f);

      glm::uvec2 hi_size = get_mip_size({mdata.width, mdata.height, 1}, hi_lod);
      glm::uvec2 lo_size = get_mip_size({mdata.width, mdata.height, 1}, lo_lod);

      ren_assert(mdata.format == DXGI_FORMAT_R32G32B32A32_FLOAT);
      glm::vec3 luminance = glm::mix(
          texture_lod(
              hi_size.x, hi_size.y,
              (const glm::vec4 *)mip_chain.GetImage(hi_lod, 0, 0)->pixels, uv),
          texture_lod(
              lo_size.x, lo_size.y,
              (const glm::vec4 *)mip_chain.GetImage(lo_lod, 0, 0)->pixels, uv),
          glm::fract(lod));
      upload_buffer.host_ptr[i] = luminance;
      ren_assert(not glm::any(glm::isnan(luminance)));

      if (i < num_sgs) {
        usize offset = i * glsl::NUM_SG_ENV_LIGHTING_PARAMS;
        params[offset + 0] = phi;
        params_lb[offset + 0] = 0.0f;
        params_ub[offset + 0] = glsl::TWO_PI;
        params[offset + 1] = z;
        params_lb[offset + 1] = -1.0f;
        params_ub[offset + 1] = 1.0f;
        params[offset + 2] = luminance.r;
        params[offset + 3] = luminance.g;
        params[offset + 4] = luminance.b;
        params[offset + 5] = 0.0f;
      }
    }

    CommandRecorder cmd;
    ren_try_to(cmd.begin(*baker->renderer, baker->cmd_pool));
    cmd.copy_buffer(upload_buffer.slice, luminance_buffer);
    cmd.memory_barrier({
        .src_stage_mask = rhi::PipelineStage::Transfer,
        .src_access_mask = rhi::Access::TransferWrite,
        .dst_stage_mask = rhi::PipelineStage::ComputeShader,
        .dst_access_mask = rhi::Access::ShaderBufferRead,
    });
    ren_try_to(baker->renderer->submit(rhi::QueueFamily::Graphics,
                                       {cmd.end().value()}));
  }

  LBFGSpp::LBFGSBParam<double> solver_options;
  solver_options.max_iterations = 4096;
  LBFGSpp::LBFGSBSolver<double> solver(solver_options);

  auto loss_f = [&](const Eigen::VectorXd &params,
                    Eigen::VectorXd &grad) -> double {
    CommandRecorder cmd;
    cmd.begin(*baker->renderer, baker->cmd_pool).value();
    cmd.bind_compute_pipeline(baker->pipelines.sg_env_lighting_loss);
    std::ranges::copy(params, baker->renderer->map_buffer(params_buffer));
    cmd.push_constants(glsl::SgEnvLightingLossArgs{
        .num_sgs = num_sgs,
        .params = baker->renderer->get_buffer_device_ptr(params_buffer),
        .grad = baker->renderer->get_buffer_device_ptr(grad_buffer),
        .loss = baker->renderer->get_buffer_device_ptr(loss_buffer),
        .luminance = DevicePtr<float>(
            baker->renderer->get_buffer_device_ptr(luminance_buffer)),
    });
    cmd.dispatch(NUM_BATCHES);
    baker->renderer->submit(rhi::QueueFamily::Graphics, {cmd.end().value()})
        .value();
    baker->renderer->wait_idle();
    baker->renderer->reset_command_pool(baker->cmd_pool).value();

    Span rb_loss(baker->renderer->map_buffer(loss_buffer), NUM_BATCHES);
    double loss = std::reduce(rb_loss.begin(), rb_loss.end(), 0.0) / NUM_POINTS;
    // fmt::println("Loss: {}", loss);
    ren_assert(not glm::isinf(loss) and not glm::isnan(loss));

    float *rb_grad = baker->renderer->map_buffer(grad_buffer);
    for (usize k : range(params.size())) {
      auto v = Span(&rb_grad[NUM_BATCHES * k], NUM_BATCHES);
      grad[k] = std::reduce(v.begin(), v.end(), 0.0) / NUM_POINTS;
      ren_assert(not glm::isinf(grad[k]) and not glm::isnan(grad[k]));
    }

    return loss;
  };

  for (usize i : range(num_sgs)) {
  }

  fmt::println("Fit environment lighting SG mixture");
  rhi::start_gfx_capture("Fit environment lighting SG mixture");
  double loss = 1.0f / 0.0f;
  try {
    solver.minimize(loss_f, params, loss, params_lb, params_ub);
  } catch (const std::runtime_error &) {
  }
  rhi::end_gfx_capture();

  if (rhi::have_gfx_debugger()) {
    fmt::println("Render SG environment lighting preview");

    RgBuilder rgb(baker->rg, *baker->renderer, baker->descriptor_allocator);

    RgTextureId preview = baker->rg.create_texture(RgTextureCreateInfo{
        .name = "sg-env-lighting-preview",
        .format = CUBE_MAP_FORMAT,
        .width = IRRADIANCE_SIZE,
        .height = IRRADIANCE_SIZE,
        .cube_map = true,
    });

    auto pass = rgb.create_pass({.name = "Render preview map"});

    RgSgEnvLightingPreviewArgs args = {
        .num_sgs = num_sgs,
        .params = baker->renderer->get_buffer_device_ptr(params_buffer),
        .preview_map = pass.write_texture("sg-env-lighting-preview", &preview,
                                          rhi::CS_UNORDERED_ACCESS_IMAGE),
    };
    std::ranges::copy(params, baker->renderer->map_buffer(params_buffer));

    pass.dispatch_grid_3d(baker->pipelines.sg_env_lighting_preview, args,
                          {IRRADIANCE_SIZE, IRRADIANCE_SIZE, 6});

    ren_try(RenderGraph rg, rgb.build({}));
    rhi::start_gfx_capture("SG environment lighting preview");
    ren_try_to(rg.execute({.gfx_cmd_pool = baker->cmd_pool}));
    rhi::end_gfx_capture();
    baker->renderer->wait_idle();
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

  std::unique_ptr<IRenderer> renderer =
      create_renderer({.type = RendererType::Headless}).value();

  IBaker *baker = create_baker(renderer.get()).value();

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
