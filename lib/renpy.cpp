#include "core/StdDef.hpp"
#include "core/Views.hpp"
#include "glsl/BRDF.h"
#include "glsl/SG.h"
#include "ren/tiny_imageformat.h"

#include <DirectXTex.h>
#include <ktx.h>

using namespace ren;

extern "C" void ren_eval_brdf(size_t n, const float *cL, float *y, float f0,
                              float roughness, float NoV) {
  const glm::vec3 V = {glm::sqrt(1.0f - NoV * NoV), 0.0f, NoV};
  const auto *L = (const glm::vec3 *)cL;
  for (usize i : range(n)) {
    float NoL = L[i].z;
    glm::vec3 H = normalize(V + L[i]);
    float NoH = H.z;
    float VoH = dot(V, H);
    float F = glsl::F_schlick(f0, VoH);
    float G = glsl::G_smith(roughness, NoL, NoV);
    float D = glsl::D_ggx(roughness, NoH);
    float Q = 4.0f * NoV;
    y[i] = NoL > 0.0f ? F * G * D / Q : 0.0f;
  }
}

namespace {

constexpr u8 SG_BRDF_LUT_KTX2[] = {
#include "../assets/sg-brdf-lut.ktx2.inc"
};

auto get_sg_brdf_lut() -> const auto * {
  static const auto lut = [] {
    std::array<glm::vec4, glsl::SG_BRDF_ROUGHNESS_SIZE *
                              glsl::SG_BRDF_NoV_SIZE * glsl::NUM_SG_BRDF_LAYERS>
        lut;

    ktx_error_code_e result = KTX_SUCCESS;
    HRESULT hres = S_OK;

    ktxTexture2 *ktx_texture2 = nullptr;
    result = ktxTexture2_CreateFromMemory(
        SG_BRDF_LUT_KTX2, sizeof(SG_BRDF_LUT_KTX2),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture2);
    ren_assert(result == KTX_SUCCESS);
    TinyImageFormat format = TinyImageFormat_FromVkFormat(
        (TinyImageFormat_VkFormat)ktx_texture2->vkFormat);

    if (format == TinyImageFormat_R32G32B32A32_SFLOAT) {
      ren_assert(sizeof(lut) == ktx_texture2->dataSize);
      std::memcpy(lut.data(), ktx_texture2->pData, ktx_texture2->dataSize);
      return lut;
    }

    auto dxgi_format = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(format);
    DirectX::ScratchImage compressed;
    hres = compressed.Initialize2D(dxgi_format, ktx_texture2->baseWidth,
                                   ktx_texture2->baseHeight,
                                   ktx_texture2->numLayers, 1);
    ren_assert(SUCCEEDED(hres));
    for (u32 layer : range(ktx_texture2->numLayers)) {
      const DirectX::Image *image = compressed.GetImage(0, layer, 0);
      ren_assert(image);
      size_t offset = 0;
      result = ktxTexture_GetImageOffset(ktxTexture(ktx_texture2), 0, layer, 0,
                                         &offset);
      ren_assert(result == KTX_SUCCESS);
      std::memcpy(image->pixels, ktx_texture2->pData + offset,
                  image->slicePitch);
    }

    DirectX::ScratchImage decompressed;
    if (DirectX::IsCompressed(dxgi_format)) {
      hres = DirectX::Decompress(compressed.GetImages(),
                                 compressed.GetImageCount(),
                                 compressed.GetMetadata(),
                                 DXGI_FORMAT_R32G32B32A32_FLOAT, decompressed);
      ren_assert(SUCCEEDED(hres));
    } else {
      hres = DirectX::ConvertEx(
          compressed.GetImages(), compressed.GetImageCount(),
          compressed.GetMetadata(), DXGI_FORMAT_R32G32B32A32_FLOAT,
          DirectX::ConvertOptions{}, decompressed);
      ren_assert(SUCCEEDED(hres));
    }

    for (u32 layer : range(decompressed.GetImageCount())) {
      const DirectX::Image *image = decompressed.GetImage(0, layer, 0);
      ren_assert(image);
      std::memcpy(
          &lut[layer * glsl::SG_BRDF_ROUGHNESS_SIZE * glsl::SG_BRDF_NoV_SIZE],
          image->pixels, image->slicePitch);
    }

    return lut;
  }();
  return (const glm::vec4(*)[glsl::SG_BRDF_NoV_SIZE]
                            [glsl::SG_BRDF_ROUGHNESS_SIZE])lut.data();
}
} // namespace

extern "C" void ren_eval_sg_brdf(size_t n, const float *cL, float *y, float f0,
                                 float roughness, float NoV,
                                 size_t num_brdf_sgs) {
  const auto *lut = get_sg_brdf_lut();
  num_brdf_sgs = glm::clamp<u32>(num_brdf_sgs, 1, glsl::MAX_SG_BRDF_SIZE);

  glm::ivec2 size = {glsl::SG_BRDF_ROUGHNESS_SIZE, glsl::SG_BRDF_NoV_SIZE};
  glm::vec2 uv = glm::vec2(size) * glm::vec2(roughness, NoV);
  glm::vec2 ab = glm::fract(uv - 0.5f);
  glm::vec2 w0 = 1.0f - ab;
  glm::vec2 w1 = ab;

  glm::ivec2 ij0 = (uv - 0.5f) - ab;
  glm::ivec2 ij1 = ij0 + 1;
  ij0 = glm::max(ij0, 0);
  ij1 = glm::min(ij1, size - 1);

  glm::vec4 params[glsl::MAX_SG_BRDF_SIZE];
  u32 base_layer = (num_brdf_sgs - 1) * num_brdf_sgs / 2;
  for (u32 layer : range(num_brdf_sgs)) {
    params[layer] = lut[base_layer + layer][ij0.y][ij0.x] * w0.y * w0.x +
                    lut[base_layer + layer][ij0.y][ij1.x] * w0.y * w1.x +
                    lut[base_layer + layer][ij1.y][ij0.x] * w1.y * w0.x +
                    lut[base_layer + layer][ij1.y][ij1.x] * w1.y * w1.x;
  }

  const glm::vec3 V = {glm::sqrt(1.0f - NoV * NoV), 0.0f, NoV};
  const auto *L = (const glm::vec3 *)cL;

  float alpha2 = glm::pow(roughness, 4.0f);
  float sh0 = 2.0f / alpha2;
  float shx0 = sh0 / 8;
  float shy0 = sh0 / (8 * NoV * NoV);

  for (usize i : range(n)) {
    float FGD = 0.0f;
    for (u32 sg : range(num_brdf_sgs)) {
      float phi = params[sg][0];
      float a = params[sg][1];
      float lx = params[sg][2];
      float ly = params[sg][3];

      float cos_phi = cos(phi);
      float sin_phi = sin(phi);
      glm::vec3 Z = {cos_phi, 0.0f, sin_phi};
      glm::vec3 Y = {0, 1, 0};
      glm::vec3 X = {-sin_phi, 0.0f, cos_phi};
      glm::vec3 H = normalize(Z + V);
      float NoH = H.z;
      float VoH = dot(V, H);

      glsl::ASG asg;
      asg.z = Z;
      asg.x = X;
      asg.y = Y;
      asg.a = glsl::F_schlick(f0, VoH) * glsl::D_ggx(roughness, NoH) * a;
      asg.lx = (lx * lx) * shx0;
      asg.ly = (ly * ly) * shy0;

      FGD += glsl::eval_asg(asg, L[i]);
    }
    float Q = 4.0f * NoV;
    y[i] = FGD / Q;
  }
}
