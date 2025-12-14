#if _WIN32
#include "Win32.hpp"

#include <cwchar>

namespace ren {

const wchar_t *utf8_to_path(NotNull<Arena *> arena, String8 str) {
  int wlen = MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, nullptr, 0);
  ren_assert(wlen > 0);
  wchar_t *wbuf = arena->allocate<wchar_t>(wlen + 1);
  int res = MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, wbuf, wlen);
  ren_assert(res == wlen);
  wbuf[wlen] = 0;
  return wbuf;
}

const wchar_t *utf8_to_raw_path(NotNull<Arena *> arena, String8 str,
                                const wchar_t *suffix) {
  int wlen = MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, nullptr, 0);
  int suflen = suffix ? std::wcslen(suffix) : 0;
  ren_assert(wlen > 0);
  wchar_t *wbuf = arena->allocate<wchar_t>(4 + wlen + suflen + 1);
  wbuf[0] = L'\\';
  wbuf[1] = L'\\';
  wbuf[2] = L'?';
  wbuf[3] = L'\\';
  int res =
      MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, wbuf + 4, wlen);
  ren_assert(res == wlen);
  if (suffix) {
    copy(suffix, suflen, &wbuf[4 + wlen]);
  }
  wbuf[4 + wlen + suflen] = 0;
  return wbuf;
}

String8 wcs_to_utf8(NotNull<Arena *> arena, const wchar_t *wcs) {
  return wcs_to_utf8(arena, {wcs, std::wcslen(wcs)});
}

String8 wcs_to_utf8(NotNull<Arena *> arena, Span<const wchar_t> wcs) {
  int len = WideCharToMultiByte(CP_UTF8, 0, wcs.m_data, wcs.m_size, nullptr, 0,
                                nullptr, nullptr);
  ren_assert(len > 0);
  char *buf = arena->allocate<char>(len);
  int res = WideCharToMultiByte(CP_UTF8, 0, wcs.m_data, wcs.m_size, buf, len,
                                nullptr, nullptr);
  ren_assert(res == len);
  return String8(buf, len);
}

} // namespace ren

#endif
