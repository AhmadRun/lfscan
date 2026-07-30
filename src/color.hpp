#pragma once

#include <ostream>

#ifdef _WIN32
#define LF_USE_COLOR 0
#else
#define LF_USE_COLOR 1
#endif

namespace color {
#if LF_USE_COLOR
inline constexpr auto reset   = "\033[0m";
inline constexpr auto bold    = "\033[1m";
inline constexpr auto dim     = "\033[2m";
inline constexpr auto red     = "\033[31m";
inline constexpr auto green   = "\033[32m";
inline constexpr auto yellow  = "\033[33m";
inline constexpr auto blue    = "\033[34m";
inline constexpr auto magenta = "\033[35m";
inline constexpr auto cyan    = "\033[36m";
inline constexpr auto grey    = "\033[37m";
#else
inline constexpr auto reset   = "";
inline constexpr auto bold    = "";
inline constexpr auto dim     = "";
inline constexpr auto red     = "";
inline constexpr auto green   = "";
inline constexpr auto yellow  = "";
inline constexpr auto blue    = "";
inline constexpr auto magenta = "";
inline constexpr auto cyan    = "";
inline constexpr auto grey    = "";
#endif

inline bool enabled = true;
} // namespace color
