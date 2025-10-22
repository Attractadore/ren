#include "ren/core/FileSystem.hpp"

namespace ren {

namespace {

bool is_volume_name(char a, char b) {
  return (a >= 'a' and a <= 'z' or a >= 'A' and a <= 'Z') and b == ':';
}

} // namespace

String8 format_as(IoStatus status) {
  switch (status) {
  default:
    return "Unknown";
  case IoStatus::Success:
    return "Success";
  case IoStatus::EFragmented:
    return "Invalid size";
  }
}

String8 path_volume_name(String8 path) {
  if (path.m_size < 2) {
    return {};
  }
  if (is_volume_name(path[0], path[1])) {
    return path.substr(0, 2);
  }
  return {};
}

String8 format_as(Path path) { return path.m_str; }

Path Path::init(String8 path) {
  ren_assert(is_path(path));
  return {path};
}

Path Path::copy(NotNull<Arena *> arena) const { return {m_str.copy(arena)}; }

Path Path::volume_name() const {
  const char *sep = m_str.find(Path::SEPARATOR);
  if (sep == m_str.m_str + 2 and is_volume_name(m_str[0], m_str[1])) {
    return Path(String8(m_str.m_str, 2));
  }
  return Path("");
}

Path Path::parent() const {
  const char *sep = m_str.rfind(Path::SEPARATOR);
  if (!sep) {
    ren_assert(not is_absolute());
    return Path{"."};
  }
  usize sep_offset = sep - m_str.m_str;
  Path root = this->volume_name();
  if (sep_offset + 1 == root.m_str.m_size) {
    return root;
  }
  return {String8(m_str.m_str, sep_offset - 1)};
}

Path Path::filename() const {
  const char *sep = m_str.rfind(Path::SEPARATOR);
  usize offset = sep ? sep - m_str.m_str + 1 : 0;
  return {String8(m_str.m_str + offset, m_str.m_size - offset)};
}

Path Path::replace_filename(NotNull<Arena *> arena, Path new_fn) const {
  auto [new_fn_str, new_fn_size] = new_fn.m_str;
  String8 fn = filename().m_str;
  usize new_size = m_str.m_size - fn.m_size + new_fn_size;
  char *buffer = arena->allocate<char>(new_size);
  std::memcpy(buffer, m_str.m_str, m_str.m_size - fn.m_size);
  std::memcpy(&buffer[m_str.m_size - fn.m_size], new_fn_str, new_fn_size);
  return {String8(buffer, new_size)};
}

Path Path::extension() const {
  Path fn = filename();
  const char *dot = fn.m_str.rfind('.');
  if (!dot) {
    return {};
  }
  usize offset = dot - fn.m_str.m_str;
  return {String8(dot, fn.m_str.m_size - offset)};
}

Path Path::stem() const {
  Path fn = filename();
  const char *dot = fn.m_str.rfind('.');
  if (!dot) {
    return fn;
  }
  usize size = dot - fn.m_str.m_str;
  return {String8(fn.m_str.m_str, size)};
}

Path Path::replace_extension(NotNull<Arena *> arena, Path new_ext) const {
  auto [new_ext_str, new_ext_size] = new_ext.m_str;
  ren_assert(new_ext_size == 0 or new_ext_str[0] == '.');
  String8 ext = extension().m_str;
  usize new_size = m_str.m_size - ext.m_size + new_ext_size;
  char *buffer = arena->allocate<char>(new_size);
  std::memcpy(buffer, m_str.m_str, m_str.m_size - ext.m_size);
  std::memcpy(&buffer[m_str.m_size - ext.m_size], new_ext_str, new_ext_size);
  return {String8(buffer, new_size)};
}

bool Path::is_absolute() const {
  String8 vol = volume_name().m_str;
  if (vol != m_str.substr(0, vol.m_size)) {
    return false;
  }
  return vol.m_size < m_str.m_size and m_str[vol.m_size] == SEPARATOR;
}

IoResult<Path> Path::absolute(NotNull<Arena *> arena) const {
  if (is_absolute()) {
    return copy(arena);
  }
  ScratchArena scratch(arena);
  auto [cwd, status] = current_directory(scratch);
  if (status != IoSuccess) {
    return status;
  }
  return cwd.concat(arena, *this);
}

Path Path::concat(NotNull<Arena *> arena, Path other) const {
  ren_assert(not other.is_absolute());
  usize new_size = m_str.m_size + 1 + other.m_str.m_size;
  char *buffer = arena->allocate<char>(new_size);
  std::memcpy(buffer, m_str.m_str, m_str.m_size);
  buffer[m_str.m_size] = SEPARATOR;
  std::memcpy(&buffer[m_str.m_size + 1], other.m_str.m_str, other.m_str.m_size);
  return {String8(buffer, new_size)};
}

IoResult<Span<char>> read(NotNull<Arena *> arena, Path path) {
  IoResult<File> file = open(path, FileAccessMode::ReadOnly);
  if (!file) {
    return file.m_status;
  }
  IoResult<usize> size = file_size(file.m_value);
  if (!size) {
    close(file.m_value);
    return size.m_status;
  }
  char *buffer = (char *)arena->allocate(size.m_value, 8);
  usize total_read = 0;
  while (total_read < size.m_value) {
    IoResult<usize> num_read =
        read(file.m_value, &buffer[total_read], size.m_value - total_read);
    if (!num_read) {
      close(file.m_value);
      return num_read.m_status;
    }
    total_read += num_read.m_value;
  }
  close(file.m_value);
  return Span<char>(buffer, size.m_value);
}

IoStatus write(Path path, const void *void_buffer, usize size,
               FileOpenFlags flags) {
  IoResult<File> file = open(path, FileAccessMode::WriteOnly, flags);
  if (!file) {
    return file.m_status;
  }
  const char *buffer = (const char *)void_buffer;
  usize total_written = 0;
  while (total_written < size) {
    IoResult<usize> num_written =
        write(file.m_value, &buffer[total_written], size - total_written);
    if (!num_written) {
      close(file.m_value);
      return num_written.m_status;
    }
    total_written += num_written.m_value;
  }
  close(file.m_value);
  return IoStatus::Success;
}

IoStatus copy_file(Path from, Path to, FileOpenFlags flags) {
  ScratchArena scratch;
  auto [data, read_status] = read(scratch, from);
  if (read_status != IoSuccess) {
    return read_status;
  }
  return write(to, data, flags);
}

} // namespace ren
