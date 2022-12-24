set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)

set(REN_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(range-v3 REQUIRED)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(Support INTERFACE SlotMap Boost::boost Boost::container
                                        range-v3::range-v3 fmt::fmt glm::glm)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(Support INTERFACE REN_LOG_${SEVERITY})
endforeach()

if(REN_ERROR_LOG)
  message(STATUS "Log errors")
endif()

add_library(ren ren.cpp RenderGraph.cpp Scene.cpp)
target_sources(
  ren
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren.h
         ${REN_INCLUDE}/ren/ren.hpp)
target_link_libraries(ren PRIVATE Support)

if(BUILD_SHARED_LIBS)
  add_library(ren::ren ALIAS ren)
else()
  include(BundleDependencies)
  bundle_dependencies(ren ren-bundled)
  set_target_properties(ren-bundled PROPERTIES EXPORT_NAME "ren")
  add_library(ren::ren ALIAS ren-bundled)
endif()

message(STATUS "Enable Vulkan renderer")
add_subdirectory(Vulkan)
if(WIN32)
  message(STATUS "Enable DirectX 12 renderer")
  add_subdirectory(DirectX12)
endif()

if(REN_INSTALL)
  include(GNUInstallDirs)
  get_target_property(ren_target ren::ren ALIASED_TARGET)
  install(
    TARGETS ${ren_target}
    EXPORT ${REN_TARGETS}
    FILE_SET HEADERS)
  if(NOT BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PROPERTY:${ren_target},ARCHIVE_OUTPUT_NAME> TYPE LIB)
  endif()
  install(
    EXPORT ${REN_TARGETS}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
