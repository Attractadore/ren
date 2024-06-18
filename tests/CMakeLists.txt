find_package(GTest REQUIRED)
include(GoogleTest)

function(ren_add_test target)
  add_executable(${target} ${target}.cpp)
  target_link_libraries(${target} GTest::gtest_main)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/lib)
  gtest_discover_tests(${target} DISCOVERY_TIMEOUT 20)
endfunction()
