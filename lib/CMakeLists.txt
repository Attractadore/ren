set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)

set(REN_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(
  Boost
  COMPONENTS container headers
  REQUIRED)
find_package(range-v3 REQUIRED)
find_package(fmt REQUIRED)
add_subdirectory(${EXTERNAL_DIR}/SlotMap ${CMAKE_CURRENT_BINARY_DIR}/SlotMap)

message(STATUS "Enable Vulkan renderer")
add_subdirectory(Vulkan)
if(WIN32)
  message(STATUS "Enable DirectX 12 renderer")
  add_subdirectory(DirectX12)
endif()

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(Support INTERFACE SlotMap Boost::container Boost::headers
                                        range-v3::range-v3 fmt::fmt)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(Support INTERFACE REN_LOG_${SEVERITY})
endforeach()

if(REN_ERROR_LOG)
  message(STATUS "Log errors")
endif()

add_library(ren SHARED ren.cpp CommandAllocator.cpp RenderGraph.cpp Scene.cpp)
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
