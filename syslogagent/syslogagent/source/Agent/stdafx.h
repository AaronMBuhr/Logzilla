// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently

#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.
#include "targetver.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows headers must come first
#include <winsock2.h>
#include <windows.h>

// Standard C/C++ headers
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <wctype.h>
#include <memory>
#include <sstream>

// Third-party headers
#include "pugixml.hpp"

// Project headers
#include "Util.h"
