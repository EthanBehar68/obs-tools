# CMake common helper functions module

include_guard(GLOBAL)

# check_uuid: Helper function to check for valid UUID
function(check_uuid uuid_string return_value)
  set(valid_uuid TRUE)
  # gersemi: off
  set(uuid_token_lengths 8 4 4 4 12)
  # gersemi: on
  set(token_num 0)

  string(REPLACE "-" ";" uuid_tokens ${uuid_string})
  list(LENGTH uuid_tokens uuid_num_tokens)

  if(uuid_num_tokens EQUAL 5)
    message(DEBUG "UUID ${uuid_string} is valid with 5 tokens.")
    foreach(uuid_token IN LISTS uuid_tokens)
      list(GET uuid_token_lengths ${token_num} uuid_target_length)
      string(LENGTH "${uuid_token}" uuid_actual_length)
      if(uuid_actual_length EQUAL uuid_target_length)
        string(REGEX MATCH "[0-9a-fA-F]+" uuid_hex_match ${uuid_token})
        if(NOT uuid_hex_match STREQUAL uuid_token)
          set(valid_uuid FALSE)
          break()
        endif()
      else()
        set(valid_uuid FALSE)
        break()
      endif()
      math(EXPR token_num "${token_num}+1")
    endforeach()
  else()
    set(valid_uuid FALSE)
  endif()
  message(DEBUG "UUID ${uuid_string} valid: ${valid_uuid}")
  set(${return_value} ${valid_uuid} PARENT_SCOPE)
endfunction()

# target_add_plugin_support: Give a plugin its own obs_log/PLUGIN_NAME/PLUGIN_VERSION from the current project()
function(target_add_plugin_support target)
  set(_support_dir "${CMAKE_SOURCE_DIR}/libs/obs-support")
  configure_file("${_support_dir}/plugin-support.c.in" "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c" @ONLY)
  add_library(${target}-support STATIC)
  target_sources(
    ${target}-support
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c"
    PUBLIC "${_support_dir}/plugin-support.h"
  )
  target_include_directories(${target}-support PUBLIC "${_support_dir}")
  if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
    # add fPIC on Linux to prevent shared object errors
    set_property(TARGET ${target}-support PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
  target_link_libraries(${target} PRIVATE ${target}-support)
endfunction()
