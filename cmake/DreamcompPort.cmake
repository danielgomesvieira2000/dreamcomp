# dreamcomp_add_port(<id> TITLE <title> [OUTPUT_NAME <exe name>] [SOURCES <port sources>...]
#                    [LAUNCHER_DIR <dir>])
#
# One call per port, in the port repository's game/CMakeLists.txt, next to <id>.toml. It wraps the
# engine's dream_add_game() (translate the boot executable, build <id>_boot) and adds the port's
# own sources (hooks, enhancements) to that executable.
#
# With the launcher built (cmake/DreamcompFrontend.cmake), dreamcomp's frontend/ and the fonts are
# copied next to the executable; LAUNCHER_DIR's files (art.png, port.rcss) are copied over them
# (docs/FRONTEND.md).
#
# Like dream_add_game, it is a no-op until the owner has extracted their disc with
# `python <dreamcomp>/tools/dc.py setup <port> --disc <image>`: nothing of the game is in the
# repository, so there is nothing to build without it.
function(dreamcomp_add_port id)
  cmake_parse_arguments(ARG "" "TITLE;OUTPUT_NAME;LAUNCHER_DIR" "SOURCES" ${ARGN})
  if(NOT ARG_TITLE)
    set(ARG_TITLE ${id})
  endif()
  dream_add_game(${id} TITLE "${ARG_TITLE}")
  if(NOT TARGET ${id}_boot)
    message(STATUS "dreamcomp: ${ARG_TITLE}: no extracted boot executable yet; run "
                   "`python ${DREAMCOMP_ROOT}/tools/dc.py setup <port dir> --disc <image>`")
    return()
  endif()
  target_link_libraries(${id}_boot PRIVATE dreamcomp_core)
  if(ARG_SOURCES)
    target_sources(${id}_boot PRIVATE ${ARG_SOURCES})
  endif()
  if(ARG_OUTPUT_NAME)
    set_target_properties(${id}_boot PROPERTIES OUTPUT_NAME ${ARG_OUTPUT_NAME})
  endif()
  target_compile_definitions(${id}_boot PRIVATE DREAMCOMP_PORT_ID="${id}"
                                                DREAMCOMP_PORT_TITLE="${ARG_TITLE}")
  # Windows has no rpath: put the runtime DLLs of everything linked (SDL3) next to the executable.
  # SDL3::SDL3 is an imported target scoped to the engine's directory, so name no target here.
  if(WIN32)
    add_custom_command(TARGET ${id}_boot POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${id}_boot>
              $<TARGET_FILE_DIR:${id}_boot>
      COMMAND_EXPAND_LISTS VERBATIM)
  endif()
  if(TARGET dreamcomp_frontend)
    target_link_libraries(${id}_boot PRIVATE dreamcomp_frontend)
    set(_fe $<TARGET_FILE_DIR:${id}_boot>/frontend)
    set(_port_fe "")
    if(ARG_LAUNCHER_DIR)
      set(_port_fe COMMAND ${CMAKE_COMMAND} -E copy_directory ${ARG_LAUNCHER_DIR} ${_fe})
    endif()
    # A target of its own (always run, cheap) rather than POST_BUILD, so an edited .rml/.rcss
    # reaches the build tree without relinking the executable.
    add_custom_target(${id}_launcher_assets ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${DREAMCOMP_ROOT}/frontend ${_fe}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_fe}/fonts
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter-Regular.ttf
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter-SemiBold.ttf
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter-Bold.ttf
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/Inter-LICENSE.txt
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/promptfont.ttf
              ${DREAMCOMP_FRONTEND_FONTS_DIR}/PromptFont-LICENSE.txt
              ${_fe}/fonts
      ${_port_fe}
      VERBATIM)
    add_dependencies(${id}_boot ${id}_launcher_assets)
  endif()
  message(STATUS "dreamcomp: port ${ARG_TITLE} (${id}) configured")
endfunction()
