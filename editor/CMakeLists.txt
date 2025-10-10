add_executable(ren-editor editor.cpp)
target_link_libraries(ren-editor PRIVATE ren::ren ren::core SDL3::SDL3)
target_compile_definitions(ren-editor PRIVATE SDL_MAIN_HANDLED)
