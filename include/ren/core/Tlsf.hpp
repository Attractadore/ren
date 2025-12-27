#pragma once
#include "Arena.hpp"
#include "List.hpp"
#include "StdDef.hpp"

// http://www.gii.upv.es/tlsf/files/papers/ecrts04_tlsf.pdf
// http://www.gii.upv.es/tlsf/files/papers/tlsf_desc.pdf

namespace ren {

constexpr usize MIN_TLSF_ALLOCATION_SIZE = 8;

struct TlsfAllocation {
  usize size = 0;
  usize offset = 0;
  union {
    ListNode<TlsfAllocation> physical_list = {};
    struct {
      TlsfAllocation *prev;
      TlsfAllocation *next;
    };
  };
  union {
    ListNode<TlsfAllocation> free_list = {};
    TlsfAllocation *next_free;
  };
};

struct TlsfAllocator {
  ListNode<TlsfAllocation> m_physical_list;
  ListNode<TlsfAllocation> m_class_free_lists[32][8] = {};
  TlsfAllocation *m_free_list = nullptr;
};

[[nodiscard]] NotNull<TlsfAllocator *> tlsf_init(NotNull<Arena *> arena,
                                                 usize size);

[[nodiscard]] TlsfAllocation *tlsf_allocate(NotNull<Arena *> arena,
                                            NotNull<TlsfAllocator *> allocator,
                                            usize size);

void tlsf_free(NotNull<TlsfAllocator *> allocator, TlsfAllocation *allocation);

void tlsf_expand(NotNull<Arena *> arena, NotNull<TlsfAllocator *> allocator,
                 usize new_size);

} // namespace ren
