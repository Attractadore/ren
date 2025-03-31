#include "IO.hpp"

#include <fmt/ostream.h>
#include <fmt/std.h>

namespace ren {

auto fopen(const fs::path &p, const char *mode) -> FILE * {
  wchar_t wmode[64];
  std::mbtowc(wmode, mode, std::size(wmode));
#if _WIN64
  return _wfopen(p.c_str(), wmode);
#else
  return std::fopen(p.c_str(), mode);
#endif
}

auto write_to_file(void *data, usize size, fs::path &p) -> Result<void, Error> {
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

} // namespace ren
