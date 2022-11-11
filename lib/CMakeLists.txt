find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(range-v3 REQUIRED)

add_subdirectory(${PROJECT_SOURCE_DIR}/external/TrivialVector
                 ${CMAKE_CURRENT_BINARY_DIR}/TrivialVector)
add_subdirectory(Vulkan)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(Support INTERFACE TrivialVector Boost::container
                                        range-v3::range-v3)

add_library(Ren SHARED Ren.cpp)
target_include_directories(Ren PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(Ren PRIVATE Support)
