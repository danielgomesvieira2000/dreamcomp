cmake_minimum_required(VERSION 3.24)
project(dreamcomp VERSION 0.1.0 LANGUAGES C CXX)

# dreamcomp: a framework for native PC ports of Dreamcast games. The recompiler and the emulated
# hardware are the vendored engine (engine/, dream-recomp, GPL-2.0); this layer adds what a port
# needs on top. A port repository adds this directory and then calls dreamcomp_add_port().
# Architecture: docs/ARCHITECTURE.md. Decisions: docs/DECISIONS.md.

set(DREAMCOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "dreamcomp source root")

# Engine defaults for ports (set before the engine's own option() calls, which then keep them).
# D-003: the interpreter fallback ships, so a call target discovery missed degrades to slower
# code instead of a crash, and the miss is reported for the next config round.
set(DREAM_DEV_INTERPRETER ON CACHE BOOL "SH-4 interpreter fallback for untranslated code")
set(DREAM_WERROR OFF CACHE BOOL "Warnings as errors in engine targets (engine CI keeps this on)")
if(NOT PROJECT_IS_TOP_LEVEL)
  set(DREAM_BUILD_TESTS OFF CACHE BOOL "Engine unit tests (on when building dreamcomp itself)")
endif()

# Third-party binaries fetched by `tools/dc.py deps` land in .deps/; find them without flags.
file(GLOB _dc_sdl3 LIST_DIRECTORIES true "${DREAMCOMP_ROOT}/.deps/SDL3-*/cmake")
if(_dc_sdl3 AND NOT SDL3_DIR)
  list(GET _dc_sdl3 -1 _dc_sdl3)
  set(SDL3_DIR ${_dc_sdl3} CACHE PATH "SDL3 config directory")
endif()

add_subdirectory(engine)
include(cmake/DreamcompPort.cmake)
