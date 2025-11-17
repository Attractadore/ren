#include "ren/core/Algorithm.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/String.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_loadso.h>
#include <cerrno>
#include <fmt/base.h>

#if __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace ren {

const ren::hot_reload::Vtbl *ren::hot_reload::vtbl_ref = nullptr;

namespace {

// Last write time on Linux can get updated before the library has been
// completely written. Use inotify to know for sure when the linker is done.
#if __linux__
int lib_inotify_fd = -1;
int lib_watch_fd = -1;
#else
u64 lib_timestamp = {};
#endif

SDL_SharedObject *lib_handle = nullptr;

// On DLL platforms, the DLL is locked when it's loaded, so it can't be updated
// by the compiler. Copy it to a temporary location to bypass this.
//
// Also, Visual Studio's debugger locks the DLL's PDB after it has been loaded
// and doesn't unlock it until the process exits. Make a temporary copy of the
// PDB and patch the temporary DLL to use it fix this.
Path make_dll_copy(Path from) {
  ScratchArena scratch;

  if (from.extension() != ".dll") {
    return from;
  }

  IoResult<Span<char>> buffer = read(scratch, from);
  if (!buffer) {
    fmt::println(stderr, "hot_reload: Failed to read {}: {}", from,
                 buffer.error());
    return {};
  }

  String<char> filename = from.filename().m_str.copy(scratch);
  fill(filename.m_str, filename.m_size - 4, '_');
  Path to = from.replace_filename(scratch, Path::init(filename));
  ren_assert(from.m_str.m_size == to.m_str.m_size);

  fmt::println("hot_reload: Copy {} to {}", from, to);

  Path from_pdb = from.replace_extension(scratch, Path::init(".pdb"));
  IoResult<bool> from_pdb_exists = from_pdb.exists();
  if (from_pdb_exists and *from_pdb_exists) {
    Path to_pdb = to.replace_extension(scratch, Path::init(".pdb"));

    fmt::println("hot_reload: Copy {} to {}", from_pdb, to_pdb);
    if (IoResult<void> result = copy_file(from_pdb, to_pdb); !result) {
      fmt::println(stderr, "hot_reload: Failed to copy {} to {}: {}", from_pdb,
                   to_pdb, result.error());
      return {};
    }

    fmt::println("hot_reload: Change {} PDB path to {}", to, to_pdb);
    String<char> substr = String<char>(buffer->m_data, buffer->m_size)
                              .find(from_pdb.native(scratch));
    ren_assert(substr.m_size > 0);
    copy(Span(to_pdb.native(scratch)), substr.m_str);
  }

  if (IoResult<void> result = write(to, *buffer); !result) {
    fmt::println(stderr, "hot_reload: Failed to write {}: {}", to,
                 result.error());
    return {};
  }

  return to;
}

} // namespace

Renderer *create_renderer(NotNull<Arena *> arena, const RendererInfo &info) {
  if (!hot_reload::vtbl_ref) {
    ScratchArena scratch;

    Path lib_path = Path::init(scratch, REN_LIB_PATH);

#if __linux__
    fmt::println("hot_reload: Create inotify instance");
    lib_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (lib_inotify_fd == -1) {
      fmt::println(stderr, "hot_reload: Failed to create inotify instance: {}",
                   strerror(errno));
    }

    fmt::println("hot_reload: Add {} to watch list", REN_LIB_DIR);
    lib_watch_fd =
        inotify_add_watch(lib_inotify_fd, REN_LIB_DIR, IN_CLOSE_WRITE);
    if (lib_watch_fd == -1) {
      fmt::println(stderr, "hot_reload: Failed to add inotify watch: {}",
                   strerror(errno));
    }
#else
    IoResult<u64> ts = last_write_time(lib_path);
    if (!ts) {
      fmt::println(stderr, "hot_reload: Failed to get DLL timestamp: {}",
                   ts.error());
    } else {
      lib_timestamp = *ts;
    }
#endif

    Path load_path = make_dll_copy(lib_path);
    if (!load_path) {
      return nullptr;
    }
    fmt::println("hot_reload: Load {}", load_path);
    lib_handle = SDL_LoadObject(load_path.m_str.zero_terminated(scratch));
    if (!lib_handle) {
      fmt::println(stderr, "hot_reload: Failed to load {}: {}", load_path,
                   SDL_GetError());
      return nullptr;
    }

    fmt::println("hot_reload: Fetch vtable");
    hot_reload::vtbl_ref =
        (const hot_reload::Vtbl *)SDL_LoadFunction(lib_handle, "ren_vtbl");
    ren_assert(hot_reload::vtbl_ref);
  }
  return hot_reload::vtbl_ref->create_renderer(arena, info);
}

void draw(Scene *scene, const DrawInfo &draw_info) {
  hot_reload::vtbl_ref->draw(scene, draw_info);

  ScratchArena scratch;

  Path lib_path = Path::init(scratch, REN_LIB_PATH);

#if __linux__
  if (lib_watch_fd == -1) {
    return;
  }
  bool changed = false;
  while (true) {
    alignas(inotify_event) char buffer[2048];
    ssize_t count = ::read(lib_inotify_fd, buffer, sizeof(buffer));
    if (count == -1 and errno != EWOULDBLOCK) {
      fmt::println(stderr, "hot_reload: Failed to get inotify update: {}: {}",
                   errno, strerror(errno));
      return;
    }
    if (count == -1 and errno == EWOULDBLOCK) {
      break;
    }
    ssize_t offset = 0;
    while (offset < count) {
      const auto *e = (const inotify_event *)&buffer[offset];
      if (e->len > 0 and std::strcmp(e->name, REN_LIB_NAME) == 0) {
        changed = true;
      }
      offset += sizeof(*e) + e->len;
    }
  }
  if (not changed) {
    return;
  }
#else
  IoResult<u64> ts = last_write_time(lib_path);
  if (!ts) {
    fmt::println(stderr, "hot_reload: Failed to get DLL timestamp: {}",
                 ts.error());
    return;
  }
  if (*ts <= lib_timestamp) {
    return;
  }
  lib_timestamp = *ts;
#endif

  fmt::println("hot_reload: {} has changed, reload", REN_LIB_PATH);

  fmt::println("hot_reload: Run unload hook");
  hot_reload::vtbl_ref->unload(scene);

  fmt::println("hot_reload: Unload old DLL");
  SDL_UnloadObject(lib_handle);

  Path load_path = make_dll_copy(lib_path);
  if (!load_path) {
    exit(EXIT_FAILURE);
  }
  fmt::println("hot_reload: Load new DLL");
  lib_handle = SDL_LoadObject(load_path.m_str.zero_terminated(scratch));
  if (!lib_handle) {
    fmt::println(stderr, "hot_reload: Failed to load {}: {}", load_path,
                 SDL_GetError());
    exit(EXIT_FAILURE);
  }

  fmt::println("hot_reload: Fetch new vtable");
  hot_reload::vtbl_ref =
      (const hot_reload::Vtbl *)SDL_LoadFunction(lib_handle, "ren_vtbl");
  ren_assert(hot_reload::vtbl_ref);

  fmt::println("hot_reload: Run load hook");
  if (auto result = hot_reload::vtbl_ref->load(scene); !result) {
    fmt::println("hot_reload: Load hook failed");
    exit(EXIT_FAILURE);
  }

  fmt::println("hot_reload: Done");
}

} // namespace ren
