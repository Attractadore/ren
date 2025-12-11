#pragma once
#include "ren/core/String.hpp"

namespace ren {

enum class FileDialogType {
  OpenFile,
  SaveFile,
  OpenFolder,
};

struct FileDialogFilter {
  String8 name;
  String8 pattern;
};

struct FileDialogOptions {
  FileDialogType type = {};
  const char *location = nullptr;
  bool force_location = false;
  Span<const FileDialogFilter> filters;
};

void open_file_dialog(const FileDialogOptions &settings) {
  if (ctx->m_ui.m_dialog_active) {
    return;
  }
  SDL_DialogFileFilter *dialog_filters = nullptr;
  if (settings.filters.m_size > 0) {
    dialog_filters = ctx->m_dialog_arena.allocate<SDL_DialogFileFilter>(
        settings.filters.m_size);
    for (usize i : range(settings.filters.m_size)) {
      FileDialogFilter filter = settings.filters[i];
      dialog_filters[i] = {
          .name = filter.name.zero_terminated(&ctx->m_dialog_arena),
          .pattern = filter.pattern.zero_terminated(&ctx->m_dialog_arena),
      };
    }
    SDL_SetPointerProperty(settings.properties,
                           SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           dialog_filters);
    SDL_SetNumberProperty(settings.properties,
                          SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          settings.filters.m_size);
  }
  ctx->m_ui.m_dialog_client = settings.client;
  ctx->m_ui.m_dialog_active = true;
  switch (settings.type) {
  case SDL_FILEDIALOG_OPENFILE: {
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE,
                                     open_file_dialog_callback, ctx,
                                     settings.properties);
  } break;
  case SDL_FILEDIALOG_SAVEFILE:
    ren_assert_msg(false, "Not implemented");
  case SDL_FILEDIALOG_OPENFOLDER:
    ren_assert(settings.location);
    SDL_SetStringProperty(settings.properties,
                          SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                          settings.location);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,
                                     open_folder_dialog_callback, ctx,
                                     settings.properties);
    break;
  }
  SDL_SetPointerProperty(settings.properties,
                         SDL_PROP_FILE_DIALOG_FILTERS_POINTER, nullptr);
  SDL_SetNumberProperty(settings.properties,
                        SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 0);
  SDL_SetStringProperty(settings.properties,
                        SDL_PROP_FILE_DIALOG_LOCATION_STRING, nullptr);
}

} // namespace ren
