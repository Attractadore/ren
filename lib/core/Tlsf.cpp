#include "ren/core/Tlsf.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Math.hpp"

namespace ren {

struct TlsfSizeMapping {
  usize fli;
  usize sli;
};

static TlsfSizeMapping tlsf_mapping(usize size) {
  usize fli = find_msb(size) - 3;
  usize sli = (size >> fli) & 7;
  return {fli, sli};
}

static void tlsf_insert(NotNull<TlsfAllocator *> allocator,
                        NotNull<TlsfAllocation *> allocation) {
  auto [fli, sli] = tlsf_mapping(allocation->size);
  list_insert_after(&allocator->m_class_free_lists[fli][sli],
                    &allocation->free_list);
}

NotNull<TlsfAllocator *> tlsf_init(NotNull<Arena *> arena, usize size) {
  size = max(size, MIN_TLSF_ALLOCATION_SIZE);
  auto *allocator = arena->allocate<TlsfAllocator>();
  list_init(&allocator->m_physical_list);
  for (usize fli : range(ren::size(allocator->m_class_free_lists))) {
    for (usize sli : range(ren::size(allocator->m_class_free_lists[fli]))) {
      list_init(&allocator->m_class_free_lists[fli][sli]);
    }
  }
  TlsfAllocation *allocation = arena->allocate<TlsfAllocation>();
  *allocation = {.size = size};
  list_insert_after(&allocator->m_physical_list, &allocation->physical_list);
  tlsf_insert(allocator, allocation);
  return allocator;
}

TlsfAllocation *tlsf_allocate(NotNull<Arena *> arena,
                              NotNull<TlsfAllocator *> allocator, usize size) {
  size = max(size, MIN_TLSF_ALLOCATION_SIZE);
  auto [fli, sli] = tlsf_mapping(size);
  sli++;
  for (; fli < ren::size(allocator->m_class_free_lists); ++fli) {
    for (; sli < ren::size(allocator->m_class_free_lists[fli]); ++sli) {
      ListNode<TlsfAllocation> *class_free_list_head =
          &allocator->m_class_free_lists[fli][sli];
      if (list_is_empty(class_free_list_head)) {
        continue;
      }

      TlsfAllocation *allocation =
          container_of(class_free_list_head->next, TlsfAllocation, free_list);
      ren_assert(allocation->size > size);
      list_remove(&allocation->free_list);

      usize remainder_size = allocation->size - size;
      if (remainder_size >= MIN_TLSF_ALLOCATION_SIZE) {
        TlsfAllocation *remainder = allocator->m_free_list;
        if (!remainder) {
          remainder = arena->allocate<TlsfAllocation>();
        } else {
          allocator->m_free_list = remainder->next_free;
        }
        *remainder = {
            .size = remainder_size,
            .offset = allocation->offset + size,
        };
        list_insert_after(&allocation->physical_list,
                          &remainder->physical_list);
        tlsf_insert(allocator, remainder);
      }

      return allocation;
    }
    sli = 0;
  }
  return nullptr;
}

static TlsfAllocation *tlsf_merge(NotNull<TlsfAllocator *> allocator,
                                  NotNull<TlsfAllocation *> left,
                                  NotNull<TlsfAllocation *> right) {
  ren_assert(left->offset + left->size == right->offset);
  left->size += right->size;
  list_remove(&left->free_list);
  list_remove(&right->free_list);
  list_remove(&right->physical_list);
  right->next_free = allocator->m_free_list;
  allocator->m_free_list = right->next_free;
  tlsf_insert(allocator, left);
  return left;
}

void tlsf_free(NotNull<TlsfAllocator *> allocator, TlsfAllocation *allocation) {
  [[unlikely]] if (!allocation) { return; }
  if (allocation->prev->offset < allocation->offset and
      is_in_list(allocation->free_list)) {
    allocation = tlsf_merge(allocator, allocation->prev, allocation);
  }
  if (allocation->next->offset > allocation->offset and
      is_in_list(allocation->free_list)) {
    allocation = tlsf_merge(allocator, allocation, allocation->next);
  }
  tlsf_insert(allocator, allocation);
}

void tlsf_expand(NotNull<Arena *> arena, NotNull<TlsfAllocator *> allocator,
                 usize new_size) {
  TlsfAllocation *last = container_of(allocator->m_physical_list.prev,
                                      TlsfAllocation, physical_list);
  usize old_size = last->offset + last->size;
  ren_assert(old_size < new_size);
  TlsfAllocation *allocation = allocator->m_free_list;
  if (!allocation) {
    allocation = arena->allocate<TlsfAllocation>();
  } else {
    allocator->m_free_list = allocation->next_free;
  }
  *allocation = {.size = new_size, .offset = old_size};
  tlsf_insert(allocator, allocation);
}

} // namespace ren
