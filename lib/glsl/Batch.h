#ifndef REN_GLSL_BATCH_H
#define REN_GLSL_BATCH_H

#include "Common.h"
#include "Mesh.h"

GLSL_NAMESPACE_BEGIN

struct Batch {
  uint id;
};

const uint MAX_NUM_BATCHES = NUM_MESH_ATTRIBUTE_FLAGS * MAX_NUM_INDEX_POOLS;

inline Batch make_batch(uint attribute_mask, uint pool) {
  Batch batch;
  batch.id = (attribute_mask << MAX_NUM_INDEX_POOL_BITS) | pool;
  return batch;
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_BATCH_H
