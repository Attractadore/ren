find_package(assimp REQUIRED)
find_package(Boost REQUIRED COMPONENTS random)
find_package(SDL2 REQUIRED)
find_package(cxxopts REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(imgui REQUIRED)

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
add_library(tinygltf tinygltf.cpp)
target_include_directories(tinygltf PUBLIC ${TINYGLTF_INCLUDE_DIRS})
target_compile_definitions(
  tinygltf PUBLIC TINYGLTF_USE_CPP14 TINYGLTF_NO_STB_IMAGE_WRITE
                  TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE)

add_library(app-base AppBase.cpp)
target_link_libraries(app-base PUBLIC ren::ren fmt::fmt)
target_compile_definitions(app-base PUBLIC SDL_MAIN_HANDLED)
if (MSVC)
  target_compile_options(app-base PUBLIC /Zc:preprocessor)
endif()

add_library(imgui-app ImGuiApp.cpp)
target_link_libraries(
  imgui-app
  PUBLIC app-base imgui::imgui
  PRIVATE ren::imgui)

add_executable(create-device create-device.cpp)
target_link_libraries(create-device app-base)

add_executable(draw-triangle draw-triangle.cpp)
target_link_libraries(draw-triangle app-base)

add_executable(view-gltf view-gltf.cpp)
target_link_libraries(
  view-gltf
  imgui-app
  Boost::boost
  cxxopts::cxxopts
  tinygltf)

add_executable(entity-stress-test entity-stress-test.cpp)
target_link_libraries(entity-stress-test imgui-app assimp::assimp cxxopts::cxxopts Boost::random)
