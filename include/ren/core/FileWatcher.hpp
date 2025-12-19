#pragma once
#include "ren/core/Arena.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Optional.hpp"

namespace ren {

struct FileWatcher;

FileWatcher *start_file_watcher(NotNull<Arena *> arena, Path root);
void stop_file_watcher(NotNull<FileWatcher *> watcher);

void watch_directory(NotNull<Arena *> arena, NotNull<FileWatcher *> watcher,
                     Path relative_path);

enum class FileWatchEventType {
  // File was created or modified or is the destination of a rename operation.
  CreatedOrModified,
  // File was deleted or is the source of a rename operation.
  Removed,
  // Unimplemented event
  Other,
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
