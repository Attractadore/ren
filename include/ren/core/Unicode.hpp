#pragma once
#include "StdDef.hpp"
#include "String.hpp"

namespace ren {

struct Utf16Char {
  u16 value = 0;
};

struct Utf32Char {
  u32 value = 0;
};

inline bool is_high_surrogate(Utf16Char cu) {
  return cu.value >= 0xD800 and cu.value <= 0xDBFF;
}

inline bool is_low_surrogate(Utf16Char cu) {
  return cu.value >= 0xDC00 and cu.value <= 0xDFFF;
}

inline Utf32Char to_utf32(Utf16Char hi, Utf16Char lo) {
  ren_assert(is_high_surrogate(hi));
  ren_assert(is_low_surrogate(lo));
  Utf32Char cu;
  cu.value = ((hi.value - 0xD800) << 10) | (lo.value - 0xDC00);
  return cu;
}

inline Utf32Char to_utf32(Utf16Char cu) { return {cu.value}; }

inline void to_utf8(Utf32Char cu, NotNull<StringBuilder *> builder) {
  if (cu.value < 0x0080) {
    builder->push(cu.value);
  }
  if (cu.value < 0x2080) {
    builder->push(0x80 | ((cu.value >> 7) & 0x3F));
    builder->push(0x80 | ((cu.value >> 0) & 0x7F));
  }
  if (cu.value < 0x82080) {
    builder->push(0xC0 | ((cu.value >> 14) & 0x1F));
    builder->push(0x80 | ((cu.value >> 7) & 0x7F));
    builder->push(0x80 | ((cu.value >> 0) & 0x7F));
  }
  if (cu.value < 0x2082080) {
    builder->push(0xE0 | ((cu.value >> 21) & 0x0F));
    builder->push(0x80 | ((cu.value >> 14) & 0x7F));
    builder->push(0x80 | ((cu.value >> 7) & 0x7F));
    builder->push(0x80 | ((cu.value >> 0) & 0x7F));
  }
  builder->push(0xF0 | ((cu.value >> 28) & 0x07));
  builder->push(0x80 | ((cu.value >> 21) & 0x7F));
  builder->push(0x80 | ((cu.value >> 14) & 0x7F));
  builder->push(0x80 | ((cu.value >> 7) & 0x7F));
  builder->push(0x80 | ((cu.value >> 0) & 0x7F));
}

} // namespace ren
