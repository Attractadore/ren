find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(CreateDevice-Vulkan CreateDevice-Vulkan.cpp)
target_link_libraries(CreateDevice-Vulkan PRIVATE ren-vk SDL2::SDL2
                                                  Vulkan::Vulkan)
target_compile_features(CreateDevice-Vulkan PRIVATE cxx_std_20)
target_compile_definitions(CreateDevice-Vulkan PRIVATE SDL_MAIN_HANDLED)

add_custom_command(
  TARGET CreateDevice-Vulkan
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:CreateDevice-Vulkan>
          $<TARGET_FILE_DIR:CreateDevice-Vulkan>
  COMMAND_EXPAND_LISTS)
