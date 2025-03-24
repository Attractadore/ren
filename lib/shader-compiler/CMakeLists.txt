find_package(glslang REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)

add_executable(ren-shader-compiler shader-compiler.cpp)
add_executable(ren::shader-compiler ALIAS ren-shader-compiler)
target_link_libraries(ren-shader-compiler
  ren-core
  glslang::glslang
  glslang::glslang-default-resource-limits
  cxxopts::cxxopts
  unofficial::spirv-reflect)
target_compile_features(ren-shader-compiler PUBLIC cxx_std_23)
