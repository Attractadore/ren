set(REN_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(range-v3 REQUIRED)
add_subdirectory(${EXTERNAL_DIR}/SlotMap ${CMAKE_CURRENT_BINARY_DIR}/SlotMap)

add_subdirectory(Vulkan)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(Support INTERFACE SlotMap Boost::container range-v3::range-v3)

add_library(ren SHARED ren.cpp Scene.cpp RenderGraph.cpp)
target_include_directories(ren PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(ren PRIVATE Support)
