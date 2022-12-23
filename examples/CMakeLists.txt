find_package(SDL2 REQUIRED)

add_library(app-base app-base.cpp)
target_link_libraries(
  app-base
  PUBLIC ren::ren SDL2::SDL2
  PRIVATE fmt::fmt)
target_compile_definitions(app-base PUBLIC SDL_MAIN_HANDLED)

if(TARGET ren::vk)
  find_package(Vulkan REQUIRED)

  target_link_libraries(app-base PUBLIC ren::vk Vulkan::Vulkan)
  target_compile_definitions(app-base PUBLIC REN_VULKAN_RENDERER)
endif()

if(TARGET ren::dx12)
  target_link_libraries(app-base PUBLIC ren::dx12)
  target_compile_definitions(app-base PUBLIC REN_DIRECTX12_RENDERER)
endif()

add_executable(create-device create-device.cpp)
target_link_libraries(create-device app-base)
if(WIN32)
  add_custom_command(
    TARGET create-device
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:create-device>
            $<TARGET_FILE_DIR:create-device>
    COMMAND_EXPAND_LISTS)
endif()
