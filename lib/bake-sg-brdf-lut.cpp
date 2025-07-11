#include "Baking.hpp"
#include "CommandRecorder.hpp"
#include "ImageBaking.hpp"
#include "Renderer.hpp"
#include "SgBrdfLoss.comp.hpp"
#include "core/IO.hpp"
#include "core/NotNull.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "core/Views.hpp"
#include "glsl/BRDF.h"
#include "glsl/Random.h"
#include "glsl/SG.h"
#include "glsl/SgBrdfLoss.h"

#include <cxxopts.hpp>

#include <DirectXTex.h>
#include <LBFGSB.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glm/gtc/constants.hpp>
#include <random>
#include <tracy/Tracy.hpp>

namespace ren {
namespace {

struct GpuContext {
  Handle<ComputePipeline> kernel;
  BufferSlice<float> f_norm_lut;
  BufferSlice<float> params;
  BufferSlice<float> loss;
  BufferSlice<float> grad;
  NotNull<Renderer *> renderer;
  Handle<CommandPool> cmd_pool;
};

constexpr double PI = 3.1416;
constexpr double INF = std::numeric_limits<double>::infinity();

constexpr usize NUM_POINTS = 16 * 1024;
constexpr usize NUM_F_NORM_LUT_POINTS = 1024;

void init_F_norm_lut(float *f_norm_lut, double roughness, double NoV) {
  double ToV = glm::sqrt(1 - NoV * NoV);
  glm::dvec3 V = {ToV, 0, NoV};
  for (usize i : range(glsl::F_NORM_LUT_SIZE)) {
    double f0 = i / double(glsl::F_NORM_LUT_SIZE - 1);
    double v = 0;
    for (usize k : range(NUM_F_NORM_LUT_POINTS)) {
      glm::dvec2 Xi = glsl::r2_seq(k);
      glm::dvec3 H = glsl::importance_sample_ggx(Xi, roughness);
      double VoH = dot(V, H);
      glm::dvec3 L = 2 * VoH * H - V;
      double F = VoH > 0 ? glsl::F_schlick(f0, VoH) : 0;
      double G = glsl::G_smith(roughness, L.z, NoV);
      double D = glsl::D_ggx(roughness, H.z);
      double Q = 4 * NoV;
      double brdf = L.z > 0 ? F * G * D / Q : 0;
      v = glm::max(v, brdf);
    }
    f_norm_lut[i] = 1 / v;
  }
}

void sort_params(Eigen::VectorXd &params) {
  std::pair<double, usize> phis[glsl::MAX_SG_BRDF_SIZE];
  usize g = params.size() / glsl::NUM_SG_BRDF_PARAMS;
  for (usize k : range(g)) {
    phis[k] = {params[k * glsl::NUM_SG_BRDF_PARAMS + 0], k};
  }
  std::ranges::sort(phis, &phis[g]);
  double unsorted_params[glsl::MAX_SG_BRDF_SIZE * glsl::NUM_SG_BRDF_PARAMS];
  std::ranges::copy(params, unsorted_params);
  for (usize k : range(g)) {
    for (usize i : range(glsl::NUM_SG_BRDF_PARAMS)) {
      params[k * glsl::NUM_SG_BRDF_PARAMS + i] =
          unsorted_params[phis[k].second * glsl::NUM_SG_BRDF_PARAMS + i];
    }
    params[k * glsl::NUM_SG_BRDF_PARAMS + 2] =
        glm::abs(params[k * glsl::NUM_SG_BRDF_PARAMS + 2]);
    params[k * glsl::NUM_SG_BRDF_PARAMS + 3] =
        glm::abs(params[k * glsl::NUM_SG_BRDF_PARAMS + 3]);
  }
}

auto minimize_local(const GpuContext &ctx,
                    LBFGSpp::LBFGSBSolver<double> &solver, double roughness,
                    double NoV, Eigen::VectorXd &params,
                    const Eigen::VectorXd &lb, const Eigen::VectorXd &ub)
    -> double {
  u32 g = params.size() / glsl::NUM_SG_BRDF_PARAMS;

  auto loss_f = [&](const Eigen::VectorXd &params,
                    Eigen::VectorXd &grad) -> double {
    ZoneScoped;

    CommandRecorder cmd;
    cmd.begin(*ctx.renderer, ctx.cmd_pool).value();

    cmd.bind_compute_pipeline(ctx.kernel);
    std::ranges::copy(params, ctx.renderer->map_buffer(ctx.params));
    cmd.push_constants(glsl::SgBrdfLossArgs{
        .f_norm_lut = ctx.renderer->get_buffer_device_ptr(ctx.f_norm_lut),
        .params = ctx.renderer->get_buffer_device_ptr(ctx.params),
        .loss = ctx.renderer->get_buffer_device_ptr(ctx.loss),
        .grad = ctx.renderer->get_buffer_device_ptr(ctx.grad),
        .NoV = (float)NoV,
        .roughness = (float)roughness,
        .n = NUM_POINTS,
        .g = g,
    });
    cmd.dispatch(NUM_POINTS / 32);

    ctx.renderer->submit(rhi::QueueFamily::Graphics, {cmd.end().value()})
        .value();
    ctx.renderer->wait_idle();
    ctx.renderer->reset_command_pool(ctx.cmd_pool).value();

    Span rb_loss(ctx.renderer->map_buffer(ctx.loss), NUM_POINTS / 32);
    double loss = std::reduce(rb_loss.begin(), rb_loss.end(), 0.0) / NUM_POINTS;
    ren_assert(not glm::isinf(loss) and not glm::isnan(loss));

    float *rb_grad = ctx.renderer->map_buffer(ctx.grad);
    for (usize k : range(g * glsl::NUM_SG_BRDF_PARAMS)) {
      auto v = Span(&rb_grad[NUM_POINTS / 32 * k], NUM_POINTS / 32);
      grad[k] = std::reduce(v.begin(), v.end(), 0.0) / NUM_POINTS;
      ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
      ren_assert(not glm::isinf(grad[k]) and not glm::isnan(grad[k]));
    }

#if 0
    fmt::println("Parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
    fmt::println("Gradient:");
    for (usize k : range(g)) {
      fmt::println("{}",
                   Span(grad.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
    fmt::println("Loss: {}", loss);
#endif

    return loss;
  };
  double loss;
  try {
    solver.minimize(loss_f, params, loss, lb, ub);
  } catch (const std::runtime_error &err) {
#if 0
    fmt::println("Minimize failed: {}", err.what());
#endif
    Eigen::VectorXd grad(params.size());
    loss = loss_f(params, grad);
  }
  sort_params(params);
  for (usize k : range(params.size())) {
    ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
  }
  ren_assert(loss >= 0);
  return loss;
}

auto minimize_global(const GpuContext &ctx,
                     LBFGSpp::LBFGSBSolver<double> &solver, double roughness,
                     double NoV, Eigen::VectorXd &params,
                     const Eigen::VectorXd &lb, const Eigen::VectorXd &ub) {
  std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<double> udist;

  const double alpha2 = glm::pow(roughness, 4);
  const double D_CUTOFF = glm::max(glm::sqrt(0.01), alpha2);
  const double LOBE_WIDTH =
      2 *
      glm::acos(glm::sqrt((alpha2 / glm::sqrt(D_CUTOFF) - 1) / (alpha2 - 1)));
  ren_assert(not glm::isnan(LOBE_WIDTH));
  ren_assert(LOBE_WIDTH <= PI);

  const glm::dvec4 scale = {LOBE_WIDTH, 1, 1, 1};

  const usize NUM_BH_ITERATIONS = 128;
  const double BH_TARGET_ACCEPT_RATIO = 0.5;
  const double BH_STEPWISE_FACTOR = 0.9;
  const usize BH_INTERVAL = 8;
  double bh_stepsize = 0.5;
  usize bh_num_accepted = 0;

  double opt_loss = minimize_local(ctx, solver, roughness, NoV, params, lb, ub);
  Eigen::VectorXd opt_params = params;
  double bh_t = opt_loss * 0.01;
  double old_loss = opt_loss;
  Eigen::VectorXd old_params = params;
  for (usize bhi : range(NUM_BH_ITERATIONS)) {
#if 0
    fmt::println("Basin hopping iteration {}:", bhi + 1);
#endif

    // Perturb parameters.
    for (usize k : range(params.size())) {
      double s = scale[k % glsl::NUM_SG_BRDF_PARAMS];
      double l = glm::max(lb[k], params[k] - s * bh_stepsize);
      double h = glm::min(ub[k], params[k] + s * bh_stepsize);
      double Xi = udist(rng);
      params[k] = glm::mix(l, h, Xi);
      ren_assert(params[k] >= lb[k]);
      ren_assert(params[k] <= ub[k]);
    }
    sort_params(params);
#if 0
    fmt::println("Perturb parameters:");
    for (usize k : range(params.size() / glsl::NUM_SG_BRDF_PARAMS)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
#endif

#if 0
    fmt::println("Minimize:");
#endif
    double loss = minimize_local(ctx, solver, roughness, NoV, params, lb, ub);
#if 0
    fmt::println("Parameters:");
    for (usize k : range(params.size() / glsl::NUM_SG_BRDF_PARAMS)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
    fmt::println("Loss: {} ({}x better)", loss, opt_loss / loss);
#endif

    // Accept or reject based on Metropolis criterion.
    double C = glm::exp(-(loss - old_loss) / bh_t);
    if (udist(rng) <= C) {
#if 0
      fmt::println("Accept solution");
#endif
      if (loss < opt_loss) {
        opt_params = params;
        opt_loss = loss;
        bh_t = opt_loss * 0.01;
      }
      bh_num_accepted++;
    } else {
#if 0
      fmt::println("Reject solution");
#endif
      params = old_params;
      loss = old_loss;
    }

    usize num_tested = bhi % BH_INTERVAL + 1;
    double accept_rate = double(bh_num_accepted) / num_tested;
#if 0
    fmt::println("Accept rate: {:.2f}", accept_rate);
#endif

    // Update step size.
    if (num_tested == BH_INTERVAL) {
      if (accept_rate > BH_TARGET_ACCEPT_RATIO) {
#if 0
        fmt::println("Increase step size");
#endif
        bh_stepsize /= BH_STEPWISE_FACTOR;
      } else {
#if 0
        fmt::println("Decrease step size");
#endif
        bh_stepsize *= BH_STEPWISE_FACTOR;
      }
      bh_num_accepted = 0;
    }

    old_params = params;
    old_loss = loss;
  }

  ren_assert(opt_loss < INF);
  params = opt_params;
  return opt_loss;
}

auto bake_sg_brdf_lut_to_memory(IBaker *baker, bool compress)
    -> Result<Blob, Error> {
  if (!baker->pipelines.sg_brdf_loss) {
    ren_try(baker->pipelines.sg_brdf_loss,
            load_compute_pipeline(baker->session_arena, SgBrdfLossCS,
                                  "SG BRDF Loss"));
  }

  GpuContext ctx = {
      .kernel = baker->pipelines.sg_brdf_loss,
      .renderer = baker->renderer,
      .cmd_pool = baker->cmd_pool,
  };

  ren_try(ctx.f_norm_lut, baker->arena.create_buffer<float>({
                              .name = "F_norm LUT",
                              .heap = rhi::MemoryHeap::Readback,
                              .count = glsl::F_NORM_LUT_SIZE,
                          }));

  ren_try(ctx.params, baker->arena.create_buffer<float>({
                          .name = "Optimization parameters",
                          .heap = rhi::MemoryHeap::Readback,
                          .count = NUM_POINTS * glsl::MAX_SG_BRDF_PARAMS,
                      }));

  ren_try(ctx.loss, baker->arena.create_buffer<float>({
                        .name = "Loss",
                        .heap = rhi::MemoryHeap::Readback,
                        .count = NUM_POINTS,
                    }));

  ren_try(ctx.grad, baker->arena.create_buffer<float>({
                        .name = "Gradient",
                        .heap = rhi::MemoryHeap::Readback,
                        .count = NUM_POINTS * glsl::MAX_SG_BRDF_PARAMS,
                    }));

  double lut_loss[glsl::MAX_SG_BRDF_SIZE][glsl::SG_BRDF_NoV_SIZE]
                 [glsl::SG_BRDF_ROUGHNESS_SIZE];
  std::ranges::fill_n(&lut_loss[0][0][0],
                      sizeof(lut_loss) / sizeof(lut_loss[0][0][0]), INF);
  Eigen::VectorXd lut_params[glsl::MAX_SG_BRDF_SIZE][glsl::SG_BRDF_NoV_SIZE]
                            [glsl::SG_BRDF_ROUGHNESS_SIZE];

  const usize INIT_IROUGHNESS = glsl::SG_BRDF_ROUGHNESS_SIZE - 1;
  const usize INIT_INoV = glsl::SG_BRDF_NoV_SIZE - 1;

  const double INIT_ROUGHNESS =
      (INIT_IROUGHNESS + 0.5) / glsl::SG_BRDF_ROUGHNESS_SIZE;
  const double INIT_NoV = (INIT_INoV + 0.5) / glsl::SG_BRDF_NoV_SIZE;

  LBFGSpp::LBFGSBParam<double> solver_options;
  solver_options.max_iterations = 256;
  LBFGSpp::LBFGSBSolver<double> solver(solver_options);

  init_F_norm_lut(ctx.renderer->map_buffer(ctx.f_norm_lut), INIT_ROUGHNESS,
                  INIT_NoV);

  Eigen::VectorXd params;
  for (u32 g : range<u32>(1, glsl::MAX_SG_BRDF_SIZE + 1)) {
    ZoneScoped;

    const double INIT_ToV = glm::sqrt(1 - INIT_NoV * INIT_NoV);
    const glm::dvec3 INIT_V = {INIT_ToV, 0, INIT_NoV};
    const glm::dvec3 INIT_R = {-INIT_ToV, 0, INIT_NoV};

    const double INIT_PHI = glm::atan(INIT_R.z, INIT_R.x);

    if (g == 1) {
      params = Eigen::VectorXd::Zero(glsl::NUM_SG_BRDF_PARAMS);
      params[0] = INIT_PHI;
      params[1] = 1;
      params[2] = 1;
      params[3] = 1;
    } else {
      params.conservativeResize(g * glsl::NUM_SG_BRDF_PARAMS);
      double *p = &params[(g - 1) * glsl::NUM_SG_BRDF_PARAMS];
      p[0] = INIT_PHI;
      p[1] = 1;
      p[2] = 1;
      p[3] = 1;
      sort_params(params);
    }

    Eigen::VectorXd lb = Eigen::VectorXd::Zero(g * glsl::NUM_SG_BRDF_PARAMS);
    Eigen::VectorXd ub =
        Eigen::VectorXd::Constant(g * glsl::NUM_SG_BRDF_PARAMS, INF);
    for (i32 i : range(g)) {
      ub[i * glsl::NUM_SG_BRDF_PARAMS + 0] = 2 * PI;
    }

    fmt::println("Fit {} ASG(s) at ({}, {})", g, INIT_IROUGHNESS, INIT_INoV);

#if 0
    fmt::println("Initial parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
#endif

    double loss =
        minimize_global(ctx, solver, INIT_ROUGHNESS, INIT_NoV, params, lb, ub);
#if 0
    fmt::println("Optimal parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_SG_BRDF_PARAMS, glsl::NUM_SG_BRDF_PARAMS));
    }
#endif
    fmt::println("Loss: {}", loss);

    lut_loss[g - 1][INIT_INoV][INIT_IROUGHNESS] = loss;
    lut_params[g - 1][INIT_INoV][INIT_IROUGHNESS] = params;
  }

  for (u32 g : range<u32>(1, glsl::MAX_SG_BRDF_SIZE + 1)) {
    Eigen::VectorXd lb = Eigen::VectorXd::Zero(g * glsl::NUM_SG_BRDF_PARAMS);
    Eigen::VectorXd ub =
        Eigen::VectorXd::Constant(g * glsl::NUM_SG_BRDF_PARAMS, INF);
    for (i32 i : range(g)) {
      ub[i * glsl::NUM_SG_BRDF_PARAMS + 0] = 2 * PI;
    }

    for (i32 iroughness = INIT_IROUGHNESS; iroughness >= 0; iroughness--) {
      for (i32 iNoV = INIT_INoV; iNoV >= 0; iNoV--) {
        double roughness = (iroughness + 0.5) / glsl::SG_BRDF_ROUGHNESS_SIZE;
        double NoV = (iNoV + 0.5) / glsl::SG_BRDF_NoV_SIZE;
        init_F_norm_lut(ctx.renderer->map_buffer(ctx.f_norm_lut), roughness,
                        NoV);

        fmt::println("Fit {} ASG(s) at ({}, {})", g, iroughness, iNoV);

        if (iroughness + 1 < glsl::SG_BRDF_ROUGHNESS_SIZE) {
          params = lut_params[g - 1][iNoV][iroughness + 1];
          double loss =
              minimize_global(ctx, solver, roughness, NoV, params, lb, ub);
          lut_loss[g - 1][iNoV][iroughness] = loss;
          lut_params[g - 1][iNoV][iroughness] = params;
        }

        if (iNoV + 1 < glsl::SG_BRDF_NoV_SIZE) {
          params = lut_params[g - 1][iNoV + 1][iroughness];
          double dPhi = glm::acos(NoV) -
                        glm::acos((iNoV + 1 + 0.5) / glsl::SG_BRDF_NoV_SIZE);
          ren_assert(dPhi >= 0);
          params = lut_params[g - 1][iNoV + 1][iroughness];
          for (usize k : range(g)) {
            params[k * glsl::NUM_SG_BRDF_PARAMS + 0] += dPhi;
          }
          double loss =
              minimize_global(ctx, solver, roughness, NoV, params, lb, ub);
          if (loss < lut_loss[g - 1][iNoV][iroughness]) {
            lut_loss[g - 1][iNoV][iroughness] = loss;
            lut_params[g - 1][iNoV][iroughness] = params;
          }
        }

#if 0
        fmt::println("Optimal parameters:");
        for (usize k : range(g)) {
          fmt::println(
              "{}",
              Span(&lut_params[g - 1][iNoV][iroughness][k * glsl::NUM_SG_BRDF_PARAMS],
                   glsl::NUM_SG_BRDF_PARAMS));
        }
#endif
        fmt::println("Loss: {}", lut_loss[g - 1][iNoV][iroughness]);
      }
    }
  }

  constexpr usize ROW_SIZE = glsl::SG_BRDF_ROUGHNESS_SIZE;
  constexpr usize NUM_ROWS = glsl::SG_BRDF_NoV_SIZE;
  constexpr usize LAYER_SIZE = ROW_SIZE * NUM_ROWS;
  constexpr usize NUM_LAYERS =
      (glsl::MAX_SG_BRDF_SIZE + 1) * glsl::MAX_SG_BRDF_SIZE / 2;
  glm::vec4 *image = new glm::vec4[LAYER_SIZE * NUM_LAYERS];
  usize layer = 0;
  for (usize g : range<usize>(1, glsl::MAX_SG_BRDF_SIZE + 1)) {
    for (usize y : range(NUM_ROWS)) {
      for (usize x : range(ROW_SIZE)) {
        for (usize l : range(g)) {
          std::ranges::copy_n(
              &lut_params[g - 1][y][x][l * glsl::NUM_SG_BRDF_PARAMS],
              glsl::NUM_SG_BRDF_PARAMS,
              &image[(l + layer) * LAYER_SIZE + y * ROW_SIZE + x].x);
        }
      }
    }
    layer += g;
  }

  TextureInfo tex_info = {
      .format = TinyImageFormat_R32G32B32A32_SFLOAT,
      .width = ROW_SIZE,
      .height = NUM_ROWS,
      .num_layers = NUM_LAYERS,
      .data = image,
  };

#if 0
  if (compress) {
    Vector<DirectX::Image> images;
    DirectX::TexMetadata mdata = to_dxtex_images(tex_info,images);
    DirectX::ScratchImage compressed;
    HRESULT hres = DirectX::Compress(
        images.data(), images.size(), mdata, DXGI_FORMAT_BC7_UNORM,
        DirectX::TEX_COMPRESS_DEFAULT, 0.0f, compressed);
    if (FAILED(hres)) {
      return fail(hres);
    }
    return write_ktx_to_memory(compressed);
  }
#endif

  return write_ktx_to_memory(tex_info);
}

} // namespace
} // namespace ren

using namespace ren;

int main(int argc, const char *argv[]) {
#if __USE_XOPEN2K
  setenv("TRACY_NO_EXIT", "1", false);
#endif

  cxxopts::Options options("bake-sg-brdf-lut",
                           "Bake Spherical Gaussian BRDF LUT");
  // clang-format off
  options.add_options()
    ("out", "output path", cxxopts::value<fs::path>())
    ("no-compress", "don't compress")
    ("h,help", "show this message")
  ;
  // clang-format on
  options.parse_positional({"out"});
  options.positional_help("out");
  cxxopts::ParseResult result = options.parse(argc, argv);
  if (result.count("help") or not result.count("out")) {
    fmt::println("{}", options.help());
    return 0;
  }

  auto path = result["out"].as<fs::path>();

  std::unique_ptr<IRenderer> renderer =
      ren::create_renderer({.type = RendererType::Headless}).value();
  IBaker *baker = ren::create_baker(renderer.get()).value();

  auto blob = bake_sg_brdf_lut_to_memory(baker, not result.count("no-compress"))
                  .value();
  stringify_and_write_to_files(blob.data, blob.size, path).value();

  destroy_baker(baker);
}
