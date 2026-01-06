add_library(app-base AppBase.cpp)
target_link_libraries(app-base PUBLIC ren::ren ren::baking ren::core fmt::fmt SDL3::SDL3)
target_compile_definitions(app-base PUBLIC SDL_MAIN_HANDLED)

add_library(imgui-app ImGuiApp.cpp)
target_link_libraries(imgui-app PUBLIC app-base imgui::imgui)

add_executable(create-device create-device.cpp)
target_link_libraries(create-device app-base)

add_executable(draw-triangle draw-triangle.cpp)
target_link_libraries(draw-triangle app-base)

add_executable(view-gltf view-gltf.cpp)
target_link_libraries(view-gltf imgui-app tinygltf)

add_executable(entity-stress-test entity-stress-test.cpp)
target_link_libraries(entity-stress-test imgui-app)
