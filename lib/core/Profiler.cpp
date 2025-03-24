#include <boost/predef.h>
#include <cstdlib>
#include <new>
#include <tracy/Tracy.hpp>

#ifdef TRACY_ENABLE

constexpr size_t CALLSTACK_DEPTH = 20;

void *operator new(std::size_t count) {
  void *ptr = std::malloc(count);
  TracyAllocS(ptr, count, CALLSTACK_DEPTH);
  return ptr;
}

void operator delete(void *ptr) noexcept {
  TracyFreeS(ptr, CALLSTACK_DEPTH);
  std::free(ptr);
}

#if BOOST_OS_WINDOWS

void *operator new(std::size_t count, std::align_val_t al,
                   const std::nothrow_t &) noexcept {
  void *ptr = _aligned_malloc(count, (size_t)al);
  TracyAllocS(ptr, count, CALLSTACK_DEPTH);
  return ptr;
}

void operator delete(void *ptr, std::align_val_t al) noexcept {
  TracyFreeS(ptr, CALLSTACK_DEPTH);
  _aligned_free(ptr);
}

#else

void *operator new(std::size_t count, std::align_val_t al) {
  void *ptr = std::aligned_alloc((size_t)al, count);
  TracyAllocS(ptr, count, CALLSTACK_DEPTH);
  return ptr;
}

void operator delete(void *ptr, std::align_val_t al) noexcept {
  TracyFreeS(ptr, CALLSTACK_DEPTH);
  std::free(ptr);
}

#endif

#endif
