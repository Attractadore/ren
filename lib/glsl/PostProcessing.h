#ifndef REN_GLSL_POST_PROCESS_H
#define REN_GLSL_POST_PROCESS_H

#include "Common.h"
#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

struct PostProcessingArgs {
  GLSL_PTR(LuminanceHistogram) histogram;
  Texture2D previous_exposure;
  Texture2D hdr;
  StorageTexture2D sdr;
};

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;

GLSL_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_H
