#pragma once
#include "Guid.hpp"
#include "ren/core/Arena.hpp"
#include "ren/core/Array.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/String.hpp"

#include <imgui.hpp>

namespace ren {

using FileDialogGuid = Guid64;

FileDialogGuid FileDialogGuidFromName(String8 title);

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
  FileDialogGuid guid;
  FileDialogType type = {};
  SDL_Window *modal_window = nullptr;
  /// Location where to start browsing.
  Path start_path;
  /// Force the location instead of using the previous one.
  bool force_path = false;
  Span<const FileDialogFilter> filters;
};

bool OpenFileDialog(const FileDialogOptions &options);

bool IsFileDialogOpen(FileDialogGuid guid);

bool IsFileDialogDone(FileDialogGuid guid);

/// Returned path is valid until the file dialog is closed.
Path FileDialogPath(FileDialogGuid guid);

void CloseFileDialog(FileDialogGuid guid);

inline Path FileDialogCopyPathAndClose(NotNull<Arena *> arena,
                                       FileDialogGuid guid) {
  Path path = FileDialogPath(guid).copy(arena);
  CloseFileDialog(guid);
  return path;
}

bool InputText(const char *label, NotNull<Arena *> arena,
               NotNull<DynamicArray<char> *> buf, ImGuiInputFlags flags = 0);

void InputPath(String8 label, NotNull<Arena *> arena,
               NotNull<DynamicArray<char> *> buffer,
               FileDialogOptions file_dialog_options);

} // namespace ren
