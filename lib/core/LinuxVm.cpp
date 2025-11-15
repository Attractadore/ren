#if __linux__
#include "ren/core/Assert.hpp"
#include "ren/core/Vm.hpp"

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

namespace ren {

auto vm_allocate(usize size) -> void * {
  void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return ptr == MAP_FAILED ? nullptr : ptr;
}

void vm_commit(void *, usize) {}

void vm_free(void *ptr, usize size) {
  int ret = munmap(ptr, size);
  ren_assert(ret == 0);
}

void vm_protect(void *ptr, usize size, PagePermissionFlags permission) {
  int prot = 0;
  if (permission.is_set(PagePermission::Read)) {
    prot |= PROT_READ;
  }
  if (permission.is_set(PagePermission::Write)) {
    prot |= PROT_WRITE;
  }
  if (permission.is_set(PagePermission::Execute)) {
    prot |= PROT_EXEC;
  }
  mprotect(ptr, size, prot);
}

usize vm_page_size() {
  errno = 0;
  usize page_size = sysconf(_SC_PAGESIZE);
  ren_assert(errno == 0);
  return page_size;
}

usize vm_allocation_granularity() { return vm_page_size(); }

} // namespace ren

#endif
