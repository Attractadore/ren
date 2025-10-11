#include "IO.hpp"
#include "ren/core/Assert.hpp"

#include <fmt/base.h>
#include <fmt/std.h>

#if _WIN32
#include <windows.h>
#endif

namespace ren {

auto to_system_path(const fs::path &path) -> String {
#if _WIN32
  static const LPSTR (*wine_get_unix_file_name)(LPCWSTR) =
      (decltype(wine_get_unix_file_name))GetProcAddress(
          GetModuleHandleA("KERNEL32"), "wine_get_unix_file_name");
  if (wine_get_unix_file_name) {
    return wine_get_unix_file_name(path.c_str());
  }
#endif
  return path.string();
}

auto fopen(const fs::path &p, const char *mode) -> FILE * {
#if _WIN32
  wchar_t wmode[64];
  usize i = 0;
  do {
    ren_assert(i < std::size(wmode));
    wmode[i] = mode[i];
  } while (mode[i++]);
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
