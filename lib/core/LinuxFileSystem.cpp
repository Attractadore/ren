#if __linux__
#include "ren/core/FileSystem.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ren {

namespace {

IoError io_error_from_errno(int err = errno) {
  ren_assert(err);
  switch (err) {
  default:
    return IoError::Unknown;
  case EACCES:
    return IoError::Access;
  case EEXIST:
    return IoError::Exists;
  case ENOENT:
    return IoError::NotFound;
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
  ScratchArena scratch;
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
  if (::stat(m_str.zero_terminated(scratch), &statbuf) == 0) {
    return true;
  }
  if (errno == ENOENT) {
    return false;
  }
  return io_error_from_errno();
}

IoResult<Path> current_directory(NotNull<Arena *> arena) {
  ScratchArena scratch;
  usize buffer_size = PATH_MAX;
  while (true) {
    char *buffer = scratch->allocate<char>(buffer_size);
    if (::getcwd(buffer, buffer_size)) {
      return Path(String8::init(buffer)).copy(arena);
    }
    if (errno == ERANGE) {
      continue;
    }
    return io_error_from_errno();
  }
}

IoResult<void> create_directory(Path path) {
  ScratchArena scratch;
  if (::mkdir(path.m_str.zero_terminated(scratch), 0755)) {
    return io_error_from_errno();
  }
  return {};
}

IoResult<bool> is_directory_empty(Path path) {
  ScratchArena scratch;
  errno = 0;
  DIR *dir = opendir(path.m_str.zero_terminated(scratch));
  if (!dir) {
    return io_error_from_errno();
  }
  struct dirent *file = nullptr;

  errno = 0;
  file = readdir(dir);
  if (!file) {
    int err = errno;
    ren_assert(err);
    closedir(dir);
    return io_error_from_errno(err);
  }

  errno = 0;
  file = readdir(dir);
  if (!file) {
    int err = errno;
    ren_assert(err);
    closedir(dir);
    return io_error_from_errno(err);
  }

  errno = 0;
  file = readdir(dir);
  int err = errno;
  closedir(dir);
  if (!file and !err) {
    return true;
  }
  if (err) {
    return io_error_from_errno(err);
  }

  return false;
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
  int fd = ::open(path.m_str.zero_terminated(scratch), posix_flags, 0644);
  if (fd == -1) {
    return io_error_from_errno();
  }
  File file = {(uintptr_t)fd};
  return file;
}

void close(File file) { ::close(file.m_fd); }

IoResult<usize> seek(File file, isize offset, SeekMode mode) {
  int whence = 0;
  switch (mode) {
  case SeekMode::Set:
    whence = SEEK_SET;
    break;
  case SeekMode::End:
    whence = SEEK_END;
    break;
  case SeekMode::Cur:
    whence = SEEK_CUR;
    break;
  }
  offset = lseek(file.m_fd, offset, whence);
  if (offset < 0) {
    return io_error_from_errno();
  }
  return offset;
}

IoResult<usize> read(File file, void *buffer, usize size) {
  ssize_t num_read = ::read(file.m_fd, buffer, size);
  if (num_read < 0) {
    return io_error_from_errno();
  }
  return num_read;
}

IoResult<usize> write(File file, const void *buffer, usize size) {
  ssize_t num_written = ::write(file.m_fd, buffer, size);
  if (num_written < 0) {
    return io_error_from_errno();
  }
  return num_written;
}

IoResult<usize> file_size(File file) {
  struct stat statbuf;
  if (::fstat(file.m_fd, &statbuf) == -1) {
    return io_error_from_errno();
  }
  return statbuf.st_size;
}

Path app_data_directory(NotNull<Arena *> arena) {
  const char *xdg_data_home = std::getenv("XDG_DATA_HOME");
  if (xdg_data_home) {
    return Path::init(arena, String8::init(xdg_data_home));
  }
  const char *home = std::getenv("HOME");
  ren_assert(home);
  return Path::init(String8::init(home))
      .concat(arena, Path::init(String8::init(".local/share")));
}

Path home_directory(NotNull<Arena *> arena) {
  const char *home = std::getenv("HOME");
  ren_assert(home);
  return Path::init(arena, String8::init(home));
}

IoResult<NotNull<Directory *>> open_directory(NotNull<Arena *>, Path path) {
  ScratchArena scratch;
  errno = 0;
  auto *dir = (Directory *)::opendir(path.m_str.zero_terminated(scratch));
  if (!dir) {
    return io_error_from_errno();
  }
  return NotNull<Directory *>(dir);
}

IoResult<Path> read_directory(NotNull<Arena *> arena,
                              NotNull<Directory *> dir) {
  errno = 0;
  struct dirent *ent = ::readdir((DIR *)dir.get());
  if (!ent) {
    if (errno == 0) {
      return Path();
    }
    return io_error_from_errno();
  }
  return Path::init(arena, String8::init(ent->d_name));
}

void close_directory(NotNull<Directory *> dir) {
  errno = 0;
  ::closedir((DIR *)dir.get());
}

} // namespace ren
#endif
