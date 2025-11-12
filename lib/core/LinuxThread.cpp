#if __linux__
#include "ren/core/Array.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Thread.hpp"

#include "fmt/base.h"
#include <fcntl.h>
#include <unistd.h>

namespace ren {

Span<Processor> cpu_topology(NotNull<Arena *> arena) {
  ScratchArena scratch(arena);
  DynamicArray<Processor> processors;
  char onl_buffer[1024];
  const char *cursor = nullptr;

  const char ONLINE[] = "/sys/devices/system/cpu/online";

  IoResult<File> file = open(Path::init(ONLINE), FileAccessMode::ReadOnly);
  if (!file) {
    fmt::println(stderr, "thread: failed to open {}: {}", ONLINE, file.error());
    exit(EXIT_FAILURE);
  }
  if (IoResult<usize> result = read(*file, onl_buffer, sizeof(onl_buffer));
      !result) {
    fmt::println(stderr, "thread: failed to read {}: {}", ONLINE,
                 result.error());
    exit(EXIT_FAILURE);
  }
  close(*file);
  file = {};
  fmt::print("thread: {}: {}", ONLINE, onl_buffer);

  cursor = onl_buffer;
  while (true) {
    usize first = 0;
    while (*cursor != '-') {
      first = first * 10 + *cursor - '0';
      cursor++;
    }
    cursor++;
    usize last = 0;
    while (*cursor != ',' and *cursor != '\n') {
      last = last * 10 + *cursor - '0';
      cursor++;
    }

    for (usize id = first; id <= last; ++id) {
      Processor processor;
      processor.id = id;

      const char *hierarchy_names[] = {
          "core",
          "die",
          "physical_package",
          "cluster",
      };

      u32 *hierarchy_id_ptrs[] = {
          &processor.core_id,
          &processor.die_id,
          &processor.package_id,
          &processor.cluster_id,
      };

      for (usize hierarchy_index : range(4)) {
        char hi_buffer[128];
        char path[256];
        usize len =
            fmt::format_to_n(path, sizeof(path),
                             "/sys/devices/system/cpu/cpu{}/topology/{}_id", id,
                             hierarchy_names[hierarchy_index])
                .size;
        ren_assert(len + 1 <= sizeof(path));
        path[len] = 0;
        file = open(Path::init(path), FileAccessMode::ReadOnly);
        if (!file) {
          fmt::println(stderr, "thread: failed to open {}: {}", path,
                       file.error());
          exit(EXIT_FAILURE);
        }
        if (IoResult<usize> result = read(*file, hi_buffer, sizeof(hi_buffer));
            !result) {
          fmt::println(stderr, "thread: failed to read {}: {}", path,
                       result.error());
          exit(EXIT_FAILURE);
        }
        close(*file);
        file = {};

        u32 *id_ptr = hierarchy_id_ptrs[hierarchy_index];
        int cnt = std::sscanf(hi_buffer, "%u", id_ptr);
        ren_assert(cnt == 1);
      }
      processors.push(scratch, processor);
    }

    if (*cursor == '\n') {
      break;
    }
    cursor++;
  }

  return Span(processors).copy(arena);
}

} // namespace ren
#endif
