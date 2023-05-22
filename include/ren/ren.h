#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <cassert>
#include <span>
#endif

// Define nodiscard
#ifdef __cplusplus
#define REN_NODISCARD [[nodiscard]]
#else

#if _MSC_VER
#if _MSC_VER >= 1700
#define REN_NODISCARD _Check_return_
#endif
#else
#define REN_NODISCARD __attribute__((warn_unused_result))
#endif

#ifndef REN_NODISCARD
#define REN_NODISCARD
#endif

#endif

// Define inline
#ifdef __cplusplus
#define REN_INLINE inline
#else
#define REN_INLINE static inline
#endif

// Define macro to specify default values in C++
#ifdef __cplusplus
#define REN_DEFAULT_VALUE(...) = __VA_ARGS__
#else
#define REN_DEFAULT_VALUE(...)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Define a bool type compatible with C and C++
typedef int RenBool;

enum {
  REN_TRUE = 0,
  REN_FALSE = 1,
};

typedef enum {
  REN_SUCCESS = 0,
  REN_VULKAN_ERROR,
  REN_SYSTEM_ERROR,
  REN_RUNTIME_ERROR,
  REN_UNKNOWN_ERROR,
} RenResult;

typedef float RenVector2[2];
typedef float RenVector3[3];
typedef float RenVector4[4];
typedef float RenMatrix4x4[4][4];

typedef struct RenDevice RenDevice;
typedef struct RenSwapchain RenSwapchain;
typedef struct RenScene RenScene;

typedef enum : uint32_t {
  REN_NULL_MESH = 0,
} RenMesh;

typedef enum : uint32_t {
  REN_NULL_IMAGE = 0,
} RenImage;

typedef enum : uint32_t {
  REN_NULL_MATERIAL = 0,
} RenMaterial;

typedef enum : uint32_t {
  REN_NULL_MESH_INST = 0,
} RenMeshInst;

typedef enum : uint32_t {
  REN_NULL_DIR_LIGHT = 0,
} RenDirLight;

typedef enum : uint32_t {
  REN_NULL_POINT_LIGHT = 0,
} RenPointLight;

typedef enum : uint32_t {
  REN_NULL_SPOT_LIGHT = 0,
} RenSpotLight;

typedef enum {
  REN_PROJECTION_PERSPECTIVE = 0,
  REN_PROJECTION_ORTHOGRAPHIC,
} RenProjection;

/// Perspective projection parameters.
typedef struct {
  /// Horizontal field of view in radians.
  float hfov;
} RenPerspectiveProjection;

/// Orthographic projection parameters.
typedef struct {
  /// Width of the orthographic projection box.
  float width;
} RenOrthographicProjection;

typedef enum {
  REN_EXPOSURE_MODE_CAMERA,
  REN_EXPOSURE_MODE_AUTOMATIC,
} RenExposureMode;

/// Scene camera.
typedef struct RenCameraDesc {
  /// The type of projection to use.
  RenProjection projection REN_DEFAULT_VALUE(REN_PROJECTION_PERSPECTIVE);
  union {
    /// Perspective camera projection.
    RenPerspectiveProjection perspective REN_DEFAULT_VALUE({
        .hfov = 90.0f / 3.1415f,
    });
    /// Orthographic camera projection.
    RenOrthographicProjection orthographic;
  };
  /// Horizontal rendering resolution.
  unsigned width REN_DEFAULT_VALUE(1280);
  /// Vertical rendering resolution.
  unsigned height REN_DEFAULT_VALUE(720);
  /// Relative aperture in f-stops. Affects exposure when it's calculated based
  /// on camera parameters and depth of field.
  float aperture REN_DEFAULT_VALUE(16.0f);
  /// Shutter time in seconds. Affects exposure when it's calculated based
  /// on camera parameters and motion blur.
  float shutter_time REN_DEFAULT_VALUE(1.0f / 400.0f);
  /// Sensitivity in ISO. Ignored if exposure is not calculated based on camera
  /// parameters.
  float iso REN_DEFAULT_VALUE(400.0f);
  /// Exposure compensation in f-stops.
  float exposure_compensation REN_DEFAULT_VALUE(0.0f);
  /// Exposure computation mode.
  RenExposureMode exposure_mode REN_DEFAULT_VALUE(REN_EXPOSURE_MODE_CAMERA);
  /// This camera's position in the world.
  RenVector3 position REN_DEFAULT_VALUE({0.0f, 0.0f, 0.0f});
  /// Where this camera is facing.
  RenVector3 forward REN_DEFAULT_VALUE({1.0f, 0.0f, 0.0f});
  /// This camera's up vector.
  RenVector3 up REN_DEFAULT_VALUE({0.0f, 0.0f, 1.0f});

#ifdef __cplusplus
public:
  auto set_projection(const RenPerspectiveProjection &perspective) noexcept
      -> RenCameraDesc & {
    this->projection = REN_PROJECTION_PERSPECTIVE;
    this->perspective = perspective;
    return *this;
  }

  auto set_projection(const RenOrthographicProjection &ortho) noexcept
      -> RenCameraDesc & {
    projection = REN_PROJECTION_ORTHOGRAPHIC;
    orthographic = ortho;
    return *this;
  }
#endif
} RenCameraDesc;

typedef enum {
  REN_TONEMAPPING_OPERATOR_REINHARD = 0,
  REN_TONEMAPPING_OPERATOR_ACES,
} RenTonemappingOperator;

/// The parameters of the mesh to be created.
typedef struct RenMeshDesc {
  /// The number of vertices that this mesh has.
  unsigned num_vertices;
  /// Optional: the number of indices that this mesh has.
  unsigned num_indices;
  /// This mesh's positions.
  const RenVector3 *positions;
  /// Optional: this mesh's vertex colors.
  const RenVector4 *colors;
  /// Optional: this mesh's normals. Computed automatically if not specified.
  const RenVector3 *normals;
  /// Optional: this mesh's tangents. Computed automatically if not specified.
  /// When normals are not provided, the tangents are ignored and
  /// computed automatically instead.
  /// For tangents to be computed automatically, uvs must be provided.
  const RenVector4 *tangents;
  /// Optional: this mesh's uvs.
  const RenVector2 *uvs;
  /// Optional: this mesh's indices.
  const unsigned *indices;

#ifdef __cplusplus
public:
  auto set_positions(std::span<const RenVector3> positions) noexcept
      -> RenMeshDesc & {
    assert(num_vertices == 0 or num_vertices == positions.size());
    this->num_vertices = positions.size();
    this->positions = positions.data();
    return *this;
  }

  auto set_colors(std::span<const RenVector4> colors) noexcept
      -> RenMeshDesc & {
    assert(num_vertices == 0 or num_vertices == colors.size());
    this->num_vertices = colors.size();
    this->colors = colors.data();
    return *this;
  }

  auto set_normals(std::span<const RenVector3> normals) noexcept
      -> RenMeshDesc & {
    assert(num_vertices == 0 or num_vertices == normals.size());
    this->num_vertices = normals.size();
    this->normals = normals.data();
    return *this;
  }

  auto set_tangents(std::span<const RenVector4> tangents) noexcept
      -> RenMeshDesc & {
    assert(num_vertices == 0 or num_vertices == tangents.size());
    this->num_vertices = tangents.size();
    this->tangents = tangents.data();
    return *this;
  }

  auto set_uvs(std::span<const RenVector2> uvs) noexcept -> RenMeshDesc & {
    assert(num_vertices == 0 or num_vertices == uvs.size());
    this->num_vertices = uvs.size();
    this->uvs = uvs.data();
    return *this;
  }

  auto set_indices(std::span<const unsigned> indices) noexcept
      -> RenMeshDesc & {
    this->num_indices = indices.size();
    this->indices = indices.data();
    return *this;
  }
#endif
} RenMeshDesc;

typedef enum {
  REN_FORMAT_R8_UNORM,
  REN_FORMAT_R8_SRGB,
  REN_FORMAT_RG8_UNORM,
  REN_FORMAT_RG8_SRGB,
  REN_FORMAT_RGB8_UNORM,
  REN_FORMAT_RGB8_SRGB,
  REN_FORMAT_RGBA8_UNORM,
  REN_FORMAT_RGBA8_SRGB,
  REN_FORMAT_R16_UNORM,
  REN_FORMAT_RG16_UNORM,
  REN_FORMAT_RGB16_UNORM,
  REN_FORMAT_RGBA16_UNORM,
  REN_FORMAT_RGB32_SFLOAT,
  REN_FORMAT_RGBA32_SFLOAT,
} RenFormat;

/// The parameters of the image to be created.
typedef struct RenImageDesc {
  /// This image's format.
  RenFormat format;
  /// This image's width.
  unsigned width;
  /// This image's height.
  unsigned height;
  /// This image's data.
  const void *data;
} RenImageDesc;

typedef enum {
  REN_FILTER_NEAREST,
  REN_FILTER_LINEAR,
} RenFilter;

typedef enum {
  REN_WRAPPING_MODE_REPEAT,
  REN_WRAPPING_MODE_MIRRORED_REPEAT,
  REN_WRAPPING_MODE_CLAMP_TO_EDGE,
} RenWrappingMode;

typedef struct {
  RenFilter mag_filter;
  RenFilter min_filter;
  RenFilter mipmap_filter;
  RenWrappingMode wrap_u;
  RenWrappingMode wrap_v;
} RenSampler;

typedef enum {
  REN_TEXTURE_CHANNEL_IDENTITY = 0,
  REN_TEXTURE_CHANNEL_ZERO,
  REN_TEXTURE_CHANNEL_ONE,
  REN_TEXTURE_CHANNEL_R,
  REN_TEXTURE_CHANNEL_G,
  REN_TEXTURE_CHANNEL_B,
  REN_TEXTURE_CHANNEL_A,
} RenTextureChannel;

typedef struct {
  RenTextureChannel r, g, b, a;
} RenTextureChannelSwizzle;

typedef struct {
  RenImage image;
  RenSampler sampler;
  RenTextureChannelSwizzle swizzle;
} RenTexture;

typedef enum {
  REN_ALPHA_MODE_OPAQUE = 0,
  REN_ALPHA_MODE_MASK,
  REN_ALPHA_MODE_BLEND,
} RenAlphaMode;

/// The parameters of the material to be created.
typedef struct RenMaterialDesc {
  /// Color, multiplied with vertex color (if present, otherwise with
  /// [1.0, 1.0, 1.0, 1.0]), and texture color (if present, otherwise with
  /// [1.0, 1.0, 1.0, 1.0]). Must be between 0 and 1.
  RenVector4 base_color_factor REN_DEFAULT_VALUE({1.0f, 1.0f, 1.0f, 1.0f});
  /// Optional: color texture.
  RenTexture color_tex;
  /// Metallic factor, multiplied with channel B of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1.
  float metallic_factor REN_DEFAULT_VALUE(1.0f);
  /// Roughness factor, multiplied with channel G of the metallic-roughness
  /// texture (if present, otherwise with 1.0). Must be between 0 and 1.
  float roughness_factor REN_DEFAULT_VALUE(1.0f);
  /// Optional: metallic-roughness texture.
  RenTexture metallic_roughness_tex;
  /// Optional: multiplier for channel R of the occlusion texture. Ignored if
  /// there is no occlusion texture. Must be between 0 and 1.
  float occlusion_strength REN_DEFAULT_VALUE(1.0f);
  /// Optional: occlusion texture.
  RenTexture occlusion_tex;
  /// Optional: multiplier for channels X and Y of the normal texture. Ignored
  /// if there is no normal texture.
  float normal_scale REN_DEFAULT_VALUE(1.0f);
  /// Optional: normal texture.
  RenTexture normal_tex;
  /// Emissive lighting luminance in nits, multiplied with the emissive texture
  /// (if present, otherwise with 1.0).
  RenVector3 emissive_factor;
  /// Optional: emissive texture.
  RenTexture emissive_tex;
  /// Alpha mode.
  RenAlphaMode alpha_mode REN_DEFAULT_VALUE(REN_ALPHA_MODE_OPAQUE);
  /// Optional: alpha value below which objects are not rendered when
  /// alpha mode is set to mask.
  float alpha_cutoff;
  /// Whether this material is thin and must be rendered from both sides at
  /// once.
  RenBool double_sided REN_DEFAULT_VALUE(false);
} RenMaterialDesc;

/// The parameters of the mesh instance to be created.
typedef struct {
  /// The mesh to use with this mesh instance.
  RenMesh mesh;
  /// The material this mesh instance will be rendered with.
  RenMaterial material;
  /// Whether this mesh instance can cast shadows.
  RenBool casts_shadows;
} RenMeshInstDesc;

/// The parameters of the directional light to be created.
typedef struct {
  /// This light's color. Must be between 0 and 1.
  RenVector3 color;
  /// This light's intensity in lux.
  float illuminance;
  /// The direction from illuminated objects to this light.
  RenVector3 origin;
} RenDirLightDesc;

/// The parameters of the point light to be created.
typedef struct {
  /// This light's color. Must be between 0 and 1.
  RenVector3 color;
  /// This light's luminous intensity in candela.
  float intensity;
  /// This light's position in the world.
  RenVector3 position;
  /// The maximum distance at which this light illuminates objects.
  float radius;
  /// Whether or not this light can cast shadows.
  RenBool casts_shadows;
} RenPointLightDesc;

/// The parameters of the spotlight to be created.
typedef struct {
  /// This spotlight's color. Must be between 0 and 1.
  RenVector3 color;
  /// This spotlight's luminous intensity in candela.
  float intensity;
  /// This spotlight's position in the world.
  RenVector3 position;
  /// The maximum distance at which this spotlight affects objects.
  float radius;
  /// The direction in which this spotlight is facing.
  RenVector3 direction;
  /// The maximum angle (in radians) from this spotlight's direction at
  /// which the intensity doesn't drop.
  float inner_angle;
  /// The angle (in radians) from this spotlight's direction at which the
  /// intensity drops to 0. Must be larger than the inner angle.
  float outer_angle;
  /// Whether or not this spotlight can cast shadows.
  RenBool casts_shadows;
} RenSpotLightDesc;

void ren_DestroyDevice(RenDevice *device);

void ren_DestroySwapchain(RenSwapchain *swapchain);

void ren_GetSwapchainSize(const RenSwapchain *swapchain, unsigned *p_width,
                          unsigned *p_height);

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height);

REN_NODISCARD RenResult ren_CreateScene(RenDevice *device, RenScene **p_scene);

void ren_DestroyScene(RenScene *scene);

REN_NODISCARD RenResult ren_DrawScene(RenScene *scene, RenSwapchain *swapchain);

REN_NODISCARD RenResult ren_SetSceneCamera(RenScene *scene,
                                           const RenCameraDesc *desc);

REN_NODISCARD RenResult ren_SetSceneTonemapping(RenScene *scene,
                                                RenTonemappingOperator oper);

REN_NODISCARD RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                                       RenMesh *p_mesh);

REN_NODISCARD RenResult ren_CreateImage(RenScene *scene,
                                        const RenImageDesc *desc,
                                        RenImage *p_image);

REN_NODISCARD RenResult ren_CreateMaterials(RenScene *scene,
                                            const RenMaterialDesc *desc,
                                            size_t count,
                                            RenMaterial *materials);

REN_NODISCARD REN_INLINE RenResult ren_CreateMaterial(
    RenScene *scene, const RenMaterialDesc *desc, RenMaterial *p_material) {
  return ren_CreateMaterials(scene, desc, 1, p_material);
}

REN_NODISCARD RenResult ren_CreateMeshInsts(RenScene *scene,
                                            const RenMeshInstDesc *descs,
                                            size_t count, RenMeshInst *insts);

REN_NODISCARD REN_INLINE RenResult ren_CreateMeshInst(
    RenScene *scene, const RenMeshInstDesc *desc, RenMeshInst *p_inst) {
  return ren_CreateMeshInsts(scene, desc, 1, p_inst);
}

void ren_DestroyMeshInsts(RenScene *scene, const RenMeshInst *insts,
                          size_t count);

REN_INLINE void ren_DestroyMeshInst(RenScene *scene, RenMeshInst inst) {
  ren_DestroyMeshInsts(scene, &inst, 1);
}

void ren_SetMeshInstMatrices(RenScene *scene, const RenMeshInst *insts,
                             const RenMatrix4x4 *matrices, size_t count);

REN_INLINE void ren_SetMeshInstMatrix(RenScene *scene, RenMeshInst inst,
                                      const RenMatrix4x4 *matrix) {
  ren_SetMeshInstMatrices(scene, &inst, matrix, 1);
}

REN_NODISCARD RenResult ren_CreateDirLights(RenScene *scene,
                                            const RenDirLightDesc *descs,
                                            size_t count, RenDirLight *lights);

REN_NODISCARD REN_INLINE RenResult ren_CreateDirLight(
    RenScene *scene, const RenDirLightDesc *desc, RenDirLight *p_light) {
  return ren_CreateDirLights(scene, desc, 1, p_light);
}

void ren_DestroyDirLights(RenScene *scene, const RenDirLight *lights,
                          size_t count);

REN_INLINE void ren_DestroyDirLight(RenScene *scene, RenDirLight light) {
  ren_DestroyDirLights(scene, &light, 1);
}

REN_NODISCARD RenResult ren_ConfigDirLights(RenScene *scene,
                                            const RenDirLight *lights,
                                            const RenDirLightDesc *descs,
                                            size_t count);

REN_NODISCARD REN_INLINE RenResult ren_ConfigDirLight(
    RenScene *scene, RenDirLight light, const RenDirLightDesc *desc) {
  return ren_ConfigDirLights(scene, &light, desc, 1);
}

REN_NODISCARD RenResult ren_CreatePointLights(RenScene *scene,
                                              const RenPointLightDesc *descs,
                                              size_t count,
                                              RenPointLight *lights);

REN_NODISCARD REN_INLINE RenResult ren_CreatePointLight(
    RenScene *scene, const RenPointLightDesc *desc, RenPointLight *p_light) {
  return ren_CreatePointLights(scene, desc, 1, p_light);
}

void ren_DestroyPointLights(RenScene *scene, const RenPointLight *lights,
                            size_t count);

REN_INLINE void ren_DestroyPointLight(RenScene *scene, RenPointLight light) {
  ren_DestroyPointLights(scene, &light, 1);
}

REN_NODISCARD RenResult ren_ConfigPointLights(RenScene *scene,
                                              const RenPointLight *lights,
                                              const RenPointLightDesc *descs,
                                              size_t count);

REN_NODISCARD REN_INLINE RenResult ren_ConfigPointLight(
    RenScene *scene, RenPointLight light, const RenPointLightDesc *desc) {
  return ren_ConfigPointLights(scene, &light, desc, 1);
}

REN_NODISCARD RenResult ren_CreateSpotLights(RenScene *scene,
                                             const RenSpotLightDesc *descs,
                                             size_t count,
                                             RenSpotLight *lights);

REN_NODISCARD REN_INLINE RenResult ren_CreateSpotLight(
    RenScene *scene, const RenSpotLightDesc *desc, RenSpotLight *p_light) {
  return ren_CreateSpotLights(scene, desc, 1, p_light);
}

void ren_DestroySpotLights(RenScene *scene, const RenSpotLight *lights,
                           size_t count);

REN_INLINE void ren_DestroySpotLight(RenScene *scene, RenSpotLight light) {
  ren_DestroySpotLights(scene, &light, 1);
}

REN_NODISCARD RenResult ren_ConfigSpotLights(RenScene *scene,
                                             const RenSpotLight *lights,
                                             const RenSpotLightDesc *descs,
                                             size_t count);

REN_NODISCARD REN_INLINE RenResult ren_ConfigSpotLight(
    RenScene *scene, RenSpotLight light, const RenSpotLightDesc *desc) {
  return ren_ConfigSpotLights(scene, &light, desc, 1);
}

#ifdef __cplusplus
}
#endif

#undef REN_DEFAULT_VALUE
#undef REN_INLINE
#undef REN_NODISCARD
