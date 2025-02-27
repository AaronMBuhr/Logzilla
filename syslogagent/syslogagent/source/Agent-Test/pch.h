//
// pch.h
//

#pragma once

#include "gtest/gtest.h"
#include "gmock/gmock.h"

// Windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>

// STL headers
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

// Project headers
// Need to include relative path from Agent-Test to Agent project
