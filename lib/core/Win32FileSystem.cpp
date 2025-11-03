#if _WIN32
#include "ren/core/Algorithm.hpp"
#include "ren/core/FileSystem.hpp"

#include <Windows.h>
#include <bit>
#include <limits>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace ren {

const char Path::SEPARATOR = '\\';

namespace {

const wchar_t *utf8_to_wcs(NotNull<Arena *> arena, String8 str) {
  int wlen = MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, nullptr, 0);
  ren_assert(wlen > 0);
  wchar_t *wbuf = arena->allocate<wchar_t>(wlen + 1);
  int res = MultiByteToWideChar(CP_UTF8, 0, str.m_str, str.m_size, wbuf, wlen);
  ren_assert(res == wlen);
  wbuf[wlen] = 0;
  return wbuf;
}

String8 wcs_to_utf8(NotNull<Arena *> arena, const wchar_t *wcs) {
  int len =
      WideCharToMultiByte(CP_UTF8, 0, wcs, -1, nullptr, 0, nullptr, nullptr);
  ren_assert(len > 0);
  char *buf = arena->allocate<char>(len);
  int res =
      WideCharToMultiByte(CP_UTF8, 0, wcs, -1, buf, len, nullptr, nullptr);
  ren_assert(res == len);
  return String8(buf, len);
}

IoError win32_to_io_error(DWORD err = GetLastError()) {
  ren_assert(err);
  switch (err) {
  default:
    return IoError::Unknown;
  case ERROR_FILE_EXISTS:
    return IoError::Exists;
  case ERROR_ACCESS_DENIED:
    return IoError::Access;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return IoError::NotFound;
  }
}

HANDLE handle_from_file(File file) { return std::bit_cast<HANDLE>(file.m_fd); }

} // namespace

bool is_path(String8 path) {
  if (!path) {
    return true;
  }

  String8 vol = path_volume_name(path);
  if (vol) {
    if (vol.m_size == path.m_size) {
      return false;
    }
    if (path[vol.m_size] != Path::SEPARATOR) {
      return false;
    }
  }
  bool is_abs = path[vol.m_size] == Path::SEPARATOR;
  bool is_root = is_abs and path.m_size == vol.m_size + 1;

  bool prev_sep = false;
  for (usize i : range<usize>(vol.m_size, path.m_size)) {
    char c = path[i];
    if (c == Path::SEPARATOR and prev_sep) {
      return false;
    }
    switch (c) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '|':
    case '?':
    case '*':
      return false;
    }
    prev_sep = c == Path::SEPARATOR;
  }
  return not prev_sep or is_root;
}

Path Path::init(NotNull<Arena *> arena, String8 path) {
  if (!path) {
    return {};
  }

  ScratchArena scratch(arena);
  auto builder = StringBuilder::init(scratch);

  String8 vol = path_volume_name(path);
  if (vol) {
    if (vol.m_size == path.m_size) {
      builder.push(vol);
      builder.push(SEPARATOR);
      return {builder.materialize(arena)};
    }
    if (path[vol.m_size] != SEPARATOR and path[vol.m_size] != '/') {
      vol = {};
    }
  }

  bool is_abs = path[vol.m_size] == Path::SEPARATOR or path[vol.m_size] == '/';
  bool is_root = is_abs and path.m_size == vol.m_size + 1;

  usize i = 0;
  if (vol) {
    builder.push(path[i++]);
    builder.push(path[i++]);
  }

  bool prev_sep = false;
  for (; i < path.m_size; ++i) {
    char c = path[i];
    if (c == '/') {
      c = SEPARATOR;
    }
    switch (c) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '|':
    case '?':
    case '*':
      c = '_';
      break;
    }
    if (c != SEPARATOR or not prev_sep) {
      builder.push(c);
    }
    prev_sep = c == SEPARATOR;
  }
  if (prev_sep and not is_root) {
    builder.pop();
  }

  return {builder.materialize(arena)};
}

String8 Path::native(NotNull<Arena *> arena) const {
  static const LPSTR (*wine_get_unix_file_name)(LPCWSTR) =
      (decltype(wine_get_unix_file_name))GetProcAddress(
          GetModuleHandleA("KERNEL32"), "wine_get_unix_file_name");
  if (wine_get_unix_file_name) {
    ScratchArena scratch(arena);
    const char *str = wine_get_unix_file_name(utf8_to_wcs(scratch, m_str));
    ren_assert(str);
    return String8::init(str).copy(arena);
  }
  return m_str.copy(arena);
}

IoResult<bool> Path::exists() const {
  ScratchArena scratch;
  if (PathFileExistsW(utf8_to_wcs(scratch, m_str))) {
    return true;
  }
  DWORD err = GetLastError();
  if (!err) {
    return false;
  }
  return win32_to_io_error(err);
}

IoResult<Path> current_directory(NotNull<Arena *> arena) {
  DWORD size = GetCurrentDirectoryW(0, nullptr);
  if (size == 0) {
    return win32_to_io_error();
  }
  ScratchArena scratch(arena);
  wchar_t *buffer = scratch->allocate<wchar_t>(size + 1);
  if (!GetCurrentDirectoryW(size + 1, buffer)) {
    return win32_to_io_error();
  }
  return Path::init(arena, wcs_to_utf8(scratch, buffer));
}

IoResult<void> create_directory(Path path) {
  ScratchArena scratch;
  const wchar_t *wcs_path = utf8_to_wcs(scratch, path.m_str);
  if (!CreateDirectoryW(wcs_path, nullptr)) {
    return win32_to_io_error();
  }
  return {};
}

IoResult<u64> last_write_time(Path path) {
  ScratchArena scratch;
  HANDLE hfile =
      CreateFileW(utf8_to_wcs(scratch, path.m_str), 0,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (!hfile) {
    return win32_to_io_error();
  }
  FILETIME time;
  bool success = GetFileTime(hfile, nullptr, nullptr, &time);
  DWORD err = GetLastError();
  CloseHandle(hfile);
  if (!success) {
    return win32_to_io_error();
  }
  return std::bit_cast<u64>(time);
}

IoResult<File> open(Path path, FileAccessMode mode, FileOpenFlags flags) {
  ScratchArena scratch;
  DWORD access = 0;
  switch (mode) {
  case FileAccessMode::ReadOnly:
    access = GENERIC_READ;
    break;
  case FileAccessMode::WriteOnly:
    access = GENERIC_WRITE;
    break;
  case FileAccessMode::ReadWrite:
    access = GENERIC_READ | GENERIC_WRITE;
    break;
  }
  DWORD disposition = 0;
  if (flags.is_set(FileOpen::Create)) {
    disposition =
        flags.is_set(FileOpen::Truncate) ? CREATE_ALWAYS : OPEN_ALWAYS;
  } else {
    disposition =
        flags.is_set(FileOpen::Truncate) ? TRUNCATE_EXISTING : OPEN_EXISTING;
  }
  HANDLE hfile =
      CreateFileW(utf8_to_wcs(scratch, path.m_str), access,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (!hfile) {
    return win32_to_io_error();
  }
  return File{std::bit_cast<uintptr_t>(hfile)};
};

void close(File file) { CloseHandle(handle_from_file(file)); }

IoResult<usize> seek(File file, isize offset, SeekMode mode) {
  DWORD method = 0;
  switch (mode) {
  case SeekMode::Set:
    method = FILE_BEGIN;
    break;
  case SeekMode::End:
    method = FILE_END;
    break;
  case SeekMode::Cur:
    method = FILE_CURRENT;
    break;
  }
  LARGE_INTEGER distance = {.QuadPart = offset};
  LARGE_INTEGER pos;
  if (!SetFilePointerEx(handle_from_file(file), distance, &pos, method)) {
    return win32_to_io_error();
  }
  return pos.QuadPart;
}

IoResult<usize> read(File file, void *buffer, usize size) {
  size = min<usize>(size, std::numeric_limits<DWORD>::max());
  DWORD num_read = 0;
  if (!ReadFile(handle_from_file(file), buffer, size, &num_read, nullptr)) {
    DWORD err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      return num_read;
    }
    return win32_to_io_error(err);
  }
  return num_read;
}

IoResult<usize> write(File file, const void *buffer, usize size) {
  size = min<usize>(size, std::numeric_limits<DWORD>::max());
  DWORD num_write = 0;
  if (!WriteFile(handle_from_file(file), buffer, size, &num_write, nullptr)) {
    return win32_to_io_error();
  }
  return num_write;
}

IoResult<usize> file_size(File file) {
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(handle_from_file(file), &size)) {
    return win32_to_io_error();
  }
  return size.QuadPart;
}

Path app_data_directory(NotNull<Arena *> arena) {
  const char *app_data = std::getenv("APPDATA");
  ren_assert(app_data);
  return Path::init(arena, String8::init(app_data));
}

Path home_directory(NotNull<Arena *> arena) {
  const char *user_profile = std::getenv("USERPROFILE");
  ren_assert(user_profile);
  return Path::init(arena, String8::init(user_profile));
}

} // namespace ren

#endif
