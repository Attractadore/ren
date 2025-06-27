#include "IO.hpp"
#include "String.hpp"
#include "Views.hpp"

#include <fmt/ostream.h>
#include <fmt/std.h>

namespace ren {

namespace {
auto stringify(const u8 *data, usize size) -> String {
  constexpr usize LINE_WIDTH = 32;
  constexpr usize SYM_LENGTH = 6;
  String s;
  s.resize(size * SYM_LENGTH);
  constexpr char REMAP[] = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
  };
  usize b = 0;
  usize c = 0;
  while (b < size) {
    usize len = std::min(size - b, LINE_WIDTH);
    for (usize k : range(len)) {
      s[c++] = '0';
      s[c++] = 'x';
      s[c++] = REMAP[(data[b] >> 4)];
      s[c++] = REMAP[data[b] & 0xf];
      s[c++] = ',';
      s[c++] = ' ';
      b++;
    }
    s[c - 1] = '\n';
  }
  ren_assert(c == s.size());
  return s;
}
} // namespace

auto fopen(const fs::path &p, const char *mode) -> FILE * {
  wchar_t wmode[64];
  std::mbtowc(wmode, mode, std::size(wmode));
#if _WIN64
  return _wfopen(p.c_str(), wmode);
#else
  return std::fopen(p.c_str(), mode);
#endif
}

auto write_to_file(const void *data, usize size, const fs::path &p)
    -> Result<void, Error> {
  FILE *f = fopen(p, "wb");
  if (!f) {
    fmt::println(stderr, "Failed to open {} for writing", p);
    return Failure(Error::IO);
  }
  usize count = std::fwrite(data, 1, size, f);
  std::fclose(f);
  if (size != count) {
    return Failure(Error::IO);
  }
  return {};
}

auto stringify_and_write_to_files(const void *data, usize size,
                                  const fs::path &path) -> Result<void, Error> {
  fs::path header_path = fmt::format("{}.inc", path);
  auto header = stringify((const u8 *)data, size);

  fs::path parent_dir = path.parent_path();
  if (not fs::exists(parent_dir)) {
    fs::create_directory(parent_dir);
  }

  std::ofstream f(path, std::ios::binary);
  if (!f) {
    fmt::println(stderr, "Failed to open {} for writing", path);
    return Failure(Error::IO);
  }
  f.write((const char *)data, size);
  if (!f) {
    fmt::println(stderr, "Failed to open write {}", path);
    return Failure(Error::IO);
  }
  f.close();

  f.open(header_path, std::ios::binary);
  if (!f) {
    fmt::println(stderr, "Failed to open {} for writing", header_path);
    return Failure(Error::IO);
  }
  f.write(header.data(), header.size());
  if (!f) {
    fmt::println(stderr, "Failed to open write {}", header_path);
    return Failure(Error::IO);
  }

  return {};
}

} // namespace ren
