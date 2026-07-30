cmake_minimum_required(VERSION 3.16)
project(lfscan VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── CLI11 ──────────────────────────────────────────────────────────
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.0
)
FetchContent_MakeAvailable(cli11)

# ── SQLite3 amalgamation ───────────────────────────────────────────
# If system sqlite3 is available, use it; otherwise download the amalgamation
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  set(SQLITE_TARGET SQLite::SQLite3)
  message(STATUS "Found system SQLite3: ${SQLite3_VERSION}")
else()
  include(FetchContent)
  FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    SOURCE_SUBDIR NONE
  )
  FetchContent_MakeAvailable(sqlite3)
  add_library(sqlite3_amalg OBJECT "${sqlite3_SOURCE_DIR}/sqlite3.c")
  target_include_directories(sqlite3_amalg PUBLIC "${sqlite3_SOURCE_DIR}")
  set(SQLITE_TARGET sqlite3_amalg)
  message(STATUS "Building with bundled SQLite3 amalgamation")
endif()

# ── FTXUI (TUI) ─────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# ── lfscan executable ──────────────────────────────────────────────
add_executable(lfscan
  src/main.cpp
  src/scanner.cpp
  src/age_detector.cpp
  src/pattern_detector.cpp
  src/broken_symlink_detector.cpp
  src/mount_checker.cpp
  src/open_file_checker.cpp
  src/orphaned_package_detector.cpp
  src/stale_log_detector.cpp
  src/config.cpp
  src/quarantine.cpp
  src/reporter.cpp
  src/cleaner_registry.cpp
  src/container_cleaner.cpp
  src/flatpak_cleaner.cpp
  src/history_db.cpp
  src/journal_cleaner.cpp
  src/language_cache_cleaner.cpp
  src/quarantine_restore.cpp
  src/snap_cleaner.cpp
  src/tui_mode.cpp
)

target_link_libraries(lfscan PRIVATE CLI11::CLI11 ${SQLITE_TARGET} ftxui::screen ftxui::dom ftxui::component)
target_include_directories(lfscan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
