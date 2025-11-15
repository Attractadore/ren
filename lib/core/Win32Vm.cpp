#if _WIN32
#include "Win32.hpp"
#include "ren/core/Vm.hpp"

#include <Windows.h>

namespace ren {

void *vm_allocate(usize size) {
  usize granularity = vm_allocation_granularity();
  if (size < granularity) {
    fmt::println(
        "vm: allocation size {} is less than allocation granularity {}", size,
        granularity);
  }
  return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

void vm_commit(void *ptr, usize size) {
  WIN32_CHECK(VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE));
}

void vm_free(void *ptr, usize size) { VirtualFree(ptr, 0, MEM_RELEASE); }

void vm_protect(void *ptr, usize size, PagePermissionFlags permission) {
  DWORD protect = PAGE_NOACCESS;
  if (permission.is_set(PagePermission::Execute)) {
    if (permission.is_set(PagePermission::Write)) {
      protect = PAGE_EXECUTE_READWRITE;
    } else if (permission.is_set(PagePermission::Read)) {
      protect = PAGE_EXECUTE_READ;
    } else {
      protect = PAGE_EXECUTE;
    }
  } else if (permission.is_set(PagePermission::Write)) {
    protect = PAGE_READWRITE;
  } else if (permission.is_set(PagePermission::Read)) {
    protect = PAGE_READONLY;
  }
  DWORD old_protect;
  WIN32_CHECK(VirtualProtect(ptr, size, protect, &old_protect));
}

usize vm_page_size() {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
}

usize vm_allocation_granularity() {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwAllocationGranularity;
}

} // namespace ren
#endif
