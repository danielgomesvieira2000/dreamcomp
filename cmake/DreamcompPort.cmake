# dreamcomp_add_port(<id> TITLE <title> [OUTPUT_NAME <exe name>] [SOURCES <port sources>...])
#
# One call per port, in the port repository's game/CMakeLists.txt, next to <id>.toml. It wraps the
# engine's dream_add_game() (translate the boot executable, build <id>_boot) and adds the port's
# own sources (hooks, enhancements) to that executable.
#
# Like dream_add_game, it is a no-op until the owner has extracted their disc with
# `python <dreamcomp>/tools/dc.py setup <port> --disc <image>`: nothing of the game is in the
# repository, so there is nothing to build without it.
function(dreamcomp_add_port id)
  cmake_parse_arguments(ARG "" "TITLE;OUTPUT_NAME" "SOURCES" ${ARGN})
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
  message(STATUS "dreamcomp: port ${ARG_TITLE} (${id}) configured")
endfunction()
