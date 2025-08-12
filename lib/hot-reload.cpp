#include "core/Assert.hpp"
#include "core/Result.hpp"
#include "core/StdDef.hpp"
#include "ren/ren.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/std.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ren {

const ren::hot_reload::Vtbl *ren::hot_reload::vtbl_ref = nullptr;

namespace {

void *lib_handle = nullptr;

int inotify_fd = -1;
int watch_fd = -1;

const fs::path LIB_PATH = fs::relative(fs::path(REN_LIBREN_PATH));
const fs::path LIB_DIR = fs::path(REN_LIBREN_PATH).parent_path();
const fs::path LIB_NAME = fs::path(REN_LIBREN_PATH).filename();

} // namespace

auto create_renderer(const RendererInfo &info) -> expected<Renderer *> {
  if (!hot_reload::vtbl_ref) {
    fmt::println("hot_reload: Open {}", LIB_PATH);

    lib_handle = dlopen(LIB_PATH.c_str(), RTLD_LAZY);
    if (!lib_handle) {
      fmt::println("hot_reload: Failed to open {}", LIB_PATH.c_str());
      return std::unexpected(Error::IO);
    }
    fmt::println("hot_reload: Fetch vtable");
    hot_reload::vtbl_ref =
        (const hot_reload::Vtbl *)dlsym(lib_handle, "ren_vtbl");
    ren_assert(hot_reload::vtbl_ref);

    fmt::println("hot_reload: Create inotify instance");
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd == -1) {
      fmt::println("hot_reload: Failed to create inotify instance: {}",
                   strerror(errno));
    }

    fmt::println("hot_reload: Add {} to watch list", LIB_DIR);
    watch_fd = inotify_add_watch(inotify_fd, LIB_DIR.c_str(), IN_CLOSE_WRITE);
    if (watch_fd == -1) {
      fmt::println("hot_reload: Failed to add inotify watch: {}",
                   strerror(errno));
    }
  }
  return hot_reload::vtbl_ref->create_renderer(info);
}

auto draw(Scene *scene) -> expected<void> {
  ren_try_to(hot_reload::vtbl_ref->draw(scene));

  bool changed = false;
  if (watch_fd != -1) {
    while (true) {
      alignas(inotify_event) char buffer[2048];
      ssize_t count = read(inotify_fd, buffer, sizeof(buffer));
      if (count == -1 and errno != EWOULDBLOCK) {
        fmt::println("hot_reload: Failed to get inotify update: {}: {}", errno,
                     strerror(errno));
      } else if (count > 0) {
        usize offset = 0;
        while (offset < count) {
          const auto *e = (const inotify_event *)&buffer[offset];
          if (e->len > 0 and e->name == LIB_NAME) {
            changed = true;
          }
          offset += sizeof(*e) + e->len;
        }
        continue;
      }
      break;
    }
  }
  if (not changed) {
    return {};
  }

  fmt::println("hot_reload: {} has changed, reload", LIB_PATH);

  fmt::println("hot_reload: Run unload hook");
  hot_reload::vtbl_ref->unload(scene);

  fmt::println("hot_reload: Unload old DLL");
  dlclose(lib_handle);

  fmt::println("hot_reload: Load new DLL");
  lib_handle = dlopen(LIB_PATH.c_str(), RTLD_NOW);
  if (!lib_handle) {
    fmt::println("hot_reload: Failed to open {}", LIB_PATH);
    return {};
  }

  fmt::println("hot_reload: Fetch new vtable");
  hot_reload::vtbl_ref =
      (const hot_reload::Vtbl *)dlsym(lib_handle, "ren_vtbl");
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
