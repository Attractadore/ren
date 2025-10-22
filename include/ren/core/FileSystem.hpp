#pragma once
#include "Flags.hpp"
#include "Result.hpp"
#include "Span.hpp"
#include "String.hpp"

namespace ren {

enum class IoStatus {
  Success,
  EUnknown,
  EAccess,
  ENotFound,
  EExists,
  EFragmented,
};
constexpr IoStatus IoSuccess = IoStatus::Success;

String8 format_as(IoStatus status);

template <typename T> using IoResult = Result2<T, IoStatus>;

bool is_path(String8 path);

String8 path_volume_name(String8 path);

struct Path {
  String8 m_str;

  static const char SEPARATOR;

public:
  [[nodiscard]] static Path init(String8 path);
  [[nodiscard]] static Path init(NotNull<Arena *> arena, String8 path);

  explicit operator bool() const { return !!m_str; }

  [[nodiscard]] Path copy(NotNull<Arena *> arena) const;

  [[nodiscard]] bool is_absolute() const;

  [[nodiscard]] IoResult<Path> absolute(NotNull<Arena *> arena) const;

  [[nodiscard]] IoResult<Path> relative(NotNull<Arena *> arena) const;

  [[nodiscard]] Path volume_name() const;

  [[nodiscard]] Path parent() const;

  [[nodiscard]] Path extension() const;

  [[nodiscard]] Path replace_extension(NotNull<Arena *> arena,
                                       Path new_ext) const;

  [[nodiscard]] Path stem() const;

  [[nodiscard]] Path filename() const;

  [[nodiscard]] Path replace_filename(NotNull<Arena *> arena,
                                      Path new_fn) const;

  // Returns Linux path if running on Wine, otherwise the path.
  [[nodiscard]] String8 native(NotNull<Arena *> arena) const;

  [[nodiscard]] IoResult<bool> exists() const;

  [[nodiscard]] Path concat(NotNull<Arena *> arena, Path other) const;
};

inline bool operator==(Path lhs, String8 rhs) { return lhs.m_str == rhs; }

String8 format_as(Path path);

IoResult<Path> current_directory(NotNull<Arena *> arena);

IoStatus create_directory(Path path);

IoResult<u64> last_write_time(Path path);

struct File {
  uintptr_t m_fd;
};

enum class FileAccessMode {
  ReadOnly,
  WriteOnly,
  ReadWrite,
};

REN_BEGIN_FLAGS_ENUM(FileOpen){
    REN_FLAG(Create),
    REN_FLAG(Truncate),
} REN_END_FLAGS_ENUM(FileOpen);

} // namespace ren

REN_ENABLE_FLAGS(ren::FileOpen);

namespace ren {

using FileOpenFlags = Flags<FileOpen>;

[[nodiscard]] IoResult<File> open(Path path, FileAccessMode mode,
                                  FileOpenFlags flags = {});

void close(File file);

[[nodiscard]] IoResult<usize> read(File file, void *buffer, usize size);

[[nodiscard]] IoResult<usize> write(File file, const void *buffer, usize size);

[[nodiscard]] IoResult<usize> file_size(File file);

[[nodiscard]] IoResult<Span<char>> read(NotNull<Arena *> arena, Path path);

template <typename T>
[[nodiscard]] IoResult<Span<T>> read(NotNull<Arena *> arena, Path path) {
  IoResult<Span<char>> buffer = read(arena, path);
  if (!buffer) {
    return buffer.m_status;
  }
  if (buffer.m_value.size() % sizeof(T) != 0) {
    return IoStatus::EFragmented;
  }
  return Span<T>((T *)buffer.m_value.data(), buffer.m_value.size() / sizeof(T));
}

[[nodiscard]] IoStatus write(Path path, const void *buffer, usize size,
                             FileOpenFlags flags = FileOpen::Create |
                                                   FileOpen::Truncate);

template <typename T>
[[nodiscard]] IoStatus write(Path path, Span<T> buffer,
                             FileOpenFlags flags = FileOpen::Create |
                                                   FileOpen::Truncate) {
  return write(path, buffer.data(), buffer.size_bytes(), flags);
}

[[nodiscard]] IoStatus copy_file(Path from, Path to,
                                 FileOpenFlags flags = FileOpen::Create |
                                                       FileOpen::Truncate);

} // namespace ren
