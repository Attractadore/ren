#if __linux__
#include "ren/core/Assert.hpp"
#include "ren/core/Vm.hpp"

#include <sys/mman.h>

namespace ren {

auto vm_allocate(usize size) -> void * {
  return mmap(nullptr, size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void vm_commit(void *, usize) {}

void vm_free(void *ptr, usize size) {
  int ret = munmap(ptr, size);
  ren_assert(ret == 0);
}

} // namespace ren

#endif
