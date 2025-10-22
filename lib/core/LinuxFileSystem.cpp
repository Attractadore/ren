#if __linux__
#include "ren/core/FileSystem.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ren {

namespace {

IoStatus io_status_from_errno() {
  switch (errno) {
  default:
    return IoStatus::EUnknown;
  case 0:
    return IoStatus::Success;
  case EACCES:
    return IoStatus::EAccess;
  }
}

} // namespace

const char Path::SEPARATOR = '/';

bool is_path(String8 path) {
  bool is_root = path.m_size == 1 and path[0] == Path::SEPARATOR;
  bool prev_sep = false;
  for (char c : path) {
    if (c == Path::SEPARATOR and prev_sep) {
      return false;
    }
    prev_sep = c == Path::SEPARATOR;
  }
  return not prev_sep or is_root;
}

Path Path::init(NotNull<Arena *> arena, String8 path) {
  bool is_root = path.m_size == 1 and path[0] == Path::SEPARATOR;
  ScratchArena scratch(arena);
  auto builder = StringBuilder::init(scratch);
  bool prev_sep = false;
  for (char c : path) {
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

String8 Path::native(NotNull<Arena *> arena) const { return m_str.copy(arena); }

IoResult<bool> Path::exists() const {
  ScratchArena scratch;
  struct stat statbuf;
  errno = 0;
  ::stat(m_str.zero_terminated(scratch), &statbuf);
  if (!errno) {
    return true;
  }
  if (errno == ENOENT) {
    return false;
  }
  return io_status_from_errno();
}

IoResult<Path> current_directory(NotNull<Arena *> arena) {
  ScratchArena scratch(arena);
  usize buffer_size = PATH_MAX;
  while (true) {
    char *buffer = scratch->allocate<char>(buffer_size);
    errno = 0;
    if (::getcwd(buffer, buffer_size)) {
      usize len = std::strlen(buffer);
      return Path(String8(buffer, len)).copy(arena);
    }
    if (errno == ERANGE) {
      continue;
    }
    return io_status_from_errno();
  }
}

IoStatus create_directory(Path path) {
  ScratchArena scratch;
  errno = 0;
  ::mkdir(path.m_str.zero_terminated(scratch), 0755);
  return io_status_from_errno();
}

IoResult<File> open(Path path, FileAccessMode mode, FileOpenFlags flags) {
  ScratchArena scratch;
  int posix_flags = 0;
  switch (mode) {
  case FileAccessMode::ReadOnly:
    posix_flags = O_RDONLY;
    break;
  case FileAccessMode::WriteOnly:
    posix_flags = O_WRONLY;
    break;
  case FileAccessMode::ReadWrite:
    posix_flags = O_RDWR;
    break;
  }
  if (flags.is_set(FileOpen::Create)) {
    posix_flags |= O_CREAT;
  }
  if (flags.is_set(FileOpen::Truncate)) {
    posix_flags |= O_TRUNC;
  }
  errno = 0;
  int fd = ::open(path.m_str.zero_terminated(scratch), posix_flags, 0644);
  File file = {(uintptr_t)fd};
  return {file, io_status_from_errno()};
}

void close(File file) {
  errno = 0;
  ::close(file.m_fd);
}

IoResult<usize> read(File file, void *buffer, usize size) {
  errno = 0;
  ssize_t num_read = ::read(file.m_fd, buffer, size);
  return {(usize)num_read, io_status_from_errno()};
}

IoResult<usize> write(File file, const void *buffer, usize size) {
  errno = 0;
  ssize_t num_written = ::write(file.m_fd, buffer, size);
  return {(usize)num_written, io_status_from_errno()};
}

IoResult<usize> file_size(File file) {
  errno = 0;
  struct stat64 statbuf;
  ::fstat64(file.m_fd, &statbuf);
  return {(usize)statbuf.st_size, io_status_from_errno()};
}

} // namespace ren
#endif
