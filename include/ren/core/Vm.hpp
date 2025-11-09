#pragma once
#include "Flags.hpp"
#include "StdDef.hpp"

namespace ren {

REN_BEGIN_FLAGS_ENUM(PagePermission){
    Read,
    Write,
    Execute,
} REN_END_FLAGS_ENUM(PagePermission);

}

REN_ENABLE_FLAGS(ren::PagePermission);

namespace ren {

using PagePermissionFlags = Flags<PagePermission>;

constexpr PagePermissionFlags PagePermissionNone = {};

void *vm_allocate(usize size);

void vm_commit(void *ptr, usize size);

void vm_free(void *ptr, usize size);

void vm_protect(void *ptr, usize size, PagePermissionFlags permission);

usize vm_page_size();

} // namespace ren
