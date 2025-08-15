#pragma once

#if _WIN32

#define REN_DLL_EXPORT __declspec(dllexport)
#define REN_DLL_IMPORT __declspec(dllimport)
#define REN_DLL_LOCAL

#else

#define REN_DLL_EXPORT __attribute__((visibility("default")))
#define REN_DLL_IMPORT __attribute__((visibility("default")))
#define REN_DLL_LOCAL __attribute__((visibility("hidden")))

#endif
