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
typedef uint32_t RenMesh;
typedef uint32_t RenMaterial;
typedef uint32_t RenModel;

REN_NODISCARD RenResult ren_DeviceBeginFrame(RenDevice *device);

REN_NODISCARD RenResult ren_DeviceEndFrame(RenDevice *device);

void ren_DestroyDevice(RenDevice *device);

void ren_DestroySwapchain(RenSwapchain *swapchain);

void ren_GetSwapchainSize(const RenSwapchain *swapchain, unsigned *p_width,
                          unsigned *p_height);

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height);

REN_NODISCARD RenResult ren_CreateScene(RenDevice *device, RenScene **p_scene);

void ren_DestroyScene(RenScene *scene);

REN_NODISCARD RenResult ren_SceneBeginFrame(RenScene *scene,
                                            RenSwapchain *swapchain);

REN_NODISCARD RenResult ren_SetSceneOutputSize(RenScene *scene, unsigned width,
                                               unsigned height);

RenResult ren_SceneEndFrame(RenScene *scene);

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
  float position[3];
  float forward[3];
  float up[3];
} RenCameraDesc;

void ren_SetSceneCamera(RenScene *scene, const RenCameraDesc *desc);

typedef struct {
  unsigned num_vertices;
  unsigned num_indices;
  const float *positions;
  const float *colors;
  const unsigned *indices;
} RenMeshDesc;

REN_NODISCARD RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                                       RenMesh *p_mesh);
void ren_DestroyMesh(RenScene *scene, RenMesh mesh);

typedef enum {
  REN_MATERIAL_ALBEDO_CONST,
  REN_MATERIAL_ALBEDO_VERTEX,
} RenMaterialAlbedo;

typedef struct {
  RenMaterialAlbedo albedo;
  union {
    float const_albedo[3];
  };
} RenMaterialDesc;

REN_NODISCARD RenResult ren_CreateMaterial(RenScene *scene,
                                           const RenMaterialDesc *desc,
                                           RenMaterial *p_material);

void ren_DestroyMaterial(RenScene *scene, RenMaterial material);

typedef struct {
  RenMesh mesh;
  RenMaterial material;
} RenModelDesc;

REN_NODISCARD RenResult ren_CreateModel(RenScene *scene,
                                        const RenModelDesc *desc,
                                        RenModel *p_model);

void ren_DestroyModel(RenScene *scene, RenModel model);

void ren_SetModelMatrix(RenScene *scene, RenModel model,
                        const float matrix[16]);

#ifdef __cplusplus
}
#endif
