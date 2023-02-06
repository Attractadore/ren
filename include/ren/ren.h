#pragma once
#include <stdint.h>

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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  REN_SUCCESS = 0,
  REN_VULKAN_ERROR,
  REN_SYSTEM_ERROR,
  REN_RUNTIME_ERROR,
  REN_UNKNOWN_ERROR,
} RenResult;

typedef struct RenDevice RenDevice;
typedef struct RenSwapchain RenSwapchain;
typedef struct RenScene RenScene;
typedef enum : uint32_t {
  REN_NULL_MESH = 0,
} RenMesh;
typedef enum : uint32_t {
  REN_NULL_MATERIAL = 0,
} RenMaterial;
typedef enum : uint32_t {
  REN_NULL_MESH_INSTANCE = 0,
} RenMeshInstance;
typedef enum : uint32_t {
  REN_NULL_DIRECTIONAL_LIGHT = 0,
} RenDirectionalLight;
typedef float RenVector3[3];
typedef float RenVector4[4];
typedef float RenMatrix4x4[16];

void ren_DestroyDevice(RenDevice *device);

void ren_DestroySwapchain(RenSwapchain *swapchain);

void ren_GetSwapchainSize(const RenSwapchain *swapchain, unsigned *p_width,
                          unsigned *p_height);

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height);

REN_NODISCARD RenResult ren_CreateScene(RenDevice *device, RenScene **p_scene);

void ren_DestroyScene(RenScene *scene);

REN_NODISCARD RenResult ren_SetViewport(RenScene *scene, unsigned width,
                                        unsigned height);

typedef enum {
  REN_PROJECTION_PERSPECTIVE = 0,
  REN_PROJECTION_ORTHOGRAPHIC,
} RenProjection;

typedef struct {
  float hfov;
} RenPerspectiveProjection;

typedef struct {
  float width;
} RenOrthographicProjection;

typedef struct {
  RenProjection projection;
  union {
    RenPerspectiveProjection perspective;
    RenOrthographicProjection orthographic;
  };
  RenVector3 position;
  RenVector3 forward;
  RenVector3 up;
} RenCameraDesc;

void ren_SetSceneCamera(RenScene *scene, const RenCameraDesc *desc);

REN_NODISCARD RenResult ren_DrawScene(RenScene *scene, RenSwapchain *swapchain);

typedef struct {
  unsigned num_vertices;
  unsigned num_indices;
  // Mesh positions, required
  const RenVector3 *positions;
  // Mesh normals, required
  const RenVector3 *normals;
  // Mesh vertex colors, optional
  const RenVector3 *colors;
  // Mesh indices, required
  const unsigned *indices;
} RenMeshDesc;

REN_NODISCARD RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                                       RenMesh *p_mesh);
void ren_DestroyMesh(RenScene *scene, RenMesh mesh);

typedef enum {
  REN_MATERIAL_COLOR_CONST,
  REN_MATERIAL_COLOR_VERTEX,
} RenMaterialColor;

typedef struct {
  RenMaterialColor color;
  RenVector4 base_color;
  float metallic;
  float roughness;
} RenMaterialDesc;

REN_NODISCARD RenResult ren_CreateMaterial(RenScene *scene,
                                           const RenMaterialDesc *desc,
                                           RenMaterial *p_material);

void ren_DestroyMaterial(RenScene *scene, RenMaterial material);

typedef struct {
  RenMesh mesh;
  RenMaterial material;
} RenMeshInstanceDesc;

REN_NODISCARD RenResult ren_CreateMeshInstance(RenScene *scene,
                                               const RenMeshInstanceDesc *desc,
                                               RenMeshInstance *p_model);

void ren_DestroyMeshInstance(RenScene *scene, RenMeshInstance model);

void ren_SetMeshInstanceMatrix(RenScene *scene, RenMeshInstance model,
                               const RenMatrix4x4 *matrix);

typedef struct {
  RenVector3 color;
  /// Intensity in lux
  float illuminance;
  /// Direction from an object to the light
  RenVector3 origin;
} RenDirectionalLightDesc;

REN_NODISCARD RenResult
ren_CreateDirectionalLight(RenScene *scene, const RenDirectionalLightDesc *desc,
                           RenDirectionalLight *p_light);

void ren_DestroyDirectionalLight(RenScene *scene, RenDirectionalLight light);

void ren_SetDirectionalLightColor(RenScene *scene, RenDirectionalLight light,
                                  RenVector3 color);

void ren_SetDirectionalLightIntencity(RenScene *scene,
                                      RenDirectionalLight light,
                                      float intensity);

void ren_SetDirectionalLightOrigin(RenScene *scene, RenDirectionalLight light,
                                   RenVector3 origin);

#ifdef __cplusplus
}
#endif
