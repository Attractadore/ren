#pragma once

#if _WIN32
#define REN_DLL_EXPORT __declspec(dllexport)
#else
#define REN_DLL_EXPORT __attribute__((visibility("default")))
#endif
