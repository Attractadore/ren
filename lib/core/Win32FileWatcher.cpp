#if _WIN32
#include "Win32.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/FileWatcher.hpp"

#include <Windows.h>
#include <fmt/base.h>
#include <tracy/Tracy.hpp>
#include <utility>

namespace ren {

struct FileWatchItem {
  Path relative_path;
  HANDLE handle = INVALID_HANDLE_VALUE;
  u64 last_event_time_ns = UINT64_MAX;
};

struct FileWatcher {
  Path m_root;
  u64 m_report_timeout_ns = 0;
  usize m_num_watch_items = 0;
  FileWatchItem m_watch_items[64] = {};
};

FileWatcher *start_file_watcher(NotNull<Arena *> arena, Path root,
                                u64 event_report_timeout_ns) {
  ScratchArena scratch;
  auto *watcher = arena->allocate<FileWatcher>();
  *watcher = {
      .m_root = root.copy(arena),
      .m_report_timeout_ns = event_report_timeout_ns,
  };
  return watcher;
}

void stop_file_watcher(NotNull<FileWatcher *> watcher) {
  for (usize i : range(watcher->m_num_watch_items)) {
    FindCloseChangeNotification(watcher->m_watch_items[i].handle);
  }
  watcher->m_num_watch_items = 0;
}

void watch_directory(NotNull<Arena *> arena, NotNull<FileWatcher *> watcher,
                     Path relative_path) {
  ren_assert_msg(watcher->m_num_watch_items < 64,
                 "WaitForMultipleObjects can't wait for more than 64 handles");
  ScratchArena scratch;
  Path path = watcher->m_root.concat(scratch, relative_path);
  std::ignore = create_directories(path);
  HANDLE handle = FindFirstChangeNotificationW(
      utf8_to_raw_path(scratch, path), false,
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
          FILE_NOTIFY_CHANGE_LAST_WRITE);
  if (handle == INVALID_HANDLE_VALUE) {
    fmt::println(stderr, "FindFirstChangeNotificationW failed: {}",
                 GetLastError());
    return;
  }
  watcher->m_watch_items[watcher->m_num_watch_items++] = {
      .relative_path = relative_path.copy(arena),
      .handle = handle,
  };
}

Optional<FileWatchEvent> read_watch_event(NotNull<Arena *> arena,
                                          NotNull<FileWatcher *> watcher) {
  ZoneScoped;

  HANDLE wait_handles[64];
  for (usize i : range(watcher->m_num_watch_items)) {
    wait_handles[i] = watcher->m_watch_items[i].handle;
  }
  DWORD wait_result = WaitForMultipleObjects(watcher->m_num_watch_items,
                                             wait_handles, false, 0);
  ren_assert(wait_result < WAIT_ABANDONED_0 or
             wait_result >= WAIT_ABANDONED_0 + watcher->m_num_watch_items);
  if (wait_result == WAIT_FAILED) {
    fmt::println(stderr, "WaitForMultipleObjects failed: {}", GetLastError());
    return {};
  }

  u64 now_ns = clock();
  if (wait_result != WAIT_TIMEOUT) {
    usize ready_index = wait_result - WAIT_OBJECT_0;
    ren_assert(ready_index < 64);
    watcher->m_watch_items[ready_index].last_event_time_ns = now_ns;
    if (!FindNextChangeNotification(
            watcher->m_watch_items[ready_index].handle)) {
      fmt::println(stderr, "FindNextChangeNotification failed: {}",
                   GetLastError());
    }
  }

  for (FileWatchItem &wi :
       Span(watcher->m_watch_items, watcher->m_num_watch_items)) {
    if (wi.last_event_time_ns < now_ns and
        wi.last_event_time_ns + watcher->m_report_timeout_ns < now_ns) {
      wi.last_event_time_ns = UINT64_MAX;
      return FileWatchEvent{
          .type = FileWatchEventType::Fuzzy,
          .parent = wi.relative_path.copy(arena),
      };
    }
  }

  return {};
}

} // namespace ren
#endif
