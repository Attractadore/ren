add_executable(ren-editor main.cpp
  AssetCompiler.cpp
  Assets.cpp
  AssetWatcher.cpp
  Editor.cpp
  EditorUI.cpp
  Meta.cpp
  UIWidgets.cpp
)
target_link_libraries(ren-editor PRIVATE ren::ren ren::core imgui SDL3::SDL3 BLAKE3::blake3 ren::baking)
target_compile_definitions(ren-editor PRIVATE SDL_MAIN_HANDLED)
