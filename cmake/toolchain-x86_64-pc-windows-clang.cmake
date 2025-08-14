set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER_TYPE LLD)

include_directories(SYSTEM ~/msvc/crt/include ~/msvc/sdk/include/ucrt ~/msvc/sdk/include/um ~/msvc/sdk/include/shared ~/msvc/sdk/include/winrt)
link_directories(BEFORE ~/msvc/crt/lib/x64 ~/msvc/sdk/lib/ucrt/x64 ~/msvc/sdk/lib/um/x64)

# To fix assimp build with llvm-rc
set(CMAKE_RC_FLAGS_INIT "/C 1252")

# To execute shader compiler
set(CMAKE_CROSSCOMPILING_EMULATOR wine)
