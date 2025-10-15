#include "IO.hpp"
#include "ren/core/Assert.hpp"

#include <fmt/base.h>
#include <fmt/std.h>

#if _WIN32
#include <windows.h>
#endif

namespace ren {

auto to_system_path(NotNull<Arena *> arena, const fs::path &path) -> String8 {
#if _WIN32
  static const LPSTR (*wine_get_unix_file_name)(LPCWSTR) =
      (decltype(wine_get_unix_file_name))GetProcAddress(
          GetModuleHandleA("KERNEL32"), "wine_get_unix_file_name");
  if (wine_get_unix_file_name) {
    const char *str = wine_get_unix_file_name(path.c_str());
    usize len = std::strlen(str);
    char *buf = allocate<char>(arena, len);
    std::memcpy(buf, str, len);
    return {buf, len};
  }
  int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), path.native().size(),
                                nullptr, 0, nullptr, nullptr);
  ren_assert(len > 0);
  char *buf = allocate<char>(arena, len);
  int res = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), path.native().size(),
                                buf, len, nullptr, nullptr);
  ren_assert(res == len);
  return {buf, (usize)len};
#else
  usize len = path.native().size();
  char *buf = allocate<char>(arena, len);
  std::memcpy(buf, path.c_str(), len);
  return {buf, len};
#endif
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
