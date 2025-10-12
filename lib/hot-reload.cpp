#include "core/IO.hpp"
#include "core/Result.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "ren/core/Assert.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_loadso.h>
#include <filesystem>
#include <fmt/base.h>
#include <fmt/std.h>

#if __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ren {

const ren::hot_reload::Vtbl *ren::hot_reload::vtbl_ref = nullptr;

namespace {

// Last write time on Linux can get updated before the library has been
// completely written. Use inotify to know for sure when the linker is done.
#if __linux__
int lib_inotify_fd = -1;
int lib_watch_fd = -1;
#else
fs::file_time_type lib_timestamp = {};
#endif

SDL_SharedObject *lib_handle = nullptr;

// On DLL platforms, the DLL is locked when it's loaded, so it can't be updated
// by the compiler. Copy it to a temporary location to bypass this.
//
// Also, Visual Studio's debugger locks the DLL's PDB after it has been loaded
// and doesn't unlock it until the process exits. Make a temporary copy of the
// PDB and patch the temporary DLL to use it fix this.
auto make_dll_copy(fs::path from) -> Result<fs::path, Error> {
  if (from.extension() != ".dll") {
    return from;
  }
  from.make_preferred();

  FILE *src = fopen(from, "rb");
  if (!src) {
    fmt::println("hot_reload: Failed to open {} for reading: {}",
                 fs::relative(from), std::strerror(errno));
    return {};
  }

  Vector<char> buffer(fs::file_size(from));
  usize num_read = std::fread(buffer.data(), 1, buffer.size(), src);
  std::fclose(src);
  if (num_read != buffer.size()) {
    fmt::println("hot_reload: Failed to read from {}", fs::relative(from));
    return Failure(Error::IO);
  }

  fs::path::string_type filename = from.filename().native();
  std::ranges::fill_n(filename.data(), filename.size() - 4, '_');
  fs::path to = fs::path(from).replace_filename(filename);

  fmt::println("hot_reload: Copy {} to {}", fs::relative(from),
               fs::relative(to));

  fs::path from_pdb = fs::path(from).replace_extension(".pdb");
  if (fs::exists(from_pdb)) {
    fs::path to_pdb = fs::path(to).replace_extension(".pdb");
    fmt::println("hot_reload: Copy {} to {}", fs::relative(from_pdb),
                 fs::relative(to_pdb));
    fs::copy(from_pdb, to_pdb, fs::copy_options::overwrite_existing);

    fmt::println("hot_reload: Change {} PDB path to {}", fs::relative(to),
                 fs::relative(to_pdb));
    String from_pdb_str = to_system_path(from_pdb.string());
    String to_pdb_str = to_system_path(to_pdb.string());
    ren_assert(from_pdb_str.size() == to_pdb_str.size());
    size_t offset = StringView(buffer.begin(), buffer.end()).find(from_pdb_str);
    ren_assert(offset != StringView::npos);
    std::ranges::copy(to_pdb_str, &buffer[offset]);
  }

  FILE *dst = fopen(to, "wb");
  if (!dst) {
    fmt::println("hot_reload: Failed to open {} for writing: {}",
                 fs::relative(to), std::strerror(errno));
    return {};
  }

  usize num_write = std::fwrite(buffer.data(), 1, buffer.size(), dst);
  std::fclose(dst);
  if (num_write != buffer.size()) {
    fmt::println("hot_reload: Failed to write to {}", fs::relative(to));
    return Failure(Error::IO);
  }

  return to;
}

} // namespace

auto create_renderer(Arena scratch, NotNull<Arena *> arena,
                     const RendererInfo &info) -> expected<Renderer *> {
  if (!hot_reload::vtbl_ref) {
#if __linux__
    fmt::println("hot_reload: Create inotify instance");
    lib_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (lib_inotify_fd == -1) {
      fmt::println("hot_reload: Failed to create inotify instance: {}",
                   strerror(errno));
    }

    fmt::println("hot_reload: Add {} to watch list", fs::relative(REN_LIB_DIR));
    lib_watch_fd =
        inotify_add_watch(lib_inotify_fd, REN_LIB_DIR, IN_CLOSE_WRITE);
    if (lib_watch_fd == -1) {
      fmt::println("hot_reload: Failed to add inotify watch: {}",
                   strerror(errno));
    }
#else
    lib_timestamp = fs::last_write_time(REN_LIB_PATH);
#endif

    ren_try(fs::path load_path, make_dll_copy(REN_LIB_PATH));
    fmt::println("hot_reload: Load {}", fs::relative(load_path));
    lib_handle = SDL_LoadObject(load_path.string().c_str());
    if (!lib_handle) {
      fmt::println("hot_reload: Failed to load {}: {}", fs::relative(load_path),
                   SDL_GetError());
      return std::unexpected(Error::IO);
    }

    fmt::println("hot_reload: Fetch vtable");
    hot_reload::vtbl_ref =
        (const hot_reload::Vtbl *)SDL_LoadFunction(lib_handle, "ren_vtbl");
    ren_assert(hot_reload::vtbl_ref);
  }
  return hot_reload::vtbl_ref->create_renderer(scratch, arena, info);
}

auto draw(Scene *scene, const DrawInfo &draw_info) -> expected<void> {
  ren_try_to(hot_reload::vtbl_ref->draw(scene, draw_info));

#if __linux__
  if (lib_watch_fd == -1) {
    return {};
  }
  bool changed = false;
  while (true) {
    alignas(inotify_event) char buffer[2048];
    ssize_t count = read(lib_inotify_fd, buffer, sizeof(buffer));
    if (count == -1 and errno != EWOULDBLOCK) {
      fmt::println("hot_reload: Failed to get inotify update: {}: {}", errno,
                   strerror(errno));
      return std::unexpected(Error::IO);
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
    return {};
  }
#else
  fs::file_time_type new_timestamp = fs::last_write_time(REN_LIB_PATH);
  if (new_timestamp <= lib_timestamp) {
    return {};
  }
  lib_timestamp = new_timestamp;
#endif

  fmt::println("hot_reload: {} has changed, reload",
               fs::relative(REN_LIB_PATH));

  fmt::println("hot_reload: Run unload hook");
  hot_reload::vtbl_ref->unload(scene);

  fmt::println("hot_reload: Unload old DLL");
  SDL_UnloadObject(lib_handle);

  ren_try(fs::path load_path, make_dll_copy(REN_LIB_PATH));
  fmt::println("hot_reload: Load new DLL");
  lib_handle = SDL_LoadObject(load_path.string().c_str());
  if (!lib_handle) {
    fmt::println("hot_reload: Failed to load {}: {}", fs::relative(load_path),
                 SDL_GetError());
    return std::unexpected(Error::IO);
  }

  fmt::println("hot_reload: Fetch new vtable");
  hot_reload::vtbl_ref =
      (const hot_reload::Vtbl *)SDL_LoadFunction(lib_handle, "ren_vtbl");
  ren_assert(hot_reload::vtbl_ref);

  fmt::println("hot_reload: Run load hook");
  auto reload_res = hot_reload::vtbl_ref->load(scene);
  if (!reload_res) {
    fmt::println("hot_reload: Load hook failed");
    return reload_res;
  }

  fmt::println("hot_reload: Done");

  return {};
}

} // namespace ren
