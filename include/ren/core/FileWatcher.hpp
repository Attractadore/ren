#pragma once
#include "ren/core/Arena.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Optional.hpp"

namespace ren {

struct FileWatcher;

/// event_report_timeout_ns: after what period, measured in nanoseconds, will a
/// fuzzy change event for a directory be delivered after the last change was
/// detected.
FileWatcher *start_file_watcher(NotNull<Arena *> arena, Path root,
                                u64 event_report_timeout_ns);
void stop_file_watcher(NotNull<FileWatcher *> watcher);

void watch_directory(NotNull<Arena *> arena, NotNull<FileWatcher *> watcher,
                     Path relative_path);

enum class FileWatchEventType {
  Created,
  RenamedTo,
  Modified,
  Removed,
  RenamedFrom,
  Other,
  Fuzzy,
  QueueOverflow,
};

struct FileWatchEvent {
  FileWatchEventType type;
  Path parent;
  Path filename;
};

/// If a watched child directory of a watched directory is deleted, an event is
/// generated only for the child directory and not the parent directory.
Optional<FileWatchEvent> read_watch_event(NotNull<Arena *> scratch,
                                          NotNull<FileWatcher *> watcher);

} // namespace ren
