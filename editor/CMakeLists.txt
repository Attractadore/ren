add_executable(ren-editor
  editor.cpp
  LinuxFileWatcher.cpp
  Meta.cpp
  UiWidgets.cpp
  Win32FileWatcher.cpp
)
target_link_libraries(ren-editor PRIVATE ren::ren ren::core imgui SDL3::SDL3 assimp::assimp BLAKE3::blake3 ren::baking)
target_compile_definitions(ren-editor PRIVATE SDL_MAIN_HANDLED)
