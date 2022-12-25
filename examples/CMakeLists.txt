find_package(SDL2 REQUIRED)
find_package(fmt REQUIRED)

function(copy_dlls target)
  if(WIN32)
    # copy will fail if target doesn't depend on any dlls
    set(dummy_dll ${CMAKE_CURRENT_BINARY_DIR}/dummy-dll)
    file(TOUCH ${dummy_dll})
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dummy_dll}
              $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

add_library(app-base app-base.cpp)
target_link_libraries(
  app-base
  PUBLIC ren::ren-hpp SDL2::SDL2
  PRIVATE fmt::fmt)
target_compile_definitions(app-base PUBLIC SDL_MAIN_HANDLED)

if(TARGET ren::vk)
  find_package(Vulkan REQUIRED)

  target_link_libraries(app-base PUBLIC ren::vk-hpp Vulkan::Vulkan)
  target_compile_definitions(app-base PUBLIC REN_VULKAN_RENDERER)
endif()

if(TARGET ren::dx12)
  target_link_libraries(app-base PUBLIC ren::dx12-hpp)
  target_compile_definitions(app-base PUBLIC REN_DIRECTX12_RENDERER)
endif()

add_executable(create-device create-device.cpp)
target_link_libraries(create-device app-base)
copy_dlls(create-device)

add_executable(draw-triangle draw-triangle.cpp)
target_link_libraries(draw-triangle app-base)
copy_dlls(draw-triangle)
