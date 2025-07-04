#include "bake-sg-brdf-lut.h"
#include "ImageBaking.hpp"
#include "core/IO.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "core/Views.hpp"
#include "glsl/BRDF.h"
#include "glsl/Random.h"

#include <DirectXTex.h>
#include <LBFGSB.h>
#include <cxxopts.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glm/gtc/constants.hpp>
#include <random>
#include <tracy/Tracy.hpp>

namespace ren {
namespace {

constexpr double PI = 3.1416;
constexpr double INF = std::numeric_limits<double>::infinity();

constexpr usize NUM_POINTS = 16 * 1024;
constexpr usize NUM_F_NORM_LUT_POINTS = 1024;
constexpr usize ROUGHNESS_SIZE = 32;
constexpr usize NoV_SIZE = 32;

void init_F_norm_lut(double roughness, double NoV) {
  double ToV = glm::sqrt(1 - NoV * NoV);
  glm::dvec3 V = {ToV, 0, NoV};
  for (usize i : range(glsl::F_NORM_LUT_SIZE)) {
    double f0 = i / double(glsl::F_NORM_LUT_SIZE - 1);
    glsl::F_NORM_LUT[i] = 0.0;
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
      glsl::F_NORM_LUT[i] = glm::max(glsl::F_NORM_LUT[i], brdf);
    }
    glsl::F_NORM_LUT[i] = 1 / glsl::F_NORM_LUT[i];
  }
}

void sort_params(Eigen::VectorXd &params) {
  std::pair<double, usize> phis[glsl::MAX_NUM_SGS];
  usize g = params.size() / glsl::NUM_PARAMS;
  for (usize k : range(g)) {
    phis[k] = {params[k * glsl::NUM_PARAMS + 0], k};
  }
  std::ranges::sort(phis);
  double unsorted_params[glsl::MAX_NUM_SGS * glsl::NUM_PARAMS];
  std::ranges::copy(params, unsorted_params);
  for (usize k : range(g)) {
    for (usize i : range(glsl::NUM_PARAMS)) {
      params[k * glsl::NUM_PARAMS + i] =
          unsorted_params[phis[k].second * glsl::NUM_PARAMS + i];
    }
    params[k * glsl::NUM_PARAMS + 2] =
        glm::abs(params[k * glsl::NUM_PARAMS + 2]);
    params[k * glsl::NUM_PARAMS + 3] =
        glm::abs(params[k * glsl::NUM_PARAMS + 3]);
  }
}

auto minimize_local(LBFGSpp::LBFGSBSolver<double> &solver, double roughness,
                    double NoV, Eigen::VectorXd &params,
                    const Eigen::VectorXd &lb, const Eigen::VectorXd &ub)
    -> double {
  u32 g = params.size() / glsl::NUM_PARAMS;
  auto loss_f = [&](const Eigen::VectorXd &params,
                    Eigen::VectorXd &grad) -> double {
    double loss = glsl::ren_sg_brdf_loss({
        .NoV = NoV,
        .roughness = roughness,
        .n = NUM_POINTS,
        .g = g,
        .params = params.data(),
        .grad = grad.data(),
    });
#if 0
    fmt::println("Parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
    }
    fmt::println("Gradient:");
    for (usize k : range(g)) {
      fmt::println("{}",
                   Span(grad.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
    }
    fmt::println("Loss: {}", loss);
#endif
    ren_assert(not glm::isinf(loss) and not glm::isnan(loss));
    for (usize k : range(params.size())) {
      ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
      ren_assert(not glm::isinf(grad[k]) and not glm::isnan(grad[k]));
    }
    return loss;
  };
  double loss;
  solver.minimize(loss_f, params, loss, lb, ub);
  sort_params(params);
  for (usize k : range(params.size())) {
    ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
  }
  ren_assert(loss >= 0);
  return loss;
}

auto minimize_global(LBFGSpp::LBFGSBSolver<double> &solver, double roughness,
                     double NoV, Eigen::VectorXd &params,
                     const Eigen::VectorXd &lb, const Eigen::VectorXd &ub) {

  std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<double> udist;

  const usize g = params.size() / glsl::NUM_PARAMS;

  double loss = minimize_local(solver, roughness, NoV, params, lb, ub);

  const double alpha2 = glm::pow(roughness, 4);
  const double D_CUTOFF = 0.001;

  const glm::dvec4 scale = {
      glm::min(PI, glm::acos(glm::sqrt((alpha2 / glm::sqrt(D_CUTOFF) - 1) /
                                       (alpha2 - 1)))),
      1,
      1,
      1,
  };

  const usize NUM_BH_ITERATIONS = 32;
  const double BH_TARGET_ACCEPT_RATIO = 0.5;
  const double BH_STEPWISE_FACTOR = 0.9;
  const usize BH_INTERVAL = 8;
  const double BH_T = loss * 0.01;
  double bh_stepsize = 0.5;
  usize bh_num_accepted = 0;

  double opt_loss = loss;
  Eigen::VectorXd opt_params = params;
  Eigen::VectorXd old_params;
  double old_loss;

  for (usize bhi : range(NUM_BH_ITERATIONS << (g - 1))) {
    old_params = params;
    old_loss = loss;

    fmt::println("Basin hopping iteration {}:", bhi + 1);

    // Perturb parameters.
    for (usize k : range(params.size())) {
      double s = scale[k % glsl::NUM_PARAMS];
      double l = glm::max(lb[k], params[k] - s * bh_stepsize);
      double h = glm::min(ub[k], params[k] + s * bh_stepsize);
      double Xi = udist(rng);
      params[k] = glm::mix(l, h, Xi);
      ren_assert(params[k] >= lb[k]);
      ren_assert(params[k] <= ub[k]);
    }
    sort_params(params);
    fmt::println("Perturb parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
    }

    fmt::println("Minimize:");
    double loss = INF;
    try {
      loss = minimize_local(solver, roughness, NoV, params, lb, ub);
    } catch (const std::runtime_error &err) {
      fmt::println("Failed: {}", err.what());
    }
    fmt::println("Parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
    }
    fmt::println("Loss: {} ({}x better)", loss, opt_loss / loss);

    // Accept or reject based on Metropolis criterion.
    double C = glm::exp(-(loss - old_loss) / BH_T);
    if (udist(rng) <= C) {
      fmt::println("Accept solution");
      if (loss < opt_loss) {
        opt_params = params;
        opt_loss = loss;
      }
      bh_num_accepted++;
    } else {
      fmt::println("Reject solution");
      params = old_params;
      loss = old_loss;
    }

    usize num_tested = bhi % BH_INTERVAL + 1;
    double accept_rate = double(bh_num_accepted) / num_tested;
    fmt::println("Accept rate: {:.2f}", accept_rate);

    // Update step size.
    if (num_tested == BH_INTERVAL) {
      if (accept_rate > BH_TARGET_ACCEPT_RATIO) {
        fmt::println("Increase step size");
        bh_stepsize /= BH_STEPWISE_FACTOR;
      } else {
        fmt::println("Decrease step size");
        bh_stepsize *= BH_STEPWISE_FACTOR;
      }
      bh_num_accepted = 0;
    }

    fmt::print("\n");
  }

  params = opt_params;
  return opt_loss;
}

auto bake_sg_brdf_lut_to_memory(bool compress) -> Result<Blob, Error> {

  double lut_loss[glsl::MAX_NUM_SGS][NoV_SIZE][ROUGHNESS_SIZE];
  std::ranges::fill_n(&lut_loss[0][0][0],
                      sizeof(lut_loss) / sizeof(lut_loss[0][0][0]), INF);
  Eigen::VectorXd lut_params[glsl::MAX_NUM_SGS][NoV_SIZE][ROUGHNESS_SIZE];

  const usize INIT_IROUGHNESS = ROUGHNESS_SIZE - 1;
  const usize INIT_INoV = NoV_SIZE - 1;
  const double INIT_ROUGHNESS = (INIT_IROUGHNESS + 0.5) / ROUGHNESS_SIZE;
  const double INIT_NoV = (INIT_INoV + 0.5) / NoV_SIZE;

  LBFGSpp::LBFGSBParam<double> solver_options;
  solver_options.max_iterations = 256;
  LBFGSpp::LBFGSBSolver<double> solver(solver_options);

  init_F_norm_lut(INIT_ROUGHNESS, INIT_NoV);

  Eigen::VectorXd params;
  for (u32 g : range<u32>(1, glsl::MAX_NUM_SGS + 1)) {
    ZoneScoped;

    const double INIT_ToV = glm::sqrt(1 - INIT_NoV * INIT_NoV);
    const glm::dvec3 INIT_V = {INIT_ToV, 0, INIT_NoV};
    const glm::dvec3 INIT_R = {-INIT_ToV, 0, INIT_NoV};

    const double INIT_PHI = glm::atan(INIT_R.z, INIT_R.x);

    if (g == 1) {
      params = Eigen::VectorXd::Zero(glsl::NUM_PARAMS);
      params[0] = INIT_PHI;
      params[1] = 1;
      params[2] = 1;
      params[3] = 1;
    } else {
      params.conservativeResize(g * glsl::NUM_PARAMS);
      double *p = &params[(g - 1) * glsl::NUM_PARAMS];
      p[0] = params[(g - 2) * glsl::NUM_PARAMS + 0];
      p[1] = 0;
      p[2] = params[(g - 2) * glsl::NUM_PARAMS + 2];
      p[3] = params[(g - 2) * glsl::NUM_PARAMS + 3];
      sort_params(params);
    }

    Eigen::VectorXd lb = Eigen::VectorXd::Zero(g * glsl::NUM_PARAMS);
    Eigen::VectorXd ub = Eigen::VectorXd::Constant(g * glsl::NUM_PARAMS, INF);
    for (i32 i : range(g)) {
      ub[i * glsl::NUM_PARAMS + 0] = 2 * PI;
    }

    double loss =
        minimize_global(solver, INIT_ROUGHNESS, INIT_NoV, params, lb, ub);

    fmt::println("Fit {} ASG(s) at ({}, {})", g, INIT_IROUGHNESS, INIT_INoV);
    fmt::println("Optimal parameters:");
    for (usize k : range(g)) {
      fmt::println(
          "{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
    }
    fmt::println("Loss: {}", loss);
    fmt::print("\n");

    lut_loss[g - 1][INIT_INoV][INIT_IROUGHNESS] = loss;
    lut_params[g - 1][INIT_INoV][INIT_IROUGHNESS] = params;
  }

  for (u32 g : range<u32>(1, glsl::MAX_NUM_SGS + 1)) {
    Eigen::VectorXd lb = Eigen::VectorXd::Zero(g * glsl::NUM_PARAMS);
    Eigen::VectorXd ub = Eigen::VectorXd::Constant(g * glsl::NUM_PARAMS, INF);
    for (i32 i : range(g)) {
      ub[i * glsl::NUM_PARAMS + 0] = 2 * PI;
    }

    for (i32 iroughness = INIT_IROUGHNESS; iroughness >= 0; iroughness--) {
      for (i32 iNoV = INIT_INoV; iNoV >= 0; iNoV--) {
        if (iroughness == INIT_IROUGHNESS and iNoV == INIT_INoV) {
          continue;
        }

        double roughness = (iroughness + 0.5) / ROUGHNESS_SIZE;
        double NoV = (iNoV + 0.5) / NoV_SIZE;
        init_F_norm_lut(roughness, NoV);

        fmt::println("Fit {} ASG(s) at ({}, {}):", g, iroughness, iNoV);

        if (iroughness + 1 < ROUGHNESS_SIZE) {
          params = lut_params[g - 1][iNoV][iroughness + 1];
        } else {
          ren_assert(iNoV + 1 < NoV_SIZE);
          double dPhi = glm::acos(NoV) - glm::acos((iNoV + 1 + 0.5) / NoV_SIZE);
          ren_assert(dPhi >= 0);
          params = lut_params[g - 1][iNoV + 1][iroughness];
          for (usize k : range(g)) {
            params[k * glsl::NUM_PARAMS + 0] += dPhi;
          }
        }
        double loss = minimize_global(solver, roughness, NoV, params, lb, ub);

        fmt::println("Optimal parameters:");
        for (usize k : range(g)) {
          fmt::println("{}", Span(params.data() + k * glsl::NUM_PARAMS,
                                  glsl::NUM_PARAMS));
        }
        fmt::println("Loss: {}", loss);

        lut_loss[g - 1][iNoV][iroughness] = loss;
        lut_params[g - 1][iNoV][iroughness] = params;
      }
    }
  }

  constexpr usize ROW_SIZE = ROUGHNESS_SIZE;
  constexpr usize NUM_ROWS = NoV_SIZE;
  constexpr usize LAYER_SIZE = ROW_SIZE * NUM_ROWS;
  constexpr usize NUM_LAYERS = (glsl::MAX_NUM_SGS + 1) * glsl::MAX_NUM_SGS / 2;
  glm::vec4 *image = new glm::vec4[LAYER_SIZE * NUM_LAYERS];
  usize layer = 0;
  for (usize g : range<usize>(1, glsl::MAX_NUM_SGS + 1)) {
    for (usize y : range(NUM_ROWS)) {
      for (usize x : range(ROW_SIZE)) {
        for (usize l : range(g)) {
          std::ranges::copy_n(
              &lut_params[g - 1][y][x][l * glsl::NUM_PARAMS], glsl::NUM_PARAMS,
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

  auto blob =
      bake_sg_brdf_lut_to_memory(not result.count("no-compress")).value();
  stringify_and_write_to_files(blob.data, blob.size, path).value();
}
