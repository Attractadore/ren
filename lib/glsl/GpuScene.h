#ifndef REN_GLSL_GPU_SCENE_H
#define REN_GLSL_GPU_SCENE_H

#include "Std.h"

GLSL_NAMESPACE_BEGIN

#define glsl_MeshInstanceVisibilityMask uint32_t
const uint MESH_INSTANCE_VISIBILITY_MASK_SIZE = 4;
static_assert(sizeof(glsl_MeshInstanceVisibilityMask) ==
              MESH_INSTANCE_VISIBILITY_MASK_SIZE);
const uint MESH_INSTANCE_VISIBILITY_MASK_BIT_SIZE =
    MESH_INSTANCE_VISIBILITY_MASK_SIZE * 8;

GLSL_NAMESPACE_END

#endif // REN_GLSL_GPU_SCENE_H
