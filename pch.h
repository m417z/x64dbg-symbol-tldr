#pragma once

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <string_view>

// MSVS regex is broken, so we use Boost instead.
// https://developercommunity.visualstudio.com/t/grouping-within-repetition-causes-regex-stack-erro/885115
#include <boost/regex.hpp>

#include "pluginsdk/_plugins.h"
