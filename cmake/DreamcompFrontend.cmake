# The player-facing launcher (docs/FRONTEND.md): an RmlUi window that opens before the game when a
# port executable starts with no arguments or with --launcher.
#
# Included from the root CMakeLists.txt when DREAMCOMP_FRONTEND is on and SDL3 is found. Nothing
# third-party is committed: FreeType and RmlUi are fetched at configure time at pinned tags, the
# fonts are downloaded and checked against their SHA-256. Licences: THIRD_PARTY_NOTICES.md.
#
# Defines:
#   dreamcomp_frontend           static library (launcher + RmlUi SDL3 backends)
#   DREAMCOMP_FRONTEND_FONTS_DIR the downloaded fonts and their licence files
# dreamcomp_add_port() links the library and copies frontend/ plus the fonts next to the exe.

include(FetchContent)

# Silences a third-party target's warnings (cl and clang-cl spell it /w, everything else -w).
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(DREAMCOMP_NO_WARNINGS /w)
else()
  set(DREAMCOMP_NO_WARNINGS -w)
endif()

set(DREAMCOMP_FREETYPE_TAG VER-2-14-3)
set(DREAMCOMP_RMLUI_TAG 6.3)

# Variables set inside a function stay local, so BUILD_SHARED_LIBS and the dependency options do
# not leak into the engine or the port.
function(_dreamcomp_fetch_frontend_deps)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  # Third-party code is not ours to fix: no warnings-as-errors, quiet warnings.
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

  # FreeType: only the TrueType path is needed (fonts are plain .ttf).
  set(FT_DISABLE_ZLIB ON)
  set(FT_DISABLE_BZIP2 ON)
  set(FT_DISABLE_PNG ON)
  set(FT_DISABLE_HARFBUZZ ON)
  set(FT_DISABLE_BROTLI ON)
  set(SKIP_INSTALL_ALL ON)
  FetchContent_Declare(dreamcomp_freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG ${DREAMCOMP_FREETYPE_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE)
  FetchContent_MakeAvailable(dreamcomp_freetype)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()
  # RmlUi calls find_package(Freetype); the target above is what it actually checks for. Stop the
  # module from picking up some other FreeType installed on the machine.
  set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)

  set(RMLUI_SAMPLES OFF)
  set(BUILD_TESTING OFF)
  set(RMLUI_FONT_ENGINE freetype)
  set(RMLUI_LUA_BINDINGS OFF)
  set(RMLUI_PRECOMPILED_HEADERS OFF)
  set(RMLUI_COMPILER_OPTIONS OFF)
  set(RMLUI_INSTALL_RUNTIME_DEPENDENCIES OFF)
  FetchContent_Declare(dreamcomp_rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG ${DREAMCOMP_RMLUI_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE)
  FetchContent_MakeAvailable(dreamcomp_rmlui)
  # Third-party code: its warnings are not ours to act on, keep the build log readable.
  foreach(t freetype rmlui_core rmlui_debugger)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE ${DREAMCOMP_NO_WARNINGS})
    endif()
  endforeach()
  set(DREAMCOMP_RMLUI_SOURCE_DIR ${dreamcomp_rmlui_SOURCE_DIR} PARENT_SCOPE)
endfunction()
_dreamcomp_fetch_frontend_deps()

# Fonts (SIL OFL 1.1). Downloaded once per build tree and verified; only the files the launcher
# uses are kept, with their licences.
set(DREAMCOMP_FRONTEND_FONTS_DIR ${CMAKE_BINARY_DIR}/dreamcomp_fonts CACHE INTERNAL "")
function(_dreamcomp_fetch_font name url sha256)
  set(zip ${CMAKE_BINARY_DIR}/_deps/${name}.zip)
  set(stamp ${DREAMCOMP_FRONTEND_FONTS_DIR}/${name}.stamp)
  if(EXISTS ${stamp})
    return()
  endif()
  message(STATUS "dreamcomp: downloading ${name} font")
  file(DOWNLOAD ${url} ${zip} EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS st)
  list(GET st 0 code)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "dreamcomp: download of ${url} failed: ${st}")
  endif()
  set(tmp ${CMAKE_BINARY_DIR}/_deps/${name}_extract)
  file(REMOVE_RECURSE ${tmp})
  file(ARCHIVE_EXTRACT INPUT ${zip} DESTINATION ${tmp})
  set(${name}_EXTRACTED ${tmp} PARENT_SCOPE)
endfunction()

if(NOT EXISTS ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter.stamp)
  _dreamcomp_fetch_font(Inter
    https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
    9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e)
  file(MAKE_DIRECTORY ${DREAMCOMP_FRONTEND_FONTS_DIR})
  foreach(f Inter-Regular.ttf Inter-SemiBold.ttf Inter-Bold.ttf)
    file(COPY_FILE ${Inter_EXTRACTED}/extras/ttf/${f} ${DREAMCOMP_FRONTEND_FONTS_DIR}/${f})
  endforeach()
  file(COPY_FILE ${Inter_EXTRACTED}/LICENSE.txt ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter-LICENSE.txt)
  file(REMOVE_RECURSE ${Inter_EXTRACTED})
  file(WRITE ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter.stamp "4.1\n")
endif()
if(NOT EXISTS ${DREAMCOMP_FRONTEND_FONTS_DIR}/PromptFont.stamp)
  _dreamcomp_fetch_font(PromptFont
    https://github.com/Shinmera/promptfont/releases/download/v1.10/promptfont.zip
    8fbfd3fbdeecccbd8c73042679ec5d0573c591c0516e2a61170fd4fb9975ae7d)
  file(MAKE_DIRECTORY ${DREAMCOMP_FRONTEND_FONTS_DIR})
  file(GLOB_RECURSE _pf_ttf ${PromptFont_EXTRACTED}/promptfont.ttf)
  file(GLOB_RECURSE _pf_lic ${PromptFont_EXTRACTED}/LICENSE.txt)
  if(NOT _pf_ttf OR NOT _pf_lic)
    message(FATAL_ERROR "dreamcomp: promptfont.zip has no promptfont.ttf / LICENSE.txt")
  endif()
  list(GET _pf_ttf 0 _pf_ttf)
  list(GET _pf_lic 0 _pf_lic)
  file(COPY_FILE ${_pf_ttf} ${DREAMCOMP_FRONTEND_FONTS_DIR}/promptfont.ttf)
  file(COPY_FILE ${_pf_lic} ${DREAMCOMP_FRONTEND_FONTS_DIR}/PromptFont-LICENSE.txt)
  file(REMOVE_RECURSE ${PromptFont_EXTRACTED})
  file(WRITE ${DREAMCOMP_FRONTEND_FONTS_DIR}/PromptFont.stamp "1.10\n")
endif()

set(_be ${DREAMCOMP_RMLUI_SOURCE_DIR}/Backends)
add_library(dreamcomp_frontend STATIC
  ${DREAMCOMP_ROOT}/src/frontend/launcher.cpp
  ${DREAMCOMP_ROOT}/src/frontend/menu_ui.cpp
  ${DREAMCOMP_ROOT}/src/frontend/overlay.cpp
  ${DREAMCOMP_ROOT}/src/frontend/hud_editor.cpp
  ${DREAMCOMP_ROOT}/src/frontend/soft_render.cpp
  ${DREAMCOMP_ROOT}/src/frontend/launcher_model.cpp
  ${DREAMCOMP_ROOT}/src/frontend/sdl_image_shim.cpp
  ${_be}/RmlUi_Platform_SDL.cpp
  ${_be}/RmlUi_Renderer_SDL.cpp)
target_include_directories(dreamcomp_frontend
  PUBLIC ${DREAMCOMP_ROOT}/include
  PRIVATE ${DREAMCOMP_ROOT}/src/frontend ${DREAMCOMP_ROOT}/src/frontend/shim ${_be})
target_compile_definitions(dreamcomp_frontend PRIVATE RMLUI_SDL_VERSION_MAJOR=3)
target_compile_features(dreamcomp_frontend PUBLIC cxx_std_20)
target_link_libraries(dreamcomp_frontend
  PUBLIC dream::runtime dream::translator dream::render dream::render_vk
  PRIVATE RmlUi::RmlUi SDL3::SDL3)
# The backends are third-party code: keep the build log about our own.
set_source_files_properties(${_be}/RmlUi_Platform_SDL.cpp ${_be}/RmlUi_Renderer_SDL.cpp
  PROPERTIES COMPILE_OPTIONS "${DREAMCOMP_NO_WARNINGS}")

# Logic checks without a window (not part of `all`):
#   cmake --build <build> --target dreamcomp_launcher_tests
#   <build>/dreamcomp/dreamcomp_launcher_tests <scratch dir> [<disc image> <sha1>]
add_executable(dreamcomp_launcher_tests EXCLUDE_FROM_ALL
  ${DREAMCOMP_ROOT}/src/frontend/tests/test_launcher_model.cpp
  ${DREAMCOMP_ROOT}/src/frontend/launcher_model.cpp
  ${DREAMCOMP_ROOT}/src/settings.cpp)
target_include_directories(dreamcomp_launcher_tests PRIVATE
  ${DREAMCOMP_ROOT}/include ${DREAMCOMP_ROOT}/src/frontend)
target_link_libraries(dreamcomp_launcher_tests PRIVATE dream::runtime)
target_compile_features(dreamcomp_launcher_tests PRIVATE cxx_std_20)
