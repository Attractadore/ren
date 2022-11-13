find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(CreateDevice-Vulkan CreateDevice-Vulkan.cpp)
target_link_libraries(CreateDevice-Vulkan PRIVATE ren-vk SDL2::SDL2 Vulkan::Vulkan)
target_compile_features(CreateDevice-Vulkan PRIVATE cxx_std_20)
