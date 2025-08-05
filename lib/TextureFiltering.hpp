#pragma once
#include <glm/glm.hpp>

namespace ren {

inline auto texture_lod(unsigned width, unsigned height,
                        const glm::vec4 *pixels, glm::vec2 st) -> glm::vec4 {
  glm::ivec2 size = {width, height};
  glm::vec2 uv = st * glm::vec2(size);
  glm::vec2 ab = glm::fract(uv - 0.5f);
  glm::vec2 w0 = 1.0f - ab;
  glm::vec2 w1 = ab;

  glm::ivec2 ij0 = (uv - 0.5f) - ab;
  glm::ivec2 ij1 = ij0 + 1;
  ij0 = glm::max(ij0, 0);
  ij1 = glm::min(ij1, size - 1);

  return pixels[ij0.y * width + ij0.x] * w0.y * w0.x +
         pixels[ij0.y * width + ij1.x] * w0.y * w1.x +
         pixels[ij1.y * width + ij0.x] * w1.y * w0.x +
         pixels[ij1.y * width + ij1.x] * w1.y * w1.x;
}

} // namespace ren
