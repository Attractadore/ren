#pragma once
#include "StdDef.hpp"

namespace ren {

void *vm_allocate(usize size);

void vm_commit(void *ptr, usize size);

void vm_free(void *ptr, usize size);

} // namespace ren
