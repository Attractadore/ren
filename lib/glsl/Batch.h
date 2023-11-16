#ifndef REN_GLSL_BATCH_H
#define REN_GLSL_BATCH_H

#include "Mesh.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

struct Batch {
  uint id;
};

const uint NUM_BATCHES = NUM_MESH_ATTRIBUTE_FLAGS * MAX_NUM_VERTEX_POOLS;

inline Batch make_batch(uint attribute_mask, uint pool) {
  Batch batch;
  batch.id = (attribute_mask << 8) | pool;
  return batch;
}

inline uint get_batch_id(uint attribute_mask, uint pool) {
  return make_batch(attribute_mask, pool).id;
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_BATCH_H
