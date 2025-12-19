#if __linux__
#include "ren/core/FileWatcher.hpp"
#include "ren/core/Format.hpp"

#include <cerrno>
#include <fmt/base.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <utility>

namespace ren {

struct WatchItem {
  int wd = -1;
  Path relative_path;
};

struct FileWatcher {
  Path m_root;
  int m_inotify_fd = -1;
  DynamicArray<WatchItem> m_watch_items;
  alignas(inotify_event) char buffer[2048];
  usize buffer_size = 0;
  usize buffer_offset = 0;
};

FileWatcher *start_file_watcher(NotNull<Arena *> arena, Path root) {
  auto *watcher = arena->allocate<FileWatcher>();
  *watcher = {
      .m_root = root.copy(arena),
  };
  ScratchArena scratch;
  errno = 0;
  watcher->m_inotify_fd = inotify_init1(IN_NONBLOCK);
  if (watcher->m_inotify_fd == -1) {
    fmt::println(stderr, "Failed to create inotify instance: {}",
                 strerror(errno));
    return nullptr;
  }
  return watcher;
}

void stop_file_watcher(NotNull<FileWatcher *> watcher) {
  errno = 0;
  ::close(watcher->m_inotify_fd);
}

void watch_directory(NotNull<Arena *> arena, NotNull<FileWatcher *> watcher,
                     Path relative_path) {
  ren_assert(not relative_path.is_absolute());
  ScratchArena scratch;
  Path path = watcher->m_root.concat(scratch, relative_path);
  std::ignore = create_directories(path);
  errno = 0;
  int wd = inotify_add_watch(watcher->m_inotify_fd,
                             path.m_str.zero_terminated(scratch),
                             IN_ONLYDIR | IN_ALL_EVENTS | IN_EXCL_UNLINK);
  if (wd == -1) {
    fmt::println(stderr, "Failed to add {} to inotify watch list: {}",
                 relative_path, strerror(errno));
    return;
  }
  watcher->m_watch_items.push(arena, {wd, relative_path.copy(arena)});
}

Optional<FileWatchEvent> read_watch_event(NotNull<Arena *> arena,
                                          NotNull<FileWatcher *> watcher) {
top: {
  if (watcher->buffer_offset == watcher->buffer_size) {
    ssize_t count =
        ::read(watcher->m_inotify_fd, watcher->buffer, sizeof(watcher->buffer));
    if (count == -1 and errno != EWOULDBLOCK) {
      fmt::println(stderr, "Failed to read inotify update: {}",
                   strerror(errno));
      return {};
    }
    if (count == -1 and errno == EWOULDBLOCK) {
      return {};
    }
    watcher->buffer_offset = 0;
    watcher->buffer_size = count;
  }

  const auto *event =
      (const inotify_event *)&watcher->buffer[watcher->buffer_offset];
  watcher->buffer_offset += sizeof(*event) + event->len;
  ren_assert(watcher->buffer_offset <= watcher->buffer_size);

  if (event->mask & IN_Q_OVERFLOW) {
    return FileWatchEvent{.type = FileWatchEventType::QueueOverflow};
  }
  FileWatchEventType type = FileWatchEventType::Other;
  if (event->mask & (IN_DELETE | IN_MOVED_FROM | IN_MOVE_SELF | IN_IGNORED)) {
    type = FileWatchEventType::Removed;
  } else if (event->mask & (IN_ATTRIB | IN_CLOSE_WRITE | IN_MOVED_TO)) {
    type = FileWatchEventType::CreatedOrModified;
  }

  Path parent;
  for (WatchItem wi : watcher->m_watch_items) {
    if (wi.wd == event->wd) {
      parent = wi.relative_path;
      break;
    };
  }
  ren_assert(parent);
  Path filename;
  if (event->len > 0) {
    filename = Path::init(String8::init(event->name));
  }

  // Don't generate an event when a watched child is removed.
  if ((event->mask & IN_ISDIR) and
      (event->mask & (IN_DELETE | IN_MOVED_FROM))) {
    for (WatchItem wi : watcher->m_watch_items) {
      if (wi.relative_path.parent() == parent and
          wi.relative_path.filename() == filename) {
        goto top;
      };
    }
  }

  return FileWatchEvent{
      .type = type,
      .parent = parent.copy(arena),
      .filename = filename.copy(arena),
  };
}
}

} // namespace ren
#endif
