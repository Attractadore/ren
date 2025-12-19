#if _WIN32
#include "Win32.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/FileSystem.hpp"

#include <Windows.h>
#include <bit>
#include <limits>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace ren {

const char Path::SEPARATOR = '\\';

namespace {

IoError win32_to_io_error(DWORD err = GetLastError()) {
  ren_assert(err);
  switch (err) {
  default:
    return IoError::Unknown;
  case ERROR_ALREADY_EXISTS:
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

  ScratchArena scratch;
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
  static LPSTR (*const wine_get_unix_file_name)(LPCWSTR) =
      (decltype(wine_get_unix_file_name))GetProcAddress(
          GetModuleHandleA("KERNEL32"), "wine_get_unix_file_name");
  if (wine_get_unix_file_name) {
    ScratchArena scratch;
    const char *str = wine_get_unix_file_name(utf8_to_path(scratch, m_str));
    ren_assert(str);
    return String8::init(str).copy(arena);
  }
  return m_str.copy(arena);
}

IoResult<bool> Path::exists() const {
  ScratchArena scratch;
  if (PathFileExistsW(utf8_to_path(scratch, m_str))) {
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
  ScratchArena scratch;
  wchar_t *buffer = scratch->allocate<wchar_t>(size + 1);
  if (!GetCurrentDirectoryW(size + 1, buffer)) {
    return win32_to_io_error();
  }
  return Path::init(arena, wcs_to_utf8(scratch, buffer));
}

IoResult<void> create_directory(Path path) {
  ScratchArena scratch;
  const wchar_t *wcs_path = utf8_to_path(scratch, path.m_str);
  if (!CreateDirectoryW(wcs_path, nullptr)) {
    return win32_to_io_error();
  }
  return {};
}

IoResult<bool> is_directory_empty(Path path) {
  ScratchArena scratch;
  // Init directly since '*' is not a valid path
  path = path.concat(scratch, Path{"*"});
  const wchar_t *wcs_path = utf8_to_path(scratch, path.m_str);
  WIN32_FIND_DATAW find_data;

  HANDLE handle = FindFirstFileW(wcs_path, &find_data);
  if (!handle) {
    return win32_to_io_error();
  }

  if (!FindNextFileW(handle, &find_data)) {
    DWORD err = GetLastError();
    FindClose(handle);
    return win32_to_io_error(err);
  }

  DWORD err = 0;
  if (!FindNextFileW(handle, &find_data)) {
    err = GetLastError();
  }
  FindClose(handle);
  if (err == ERROR_NO_MORE_FILES) {
    return true;
  }
  if (err) {
    return win32_to_io_error(err);
  }

  return false;
}

IoResult<u64> last_write_time(Path path) {
  ScratchArena scratch;
  HANDLE hfile =
      CreateFileW(utf8_to_path(scratch, path.m_str), 0,
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
    return win32_to_io_error(err);
  }
  return std::bit_cast<u64>(time);
}

IoResult<void> unlink(Path path) {
  ScratchArena scratch;
  if (!DeleteFileW(utf8_to_raw_path(scratch, path.m_str))) {
    return win32_to_io_error();
  }
  return {};
}

IoResult<void> remove_directory(Path path) {
  ScratchArena scratch;
  if (!RemoveDirectoryW(utf8_to_raw_path(scratch, path.m_str))) {
    return win32_to_io_error();
  }
  return {};
}

IoResult<void> remove_directory_tree(Path path) {
  ScratchArena scratch;
  IoResult<Path> abs_path = path.absolute(scratch);
  if (!abs_path) {
    return abs_path.error();
  }
  SHFILEOPSTRUCTW file_op = {
      .wFunc = FO_DELETE,
      // Path is required to be double zero terminated.
      .pFrom = utf8_to_path(scratch, abs_path->m_str, L"\0"),
      .fFlags = FOF_NO_UI,
  };
  if (SHFileOperationW(&file_op)) {
    return IoError::Unknown;
  }
  return {};
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
      CreateFileW(utf8_to_path(scratch, path.m_str), access,
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

struct Directory {
  HANDLE handle = nullptr;
  bool is_first_time = true;
  WIN32_FIND_DATAW find_first_data = {};
};

IoResult<NotNull<Directory *>> open_directory(NotNull<Arena *> arena,
                                              Path path) {
  ScratchArena scratch;
  Directory *dir = arena->allocate<Directory>();
  dir->handle = FindFirstFileW(utf8_to_raw_path(scratch, path.m_str, L"\\*"),
                               &dir->find_first_data);
  if (dir->handle == INVALID_HANDLE_VALUE) {
    return win32_to_io_error();
  }
  return NotNull<Directory *>(dir);
}

void close_directory(NotNull<Directory *> dir) { FindClose(dir->handle); }

IoResult<Path> read_directory(NotNull<Arena *> arena,
                              NotNull<Directory *> dir) {
  if (dir->is_first_time) {
    dir->is_first_time = false;
    return Path::init(wcs_to_utf8(arena, dir->find_first_data.cFileName));
  }
  WIN32_FIND_DATAW find_data;
  if (!FindNextFileW(dir->handle, &find_data)) {
    DWORD err = GetLastError();
    if (err == ERROR_NO_MORE_FILES) {
      return Path();
    }
    return win32_to_io_error(err);
  }
  return Path::init(wcs_to_utf8(arena, find_data.cFileName));
}

} // namespace ren

#endif
