#if _WIN32
#include "ren/core/Assert.hpp"
#include "ren/core/Vm.hpp"

#include <Windows.h>

namespace ren {

void *vm_allocate(usize size) {
  return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

void vm_commit(void *ptr, usize size) {
  bool success = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
  ren_assert(success);
}

void vm_free(void *ptr, usize size) {
  bool success = VirtualFree(ptr, 0, MEM_RELEASE);
  ren_assert(success);
}

} // namespace ren
#endif
