#pragma once
#include "ren/core/FileSystem.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Optional.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"

namespace ren {

template <usize Bytes> struct alignas(u32) Guid {
  u8 m_data[Bytes] = {};
};

using Guid32 = Guid<4>;
using Guid64 = Guid<8>;
using Guid128 = Guid<16>;

template <usize Bytes>
String8 to_string(NotNull<Arena *> arena, Guid<Bytes> guid) {
  ScratchArena scratch;
  auto builder = StringBuilder::init(scratch);
  for (isize i = isize(Bytes) - 1; i >= 0; --i) {
    const char MAP[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };
    u8 b = guid.m_data[i];
    u8 hi = b >> 4;
    u8 lo = b & 0xf;
    builder.push(MAP[hi]);
    builder.push(MAP[lo]);
  }
  return builder.materialize(arena);
}

template <usize Bytes> Optional<Guid<Bytes>> guid_from_string(String8 str) {
  usize len = Bytes * 2;
  if (str.m_size != len) {
    return {};
  }
  Guid<Bytes> guid = {};
  for (usize i : range(len)) {
    bool is_hi_byte = i % 2 == 0;
    usize byte_index = Bytes - 1 - i / 2;
    char c = str[i];
    u32 value = -1;
    value = c >= '0' and c <= '9' ? value = c - '0' : value;
    value = c >= 'A' and c <= 'F' ? value = c - 'A' + 0xA : value;
    value = c >= 'a' and c <= 'f' ? value = c - 'a' + 0xA : value;
    if (value == -1) {
      return {};
    }
    guid.m_data[byte_index] |= is_hi_byte ? (value << 4) : value;
  }
  return guid;
}

inline Optional<Guid64> guid64_from_string(String8 str) {
  return guid_from_string<8>(str);
}

struct MetaMesh {
  String8 name;
  u32 mesh_id = 0;
  u32 primitive_id = 0;
  Guid64 guid;
};

struct MetaGltf {
  String8 src;
  Span<const MetaMesh> meshes;
};

JsonValue to_json(NotNull<Arena *> arena, MetaGltf scene);

struct MetaGltfErrorInfo {};

String8 to_string(NotNull<Arena *> arena, MetaGltfErrorInfo error);

Result<MetaGltf, MetaGltfErrorInfo> meta_gltf_from_json(NotNull<Arena *> arena,
                                                        JsonValue value);

MetaGltf meta_gltf_generate(NotNull<Arena *> arena, JsonValue gltf,
                            Path filename);

} // namespace ren
