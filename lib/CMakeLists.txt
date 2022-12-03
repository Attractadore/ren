set(REN_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(range-v3 REQUIRED)
add_subdirectory(${EXTERNAL_DIR}/SlotMap ${CMAKE_CURRENT_BINARY_DIR}/SlotMap)

add_subdirectory(Vulkan)
if(WIN32)
  add_subdirectory(DirectX12)
endif()

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(Support INTERFACE SlotMap Boost::container
                                        range-v3::range-v3)

add_library(ren SHARED ren.cpp RenderGraph.cpp Scene.cpp)
target_include_directories(ren PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(ren PRIVATE Support)

target_sources(
  ren
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren.h
         ${REN_INCLUDE}/ren/ren.hpp)
add_library(ren::ren ALIAS ren)

include(GNUInstallDirs)
install(
  TARGETS ren
  EXPORT ${REN_TARGETS}
  FILE_SET HEADERS)
install(
  EXPORT ${REN_TARGETS}
  NAMESPACE ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
