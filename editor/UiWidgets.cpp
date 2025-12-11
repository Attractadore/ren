#include "UiWidgets.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Thread.hpp"

#include <SDL3/SDL_dialog.h>
#include <atomic>
#include <blake3.h>
#include <tracy/Tracy.hpp>

namespace ren {

static Arena s_dialog_arena;
static Arena s_opened_dialog_arena;

struct FileDialog {
  FileDialogGuid guid;
  String8 title;
};

static DynamicArray<FileDialog> s_dialogs;

static FileDialog *s_opened_dialog = nullptr;
static bool s_opened_dialog_done = false;
static Path s_opened_dialog_path;

FileDialogGuid FileDialogGuidFromName(String8 title) {
  ZoneScoped;

  ren_assert(is_main_thread());

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, title.m_str, title.m_size);
  FileDialogGuid guid;
  blake3_hasher_finalize(&hasher, guid.m_data, sizeof(guid));

  for (FileDialog dialog : s_dialogs) {
    if (dialog.guid == guid) {
      return guid;
    }
  }

  bool prev_hash = false;
  usize display_title_size = title.m_size;

  for (usize i : range(title.m_size)) {
    if (title[i] == '#' and prev_hash) {
      display_title_size = i - 1;
      break;
    }
    prev_hash = title[i] == '#';
  }
  String8 display_title = title.substr(0, display_title_size);

  [[unlikely]] if (!s_dialog_arena) { s_dialog_arena = Arena::init(); }
  s_dialogs.push(&s_dialog_arena,
                 {
                     .guid = guid,
                     .title = display_title.copy(&s_dialog_arena),
                 });

  return guid;
}

static void SDLCALL open_file_dialog_callback(FileDialogType type,
                                              const char *const *filelist,
                                              int filter) {
  if (not is_main_thread()) {
    ScratchArena::init_for_thread();
  }
  if (!filelist) {
    const char *what = type == FileDialogType::OpenFolder ? "folder" : "file";
    fmt::println(stderr, "Failed to select {}: {}", what, SDL_GetError());
  } else {
    const char *file = *filelist;
    if (file) {
      s_opened_dialog_path =
          Path::init(&s_opened_dialog_arena, String8::init(file));
    }
  }
  std::atomic_ref(s_opened_dialog_done).store(true, std::memory_order_release);
  if (not is_main_thread()) {
    ScratchArena::destroy_for_thread();
  }
}

bool OpenFileDialog(const FileDialogOptions &options) {
  ren_assert(is_main_thread());

  if (s_opened_dialog) {
    return false;
  }

  ScratchArena scratch;

  for (FileDialog &dialog : s_dialogs) {
    if (dialog.guid == options.guid) {
      s_opened_dialog = &dialog;
      break;
    }
  }

  [[unlikely]] if (!s_opened_dialog_arena) {
    s_opened_dialog_arena = Arena::init();
  }

  SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                         options.modal_window);
  if (options.location) {
    SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                          options.location.m_str.zero_terminated(scratch));
  }

  SDL_DialogFileFilter *dialog_filters = nullptr;
  if (options.filters.m_size > 0) {
    dialog_filters = s_opened_dialog_arena.allocate<SDL_DialogFileFilter>(
        options.filters.m_size);
    for (usize i : range(options.filters.m_size)) {
      FileDialogFilter filter = options.filters[i];
      dialog_filters[i] = {
          .name = filter.name.zero_terminated(&s_dialog_arena),
          .pattern = filter.pattern.zero_terminated(&s_dialog_arena),
      };
    }
    SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           dialog_filters);
    SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          options.filters.m_size);
  }

  switch (options.type) {
  case FileDialogType::OpenFile: {
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_OPENFILE,
        [](void *userdata, const char *const *filelist, int filter) {
          open_file_dialog_callback(FileDialogType::OpenFile, filelist, filter);
        },
        nullptr, properties);
  } break;
  case FileDialogType::SaveFile:
    ren_assert_msg(false, "Not implemented");
  case FileDialogType::OpenFolder:
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_OPENFOLDER,
        [](void *userdata, const char *const *filelist, int filter) {
          open_file_dialog_callback(FileDialogType::OpenFolder, filelist,
                                    filter);
        },
        nullptr, properties);
    break;
  }
  SDL_DestroyProperties(properties);

  return true;
}

bool IsFileDialogOpen(FileDialogGuid guid) {
  ren_assert(is_main_thread());
  return s_opened_dialog and s_opened_dialog->guid == guid;
}

bool IsFileDialogDone(FileDialogGuid guid) {
  ren_assert(is_main_thread());
  if (not IsFileDialogOpen(guid)) {
    return false;
  }
  return std::atomic_ref(s_opened_dialog_done).load(std::memory_order_acquire);
}

Path FileDialogPath(FileDialogGuid guid) {
  ren_assert(IsFileDialogDone(guid));
  return s_opened_dialog_path;
}

void CloseFileDialog(FileDialogGuid guid) {
  ren_assert(IsFileDialogDone(guid));
  s_opened_dialog = nullptr;
  s_opened_dialog_done = false;
  s_opened_dialog_path = {};
  s_opened_dialog_arena.clear();
}

struct InputTextCallback_UserData {
  Arena *arena = nullptr;
  DynamicArray<char> *buf = nullptr;
};

bool InputText(const char *label, NotNull<Arena *> arena,
               NotNull<DynamicArray<char> *> buf, ImGuiInputFlags flags) {
  flags |= ImGuiInputTextFlags_CallbackResize;
  InputTextCallback_UserData user_data = {.arena = arena, .buf = buf};
  return ImGui::InputText(
      label, buf->m_data, buf->m_capacity, flags,
      [](ImGuiInputTextCallbackData *data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
          auto [arena, buf] = *(InputTextCallback_UserData *)data->UserData;
          while (buf->m_size < (u32)data->BufTextLen + 1) {
            buf->push(arena, 0);
          }
          buf->m_size = data->BufTextLen + 1;
          (*buf)[data->BufTextLen] = 0;
          data->Buf = buf->m_data;
        }
        return 0;
      },
      &user_data);
}

void InputPath(String8 name, NotNull<Arena *> arena,
               NotNull<DynamicArray<char> *> buffer,
               FileDialogOptions file_dialog_options) {
  ScratchArena scratch;

  if (buffer->m_size == 0) {
    String8 path = file_dialog_options.location.m_str;
    buffer->push(arena, path.m_str, path.m_size);
    buffer->push(arena, 0);
  }

  if (IsFileDialogDone(file_dialog_options.guid)) {
    Path path = FileDialogCopyPathAndClose(scratch, file_dialog_options.guid);
    if (path) {
      buffer->clear();
      auto [str, size] = path.m_str;
      buffer->push(arena, str, size);
      buffer->push(arena, 0);
    }
  }

  ImGui::Text("%.*s:", (int)name.m_size, name.m_str);
  InputText(format(scratch, "##{}", name).zero_terminated(scratch), arena,
            buffer);
  ImGui::SameLine();
  ImGui::BeginDisabled(IsFileDialogOpen(file_dialog_options.guid));
  if (ImGui::Button("Browse...")) {
    Path location = Path::init(scratch, String8::init(buffer->m_data));
    if (location) {
      file_dialog_options.location = location;
      file_dialog_options.force_location = true;
    }
    OpenFileDialog(file_dialog_options);
  }
  ImGui::EndDisabled();
}

} // namespace ren
