#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RenDevice RenDevice;
typedef struct RenScene RenScene;
typedef struct RenSwapchain RenSwapchain;
typedef uint64_t RenCameraID;

void ren_DestroyDevice(RenDevice *device);

RenScene *ren_CreateScene(RenDevice *device);
void ren_DestroyScene(RenScene *scene);

void ren_SetScenePipelineDepth(RenScene *scene, unsigned pipeline_depth);
unsigned ren_GetScenePipelineDepth(const RenScene *scene);

void ren_SetSceneOutputSize(RenScene *scene, unsigned width, unsigned height);
unsigned ren_GetSceneOutputWidth(const RenScene *scene);
unsigned ren_GetSceneOutputHeight(const RenScene *scene);

void ren_DrawScene(RenScene *scene);

RenCameraID ren_CreateCamera(RenScene *scene);
void ren_DestroyCamera(RenScene *scene, RenCameraID camera);

RenCameraID ren_GetSceneDefaultCamera(const RenScene *scene);
void ren_SetSceneCamera(RenScene *scene, RenCameraID camera);
RenCameraID ren_GetSceneCamera(const RenScene *scene);

void ren_SetSceneSwapchain(RenScene *scene, RenSwapchain *swapchain);
RenSwapchain *ren_GetSceneSwapchain(const RenScene *scene);

void ren_DestroySwapchain(RenSwapchain *swapchain);

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height);
unsigned ren_GetSwapchainWidth(const RenSwapchain *swapchain);
unsigned ren_GetSwapchainHeight(const RenSwapchain *swapchain);

#ifdef __cplusplus
}
#endif
