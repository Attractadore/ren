#ifndef REN_GLSL_POST_PROCESS_PASS_H
#define REN_GLSL_POST_PROCESS_PASS_H

#include "Common.h"
#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

struct PostProcessingPassArgs {
  GLSL_PTR(LuminanceHistogram) histogram;
  StorageTexture2D previous_exposure;
  StorageTexture2D hdr;
  RWStorageTexture2D sdr;
};

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;

GLSL_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_PASS_H
