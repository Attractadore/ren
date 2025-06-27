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
using glsl::NUM_PARAMS;

const float PI = 3.1416f;
const float INF = 1.0f / 0.0f;

auto uniform_sample_sphere(glm::vec2 Xi) -> glm::vec3 {
  float phi = 2.0f * PI * Xi[0];
  float z = glm::mix(-1.0f, 1.0f, Xi[1]);
  float r = sqrt(1.0f - z * z);
  return {r * glm::cos(phi), r * glm::sin(phi), z};
};

constexpr usize F_NORM_LUT_SIZE = 128;

void init_F_norm_lut(float roughness, float NoV) {
  glm::vec3 N = {0, 0, 1};
  float ToV = glm::sqrt(1 - NoV * NoV);
  glm::vec3 V = {ToV, 0, NoV};
  for (usize i : range(F_NORM_LUT_SIZE)) {
    float f0 = i / float(F_NORM_LUT_SIZE - 1);
    glsl::F_NORM_LUT[i] = 0.0f;
    constexpr usize PHI_SIZE = 256;
    for (usize k : range(PHI_SIZE)) {
      float phi = glm::mix(-PI, PI, k / float(PHI_SIZE - 1));
      glm::vec3 L = {glm::cos(phi), 0, glm::sin(phi)};
      glm::vec3 H = glm::normalize(L + V);
      float F = glsl::F_schlick(f0, dot(H, V));
      float G = glsl::G_smith(roughness, NoV, dot(N, L));
      float D = glsl::D_ggx(roughness, dot(N, H));
      float Q = 4.0f * dot(N, H);
      float brdf = F * G * D / Q * glm::step(0.0f, dot(N, L));
      glsl::F_NORM_LUT[i] = glm::max(glsl::F_NORM_LUT[i], brdf);
    }
    glsl::F_NORM_LUT[i] = 1.0f / glsl::F_NORM_LUT[i];
  }
}

auto bake_sg_brdf_lut_to_memory(bool compress) -> Result<Blob, Error> {
  const usize ROUGHNESS_SIZE = 32;
  const usize NoV_SIZE = 32;
  const usize NUM_POINTS = 10'000;
  const float MIN_F0 = 0.02f;

  std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<float> udist;

  float f0[NUM_POINTS];
  glm::vec3 L[NUM_POINTS];
  for (i32 i : range(NUM_POINTS)) {
    glm::vec3 Xi = glsl::r3_seq(i);
    f0[i] = glm::mix(MIN_F0, 1.0f, Xi[0]);
    L[i] = uniform_sample_sphere({Xi[1], Xi[2]});
  };
  float y[NUM_POINTS];

  const float INIT_ROUGHNESS = (ROUGHNESS_SIZE - 0.5f) / ROUGHNESS_SIZE;
  const float INIT_NoV = (NoV_SIZE - 0.5f) / NoV_SIZE;
  const float INIT_ToV = glm::sqrt(1.0f - INIT_NoV * INIT_NoV);

  glm::vec3 B = {0, 1, 0};
  glm::vec3 N = {0, 0, 1};
  glm::vec3 INIT_V = {INIT_ToV, 0, INIT_NoV};

  init_F_norm_lut(INIT_ROUGHNESS, INIT_NoV);
  for (usize i : range(NUM_POINTS)) {
    glm::vec3 H = glm::normalize(L[i] + INIT_V);
    float F = glsl::F_norm(f0[i], dot(H, INIT_V));
    float G = glsl::G_smith(INIT_ROUGHNESS, dot(N, INIT_V), dot(N, L[i]));
    float D = glsl::D_ggx(INIT_ROUGHNESS, dot(N, H));
    float Q = 4.0f * dot(N, H);
    y[i] = dot(N, L[i]) > 0.0f ? F * D * D / Q : 0.0f;
    ren_assert(not glm::isnan(y[i]));
  }

  auto eval_asgs = [&](const Eigen::VectorXf &params, float f0, glm::vec3 V,
                       glm::vec3 L) -> float {
    float s = 0.0f;
    for (usize i : range(params.size() / NUM_PARAMS)) {
      const float *p = &params[NUM_PARAMS * i];
      glsl::ASG asg = glsl::make_asg(p[0], p[1], p[2], p[3], f0, V, B);
      s += eval_asg(asg, L);
    }
    return s;
  };

  Eigen::VectorXf params;
  for (u32 g : range<u32>(1, glsl::MAX_NUM_SGS + 1)) {
    ZoneScoped;

    if (g == 1) {
      params = Eigen::VectorXf::Zero(NUM_PARAMS);
      params[0] = 0.5f * PI + glm::acos(INIT_NoV);
    } else {
      float maxdelta = 0.0f;
      float phimax = 0.5f * PI;
      for (usize i : range(200)) {
        glm::vec2 Xi = glsl::r2_seq(i);
        float f0 = glm::mix(MIN_F0, 1.0f, Xi[0]);
        float phi = glm::mix(-PI, PI, Xi[1]);
        glm::vec3 L = {glm::cos(phi), 0, glm::sin(phi)};
        glm::vec3 H = glm::normalize(L + INIT_V);
        float F = glsl::F_norm(f0, dot(H, INIT_V));
        float G = glsl::G_smith(INIT_ROUGHNESS, dot(N, INIT_V), dot(N, L));
        float D = glsl::D_ggx(INIT_ROUGHNESS, dot(N, H));
        float Q = 4.0f * dot(N, H);
        float y = F * D * D / Q * glm::step(0.0f, dot(N, L));
        float delta = glm::abs(y - eval_asgs(params, f0, INIT_V, L));
        if (delta > maxdelta) {
          maxdelta = delta;
          phimax = phi;
        }
      }
      params.conservativeResize(g * NUM_PARAMS);
      params[(g - 1) * NUM_PARAMS + 0] = phimax;
      params[(g - 1) * NUM_PARAMS + 1] = 0.0f;
      params[(g - 1) * NUM_PARAMS + 2] = 0.0f;
      params[(g - 1) * NUM_PARAMS + 3] = 0.0f;
    }

    Eigen::VectorXf lb = Eigen::VectorXf::Zero(g * NUM_PARAMS);
    Eigen::VectorXf ub = Eigen::VectorXf::Constant(g * NUM_PARAMS, INF);
    for (i32 i : range(g)) {
      lb[i * NUM_PARAMS + 0] = -PI;
      ub[i * NUM_PARAMS + 0] = PI;
    }

    auto loss_f = [&](const Eigen::VectorXf &params,
                      Eigen::VectorXf &grad) -> float {
      for (usize k : range(params.size())) {
        ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
      }
      ZoneScoped;
      float loss = glsl::ren_sg_brdf_loss({
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
        fmt::println("{}", Span(params.data() + k * NUM_PARAMS, NUM_PARAMS));
      }
      fmt::println("Gradient:");
      for (usize k : range(g)) {
        fmt::println("{}", Span(grad.data() + k * NUM_PARAMS, NUM_PARAMS));
      }
      fmt::println("Loss: {}", loss);
#endif
      return loss;
    };

    LBFGSpp::LBFGSBParam<float> solver_options;
    solver_options.max_iterations = 64;
    solver_options.epsilon = 0.001f;
    solver_options.epsilon_rel = 0.001f;
    LBFGSpp::LBFGSBSolver<float> solver(solver_options);

    float loss;
    Eigen::VectorXf grad(params.size());
    try {
      solver.minimize(loss_f, params, loss, lb, ub);
    } catch (const std::exception &) {
      loss = loss_f(params, grad);
    }
    for (usize k : range(params.size())) {
      ren_assert(not glm::isinf(params[k]) and not glm::isnan(params[k]));
    }

    float opt_loss = loss;
    Eigen::VectorXf opt_params = params;

    glm::vec4 scale = {PI, 0, 0, 0};
    for (usize i : range(g)) {
      scale[1] = glm::min(2.0f * params[i * NUM_PARAMS + 1],
                          glsl::D_ggx(INIT_ROUGHNESS, 1.0f));
      float s = 2.0f * glm::max(params[i * NUM_PARAMS + 2],
                                params[i * NUM_PARAMS + 3]);
      s = glm::max(s, 1.0f);
      scale[2] = s;
      scale[3] = s;
    }
    ren_assert(not glm::isinf(scale[0]) and not glm::isnan(scale[0]));
    ren_assert(not glm::isinf(scale[1]) and not glm::isnan(scale[1]));
    ren_assert(not glm::isinf(scale[2]) and not glm::isnan(scale[2]));
    ren_assert(not glm::isinf(scale[3]) and not glm::isnan(scale[3]));

    const usize NUM_BH_ITERATIONS = 128;
    const float BH_TARGET_ACCEPT_RATIO = 0.5f;
    const float BH_STEPWISE_FACTOR = 0.9f;
    const usize BH_INTERVAL = 8;
    const float BH_T = loss * 0.01f;
    float bh_stepsize = 0.5f;
    usize bh_num_accepted = 0;

    Eigen::VectorXf old_params;
    float old_loss;
    for (usize bhi : range(NUM_BH_ITERATIONS)) {
      old_params = params;
      old_loss = loss;

      fmt::println("Basin hopping iteration {}:", bhi + 1);

      // Perturb parameters.
      for (usize k : range(params.size())) {
        float s = scale[k % NUM_PARAMS];
        float l = glm::max(lb[k], params[k] - s * bh_stepsize);
        float h = glm::min(ub[k], params[k] + s * bh_stepsize);
        float Xi = udist(rng);
        params[k] = glm::mix(l, h, Xi);
        ren_assert(params[k] >= lb[k]);
        ren_assert(params[k] <= ub[k]);
      }
      fmt::println("Perturb parameters:");
      for (usize k : range(g)) {
        fmt::println("{}", Span(params.data() + k * NUM_PARAMS, NUM_PARAMS));
      }

      fmt::println("Minimize:");
      try {
        solver.minimize(loss_f, params, loss, lb, ub);
      } catch (const std::exception &) {
        loss = loss_f(params, grad);
      }
      fmt::println("Parameters:");
      for (usize k : range(g)) {
        fmt::println("{}", Span(params.data() + k * NUM_PARAMS, NUM_PARAMS));
      }
      fmt::println("Loss: {} ({}x better)", loss, opt_loss / loss);

      // Accept or reject based on Metropolis criterion.
      float C = glm::exp(-(loss - old_loss) / BH_T);
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
      float accept_rate = float(bh_num_accepted) / num_tested;
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

    fmt::println("Fit {} ASG(s)", g);
    fmt::println("Optimal parameters:");
    for (usize k : range(g)) {
      fmt::println("{}", Span(params.data() + k * NUM_PARAMS, NUM_PARAMS));
    }
    fmt::println("Loss: {}", loss);
    fmt::print("\n");
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
