#if _WIN32
#include "FileWatcher.hpp"
#include "core/Win32.hpp"
#include "ren/core/Format.hpp"

#include <Windows.h>

namespace ren {

struct FileWatcher {
  Path m_root;
  HANDLE m_root_watch_handle = INVALID_HANDLE_VALUE;
  HANDLE m_root_watch_event = INVALID_HANDLE_VALUE;
  OVERLAPPED m_root_watch_overlapped = {};
  DynamicArray<Path> m_watch_directories;
  alignas(DWORD) char m_root_watch_buffer[2048];
  usize m_buffer_offset = 0;
  usize m_buffer_size = 0;
};

FileWatcher *start_file_watcher(NotNull<Arena *> arena, Path root) {
  ScratchArena scratch;
  auto *watcher = arena->allocate<FileWatcher>();
  *watcher = {
      .m_root = root.copy(arena),
  };
  watcher->m_root_watch_handle = CreateFileW(
      utf8_to_raw_path(scratch, root.m_str), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
      nullptr);
  if (watcher->m_root_watch_handle == INVALID_HANDLE_VALUE) {
    fmt::println(stderr, "CreateFile for {} failed: {}", root, GetLastError());
    return nullptr;
  }
  watcher->m_root_watch_event = CreateEventW(nullptr, true, false, nullptr);
  if (watcher->m_root_watch_event == INVALID_HANDLE_VALUE) {
    fmt::println(stderr, "CreateEventW failed: {}", GetLastError());
    CloseHandle(watcher->m_root_watch_handle);
    return nullptr;
  }
  return watcher;
}

void stop_file_watcher(NotNull<FileWatcher *> watcher) {
  if (watcher->m_root_watch_overlapped.hEvent) {
    CancelIo(watcher->m_root_watch_handle);
  }
  CloseHandle(watcher->m_root_watch_handle);
  CloseHandle(watcher->m_root_watch_event);
}

void watch_directory(NotNull<Arena *> arena, NotNull<FileWatcher *> watcher,
                     Path relative_path) {
  ScratchArena scratch;
  std::ignore =
      create_directories(watcher->m_root.concat(scratch, relative_path));
  watcher->m_watch_directories.push(arena, relative_path.copy(arena));
}

Optional<FileWatchEvent> read_watch_event(NotNull<Arena *> arena,
                                          NotNull<FileWatcher *> watcher) {
  while (true) {
    if (watcher->m_buffer_offset == watcher->m_buffer_size) {
      [[likely]] if (watcher->m_root_watch_overlapped.hEvent) {
        DWORD num_returned = 0;
        if (!GetOverlappedResult(watcher->m_root_watch_handle,
                                 &watcher->m_root_watch_overlapped,
                                 &num_returned, false)) {
          DWORD err = GetLastError();
          if (err != ERROR_IO_INCOMPLETE) {
            fmt::println(stderr, "GetOverlappedResult failed: {}", err);
          }
          return {};
        }
        watcher->m_root_watch_overlapped = {};
        watcher->m_buffer_offset = 0;
        watcher->m_buffer_size = num_returned;
        if (num_returned == 0) {
          return FileWatchEvent{.type = FileWatchEventType::QueueOverflow};
        }
      } else {
        watcher->m_root_watch_overlapped = {
            .hEvent = watcher->m_root_watch_event,
        };
        if (!ReadDirectoryChangesW(
                watcher->m_root_watch_handle, watcher->m_root_watch_buffer,
                sizeof(watcher->m_root_watch_buffer), true,
                FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &watcher->m_root_watch_overlapped, nullptr)) {
          fmt::println(stderr, "ReadDirectoryChangesW failed: {}",
                       GetLastError());
        }
        return {};
      }
    }

    const auto *event = (const FILE_NOTIFY_INFORMATION *)&watcher
                            ->m_root_watch_buffer[watcher->m_buffer_offset];
    if (event->NextEntryOffset == 0) {
      watcher->m_buffer_offset = watcher->m_buffer_size;
    } else {
      watcher->m_buffer_offset += event->NextEntryOffset;
    }

    FileWatchEventType type = FileWatchEventType::Other;
    if (event->Action == FILE_ACTION_ADDED or
        event->Action == FILE_ACTION_MODIFIED or
        event->Action == FILE_ACTION_RENAMED_NEW_NAME) {
      type = FileWatchEventType::CreatedOrModified;
    } else if (event->Action == FILE_ACTION_REMOVED or
               event->Action == FILE_ACTION_RENAMED_OLD_NAME) {
      type = FileWatchEventType::Removed;
    }

    Span<const wchar_t> wcs_relative_path(
        event->FileName, event->FileNameLength / sizeof(wchar_t));
    Path relative_path = Path::init(wcs_to_utf8(arena, wcs_relative_path));

    for (Path watch_path : watcher->m_watch_directories) {
      if (watch_path == relative_path) {
        return FileWatchEvent{
            .type = type,
            .parent = relative_path,
        };
      }
    }
    for (Path watch_path : watcher->m_watch_directories) {
      if (watch_path == relative_path.parent()) {
        return FileWatchEvent{
            .type = type,
            .parent = relative_path.parent(),
            .filename = relative_path.filename(),
        };
      }
    }
  }
}

} // namespace ren
#endif
