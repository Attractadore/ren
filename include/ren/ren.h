#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RenDevice RenDevice;
typedef struct RenSwapchain RenSwapchain;
typedef struct RenScene RenScene;
typedef uint32_t RenMesh;
typedef uint32_t RenMaterial;
typedef uint32_t RenModel;

void ren_DestroyDevice(RenDevice *device);

void ren_DestroySwapchain(RenSwapchain *swapchain);

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height);

RenScene *ren_CreateScene(RenDevice *device);
void ren_DestroyScene(RenScene *scene);

void ren_SetSceneOutputSize(RenScene *scene, unsigned width, unsigned height);

void ren_DrawScene(RenScene *scene);

void ren_SetSceneSwapchain(RenScene *scene, RenSwapchain *swapchain);

typedef enum {
  REN_PROJECTION_PERSPECTIVE = 0,
  REN_PROJECTION_ORTHOGRAPHIC,
} RenProjection;

typedef struct {
  float hfov;
} RenPerspectiveCameraDesc;

typedef struct {
  float width;
} RenOrthographicCameraDesc;

typedef struct {
  RenProjection type;
  union {
    RenPerspectiveCameraDesc perspective;
    RenOrthographicCameraDesc orthographic;
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

RenMesh ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc);
void ren_DestroyMesh(RenScene *scene, RenMesh mesh);

typedef enum {
  REN_MATERIAL_ALBEDO_CONST,
  REN_MATERIAL_ALBEDO_VERTEX,
} RenMaterialAlbedo;

typedef struct {
  RenMaterialAlbedo albedo_type;
  union {
    float albedo_color[3];
  };
} RenMaterialDesc;

RenMaterial ren_CreateMaterial(RenScene *scene, const RenMaterialDesc *desc);
void ren_DestroyMaterial(RenScene *scene, RenMaterial material);

typedef struct {
  RenMesh mesh;
  RenMaterial material;
} RenModelDesc;

RenModel ren_CreateModel(RenScene *scene, const RenModelDesc *desc);
void ren_DestroyModel(RenScene *scene, RenModel model);

void ren_SetModelMatrix(RenScene *scene, RenModel model,
                        const float matrix[16]);

#ifdef __cplusplus
}
#endif
