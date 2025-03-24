add_library(tiny_imageformat INTERFACE)
target_include_directories(tiny_imageformat INTERFACE ${CMAKE_CURRENT_LIST_DIR}/tiny_imageformat/include ${REN_INCLUDE})

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_subdirectory(SDL)
else()
  find_package(SDL2 REQUIRED GLOBAL)
endif()

if (REN_BUILD_IMGUI_PLUGIN)
  add_library(imgui
    imgui/backends/imgui_impl_sdl2.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
  )
  target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/imgui ${CMAKE_CURRENT_LIST_DIR}/imgui/backends)
  target_link_libraries(imgui PRIVATE SDL2::SDL2)
  add_library(imgui::imgui ALIAS imgui)
endif()

add_subdirectory(tracy)
