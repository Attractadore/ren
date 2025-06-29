#include "bake-sg-brdf-lut.h"
#include "core/IO.hpp"
#include "core/Result.hpp"
#include "core/Span.hpp"
#include "core/Views.hpp"
#include "glsl/BRDF.h"
#include "glsl/Random.h"

#include <LBFGSB.h>
#include <cxxopts.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glm/gtc/constants.hpp>
#include <random>
#include <stdlib.h>
#include <tracy/Tracy.hpp>

namespace ren {
namespace {

constexpr double PI = 3.1416;
constexpr double INF = std::numeric_limits<double>::infinity();

#if 0
constexpr usize NUM_POINTS = 128 * 1024;
constexpr usize NUM_POINTS_SLICE = 16 * 1024;
constexpr usize ROUGHNESS_SIZE = 32;
constexpr usize NoV_SIZE = 32;
#else
constexpr usize NUM_POINTS = 1024 * 1024;
constexpr usize NUM_POINTS_SLICE = 1024 * 1024;
constexpr usize ROUGHNESS_SIZE = 8;
constexpr usize NoV_SIZE = 8;
#endif
constexpr double MIN_F0 = 0.02;

auto uniform_sample_sphere(glm::dvec2 Xi) -> glm::dvec3 {
  double phi = 2.0 * PI * Xi[0];
  double z = glm::mix(-1.0, 1.0, Xi[1]);
  double r = glm::sqrt(1.0 - z * z);
  return {r * glm::cos(phi), r * glm::sin(phi), z};
};

void init_F_norm_lut(double roughness, double NoV) {
  glm::dvec3 N = {0, 0, 1};
  double ToV = glm::sqrt(1 - NoV * NoV);
  glm::dvec3 V = {ToV, 0, NoV};
  for (usize i : range(glsl::F_NORM_LUT_SIZE)) {
    double f0 = i / double(glsl::F_NORM_LUT_SIZE - 1);
    glsl::F_NORM_LUT[i] = 0.0;
    for (usize k : range(NUM_POINTS_SLICE)) {
      double phi = glm::mix(-PI, PI, k / double(NUM_POINTS_SLICE - 1));
      glm::dvec3 L = {glm::cos(phi), 0, glm::sin(phi)};
      glm::dvec3 H = glm::normalize(L + V);
      double F = glsl::F_schlick(f0, dot(H, V));
      double G = glsl::G_smith(roughness, dot(N, L), NoV);
      double D = glsl::D_ggx(roughness, dot(N, H));
      double Q = 4.0 * dot(N, V);
      double brdf = dot(N, L) > 0 ? F * G * D / Q : 0;
      glsl::F_NORM_LUT[i] = glm::max(glsl::F_NORM_LUT[i], brdf);
    }
    glsl::F_NORM_LUT[i] = 1.0 / glsl::F_NORM_LUT[i];
  }
}

auto bake_sg_brdf_lut_to_memory(bool compress) -> Result<Blob, Error> {

  double lut_loss[glsl::MAX_NUM_SGS][NoV_SIZE][ROUGHNESS_SIZE];
  std::ranges::fill_n(&lut_loss[0][0][0],
                      sizeof(lut_loss) / sizeof(lut_loss[0][0][0]), INF);
  Eigen::VectorXd lut_params[glsl::MAX_NUM_SGS][NoV_SIZE][ROUGHNESS_SIZE];

  std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<double> udist;

  double *f0 = new double[NUM_POINTS];
  glm::dvec3 *L = new glm::dvec3[NUM_POINTS];
  for (i32 i : range(NUM_POINTS)) {
    glm::dvec3 Xi = glsl::r3_seq(i);
    f0[i] = glm::mix(MIN_F0, 1.0, Xi[0]);
    L[i] = uniform_sample_sphere({Xi[1], Xi[2]});
  };
  double *y = new double[NUM_POINTS];

  const glm::dvec3 B = {0, 1, 0};
  const glm::dvec3 N = {0, 0, 1};

  auto init_training_data = [&](double roughness, double NoV) {
    init_F_norm_lut(roughness, NoV);
    double ToV = glm::sqrt(1 - NoV * NoV);
    glm::dvec3 V = {ToV, 0, NoV};
    for (usize i : range(NUM_POINTS)) {
      glm::dvec3 H = glm::normalize(L[i] + V);
      double F = glsl::F_norm(f0[i], dot(H, V));
      double G = glsl::G_smith(roughness, dot(N, L[i]), NoV);
      double D = glsl::D_ggx(roughness, dot(N, H));
      double Q = 4.0 * NoV;
      y[i] = dot(N, L[i]) > 0.0 ? F * G * D / Q : 0.0;
      ren_assert(y[i] <= 1.1);
      ren_assert(not glm::isnan(y[i]));
    }
  };

#if 0
  const usize INIT_IROUGHNESS = ROUGHNESS_SIZE - 1;
  const usize INIT_INoV = NoV_SIZE - 1;
#endif
  const usize INIT_IROUGHNESS = 0;
  const usize INIT_INoV = 0;
  const double INIT_ROUGHNESS = (INIT_IROUGHNESS + 0.5) / ROUGHNESS_SIZE;
  const double INIT_NoV = (INIT_INoV + 0.5) / NoV_SIZE;
  const double INIT_ToV = glm::sqrt(1.0 - INIT_NoV * INIT_NoV);
  const glm::dvec3 INIT_V = {INIT_ToV, 0, INIT_NoV};

  init_training_data(INIT_ROUGHNESS, INIT_NoV);

  auto eval_asgs = [&](const Eigen::VectorXd &params, double f0, glm::dvec3 V,
                       glm::dvec3 L) -> double {
    double s = 0.0;
    for (usize i : range(params.size() / glsl::NUM_PARAMS)) {
      const double *p = &params[glsl::NUM_PARAMS * i];
      glsl::DASG asg = glsl::make_asg(p[0], p[1], p[2], p[3], f0, V, B);
      s += eval_asg(asg, L);
    }
    return s;
  };

  Eigen::VectorXd params;
  for (u32 g : range<u32>(1, glsl::MAX_NUM_SGS + 1)) {
    ZoneScoped;

    if (g == 1) {
      params = Eigen::VectorXd::Zero(glsl::NUM_PARAMS);
      params[0] = 0.5 * PI + glm::acos(INIT_NoV);
    } else {
      double maxdelta = 0.0;
      double phimax = 0.5 * PI;
      for (usize i : range(256)) {
        glm::dvec2 Xi = glsl::r2_seq(i);
        double f0 = glm::mix(MIN_F0, 1.0, Xi[0]);
        double phi = glm::mix(-PI, PI, Xi[1]);
        glm::dvec3 L = {glm::cos(phi), 0, glm::sin(phi)};
        glm::dvec3 H = glm::normalize(L + INIT_V);
        double F = glsl::F_norm(f0, dot(H, INIT_V));
        double G = glsl::G_smith(INIT_ROUGHNESS, dot(N, INIT_V), dot(N, L));
        double D = glsl::D_ggx(INIT_ROUGHNESS, dot(N, H));
        double Q = 4.0f * dot(N, H);
        double y = F * D * D / Q * glm::step(0.0, dot(N, L));
        double delta = glm::abs(y - eval_asgs(params, f0, INIT_V, L));
        if (delta > maxdelta) {
          maxdelta = delta;
          phimax = phi;
        }
      }
      params.conservativeResize(g * glsl::NUM_PARAMS);
      params[(g - 1) * glsl::NUM_PARAMS + 0] = phimax;
      params[(g - 1) * glsl::NUM_PARAMS + 1] = 0.0;
      params[(g - 1) * glsl::NUM_PARAMS + 2] = 0.0;
      params[(g - 1) * glsl::NUM_PARAMS + 3] = 0.0;
    }

    Eigen::VectorXd lb = Eigen::VectorXd::Zero(g * glsl::NUM_PARAMS);
    Eigen::VectorXd ub = Eigen::VectorXd::Constant(g * glsl::NUM_PARAMS, INF);
    for (i32 i : range(g)) {
      lb[i * glsl::NUM_PARAMS + 0] = -PI;
      ub[i * glsl::NUM_PARAMS + 0] = PI;
      lb[i * glsl::NUM_PARAMS + 2] = -INF;
      lb[i * glsl::NUM_PARAMS + 3] = -INF;
    }

    auto loss_f = [&](const Eigen::VectorXd &params,
                      Eigen::VectorXd &grad) -> double {
      for (usize k : range(params.size())) {
        ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
      }
      ZoneScoped;
      double loss = glsl::ren_sg_brdf_loss({
          .V = INIT_V,
          .B = B,
          .n = NUM_POINTS,
          .f0 = f0,
          .L = L,
          .y = y,
          .g = g,
          .params = params.data(),
          .grad = grad.data(),
      });
#if 0
      fmt::println("Parameters:");
      for (usize k : range(g)) {
        fmt::println("{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
      }
      fmt::println("Gradient:");
      for (usize k : range(g)) {
        fmt::println("{}", Span(grad.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
      }
      fmt::println("Loss: {}", loss);
#endif
      return loss;
    };

    LBFGSpp::LBFGSBParam<double> solver_options;
    solver_options.max_iterations = 64;
    solver_options.epsilon = 10.0 / NUM_POINTS;
    solver_options.epsilon_rel = 0.001;
    LBFGSpp::LBFGSBSolver<double> solver(solver_options);

    double loss;
    solver.minimize(loss_f, params, loss, lb, ub);

    for (usize k : range(params.size())) {
      ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
    }

    double opt_loss = loss;
    Eigen::VectorXd opt_params = params;

    glm::vec4 scale = {PI, 0, 0, 0};
    for (usize i : range(g)) {
      scale[1] = glm::min(2.0 * params[i * glsl::NUM_PARAMS + 1],
                          glsl::D_ggx(INIT_ROUGHNESS, 1.0));
      double s = 2.0 * glm::max(params[i * glsl::NUM_PARAMS + 2],
                                params[i * glsl::NUM_PARAMS + 3]);
      s = glm::max(s, 1.0);
      scale[2] = s;
      scale[3] = s;
    }
    ren_assert(not glm::isinf(scale[0]) and not glm::isnan(scale[0]));
    ren_assert(not glm::isinf(scale[1]) and not glm::isnan(scale[1]));
    ren_assert(not glm::isinf(scale[2]) and not glm::isnan(scale[2]));
    ren_assert(not glm::isinf(scale[3]) and not glm::isnan(scale[3]));

    const usize NUM_BH_ITERATIONS = 32;
    const double BH_TARGET_ACCEPT_RATIO = 0.5;
    const double BH_STEPWISE_FACTOR = 0.9;
    const usize BH_INTERVAL = 8;
    const double BH_T = loss * 0.01;
    double bh_stepsize = 0.5;
    usize bh_num_accepted = 0;

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
      fmt::println("Perturb parameters:");
      for (usize k : range(g)) {
        fmt::println(
            "{}", Span(params.data() + k * glsl::NUM_PARAMS, glsl::NUM_PARAMS));
      }

      fmt::println("Minimize:");
      try {
        solver.minimize(loss_f, params, loss, lb, ub);
      } catch (const std::runtime_error &) {
        fmt::println("Failed");
        loss = INF;
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

    loss = opt_loss;
    params = opt_params;

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
      lb[i * glsl::NUM_PARAMS + 0] = -PI;
      ub[i * glsl::NUM_PARAMS + 0] = PI;
      lb[i * glsl::NUM_PARAMS + 2] = -INF;
      lb[i * glsl::NUM_PARAMS + 3] = -INF;
    }

    for (i32 iroughness = INIT_IROUGHNESS; iroughness >= 0; iroughness--) {
      for (i32 iNoV = INIT_INoV; iNoV >= 0; iNoV--) {
        for (auto [iroughness_offset, iNoV_offset] :
             {std::pair{0, -1}, {-1, 0}, {-1, -1}}) {
          if (iroughness + iroughness_offset < 0 or iNoV + iNoV_offset < 0) {
            continue;
          }

          float roughness =
              (iroughness + iroughness_offset + 0.5) / ROUGHNESS_SIZE;
          float NoV = (iNoV + iNoV_offset + 0.5) / NoV_SIZE;
          init_training_data(roughness, NoV);

          LBFGSpp::LBFGSBParam<double> solver_options;
          solver_options.max_iterations = 32;
          solver_options.epsilon = lut_loss[g - 1][iNoV][iroughness] * 0.00001;
          solver_options.epsilon_rel = 0.001;
          LBFGSpp::LBFGSBSolver<double> solver(solver_options);

          auto loss_f = [&](const Eigen::VectorXd &params,
                            Eigen::VectorXd &grad) -> double {
            double loss = glsl::ren_sg_brdf_loss({
                .V = INIT_V,
                .B = B,
                .n = NUM_POINTS,
                .f0 = f0,
                .L = L,
                .y = y,
                .g = g,
                .params = params.data(),
                .grad = grad.data(),
            });
#if 0
            fmt::println("Parameters:");
            for (usize k : range(g)) {
              fmt::println("{}", Span(params.data() + k * glsl::NUM_PARAMS,
                                      glsl::NUM_PARAMS));
            }
            fmt::println("Gradient:");
            for (usize k : range(g)) {
              fmt::println("{}", Span(grad.data() + k * glsl::NUM_PARAMS,
                                      glsl::NUM_PARAMS));
            }
            fmt::println("Loss: {}", loss);
#endif
            return loss;
          };

          params = lut_params[g - 1][iNoV][iroughness];
          double loss;
          solver.minimize(loss_f, params, loss, lb, ub);

          fmt::println("Fit {} ASG(s) at ({}, {})", g,
                       iroughness + iroughness_offset, iNoV + iNoV_offset);
          fmt::println("Optimal parameters:");
          for (usize k : range(g)) {
            fmt::println("{}", Span(params.data() + k * glsl::NUM_PARAMS,
                                    glsl::NUM_PARAMS));
          }
          fmt::println("Loss: {}", loss);

          if (loss < lut_loss[g - 1][iNoV + iNoV_offset]
                             [iroughness + iroughness_offset]) {
            lut_loss[g - 1][iNoV + iNoV_offset]
                    [iroughness + iroughness_offset] = loss;
            lut_params[g - 1][iNoV + iNoV_offset]
                      [iroughness + iroughness_offset] = params;
          }
        }
      }
    }
  }

  for (u32 g : range<u32>(1, glsl::MAX_NUM_SGS + 1)) {
    for (i32 iroughness = INIT_IROUGHNESS; iroughness >= 0; iroughness--) {
      for (i32 iNoV = INIT_INoV; iNoV >= 0; iNoV--) {
        float loss = lut_loss[g - 1][iNoV][iroughness];
        params = lut_params[g - 1][iNoV][iroughness];
        fmt::println("Fit {} ASG(s) at ({}, {})", g, iroughness, iNoV);
        fmt::println("Optimal parameters:");
        for (usize k : range(g)) {
          fmt::println("{}", Span(params.data() + k * glsl::NUM_PARAMS,
                                  glsl::NUM_PARAMS));
        }
        fmt::println("Loss: {}", loss);
      }
    }
  }

  return {};
}

} // namespace
} // namespace ren

using namespace ren;

int main(int argc, const char *argv[]) {
#if __USE_XOPEN2K
  setenv("TRACY_NO_EXIT", "1", false);
#endif

#if 0
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
#endif

  auto blob = bake_sg_brdf_lut_to_memory(true).value();
}
