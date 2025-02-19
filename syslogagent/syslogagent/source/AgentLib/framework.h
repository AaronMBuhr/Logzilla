#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
// Project-wide definitions
#ifdef AGENTLIB_EXPORTS
#define AGENTLIB_API __declspec(dllexport)
#else
#define AGENTLIB_API __declspec(dllimport)
#endif
