cmake_minimum_required(VERSION 3.23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

project(ren VERSION 0.1.0)

include(CTest)

set(REN_MASTER_PROJECT OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(REN_MASTER_PROJECT ON)
endif()

option(REN_INSTALL "Generate the install target" ${REN_MASTER_PROJECT})
option(REN_BUILD_TESTS "Build tests" $CACHE{BUILD_TESTING})
option(REN_BUILD_EXAMPLES "Build example excutables" FALSE)

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(REN_ASSETS_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/assets)

set(REN_TARGETS ${PROJECT_NAME}-targets)

add_subdirectory(external)
add_subdirectory(lib)

if(REN_BUILD_TESTS)
  message(STATUS "Build tests")
  add_subdirectory(tests)
endif()

if(REN_BUILD_EXAMPLES)
  message(STATUS "Build examples")
  add_subdirectory(examples)
endif()

if(REN_INSTALL)
  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)
  set(CONFIG_CMAKE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake)
  set(CONFIG_VERSION
      ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake)
  file(WRITE ${CONFIG_CMAKE}
       "include(\${CMAKE_CURRENT_LIST_DIR}/${REN_TARGETS}.cmake)")
  write_basic_package_version_file(${CONFIG_VERSION}
                                   COMPATIBILITY SameMinorVersion)
  install(FILES ${CONFIG_CMAKE} ${CONFIG_VERSION}
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
