find_package(SDL2 REQUIRED)

if(TARGET ren::vk)
  find_package(Vulkan REQUIRED)

  add_executable(CreateDevice-Vulkan CreateDevice-Vulkan.cpp)
  target_link_libraries(CreateDevice-Vulkan PRIVATE ren::vk SDL2::SDL2
                                                    Vulkan::Vulkan)
  target_compile_features(CreateDevice-Vulkan PRIVATE cxx_std_20)
  target_compile_definitions(CreateDevice-Vulkan PRIVATE SDL_MAIN_HANDLED)

  if(WIN32)
    add_custom_command(
      TARGET CreateDevice-Vulkan
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:CreateDevice-Vulkan>
        $<TARGET_FILE_DIR:CreateDevice-Vulkan>
      COMMAND_EXPAND_LISTS)
  endif()
endif()

if(TARGET ren::dx12)
  add_executable(CreateDevice-DirectX12 CreateDevice-DirectX12.cpp)
  target_link_libraries(CreateDevice-DirectX12 PRIVATE ren::dx12 SDL2::SDL2)
  target_compile_features(CreateDevice-DirectX12 PRIVATE cxx_std_20)
  target_compile_definitions(CreateDevice-DirectX12 PRIVATE SDL_MAIN_HANDLED)

  if(WIN32)
    add_custom_command(
      TARGET CreateDevice-DirectX12
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:CreateDevice-DirectX12>
        $<TARGET_FILE_DIR:CreateDevice-DirectX12>
      COMMAND_EXPAND_LISTS)
  endif()
endif()
