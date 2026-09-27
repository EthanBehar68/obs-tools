cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTS "Build the unit tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# link.exe cannot overwrite its previous output on FAT32/exFAT drives (LNK1105, error 1224), so remove it first
function(_remove_output_before_link target)
  if(WIN32)
    add_custom_command(
      TARGET ${target}
      PRE_LINK
      COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE:${target}>"
      VERBATIM
    )
  endif()
endfunction()

# Each plugin's name and version live in its plugin.json
macro(plugin_project)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" _plugin_spec)
  string(JSON _plugin_name GET ${_plugin_spec} name)
  string(JSON _plugin_version GET ${_plugin_spec} version)
  project(${_plugin_name} VERSION ${_plugin_version})
  set(PLUGIN_VERSION ${PROJECT_VERSION})
  set(PLUGIN_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
endmacro()

# PLUGIN_DATA_DIR: the plugin's data/ with the shared UI's strings (libs/common/data/locale) appended to its
# locale files, since obs_module_text reads only the plugin's own locale file.
function(plugin_merge_data)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/data")
  set(common "${CMAKE_SOURCE_DIR}/libs/common/data/locale")
  set(merged "${CMAKE_CURRENT_BINARY_DIR}/data")
  file(REMOVE_RECURSE "${merged}")
  file(COPY "${source}/" DESTINATION "${merged}")
  file(GLOB_RECURSE own_files "${source}/*")
  file(GLOB common_locales RELATIVE "${common}" "${common}/*.ini")
  foreach(locale IN LISTS common_locales)
    file(READ "${common}/${locale}" strings)
    file(APPEND "${merged}/locale/${locale}" "${strings}")
    list(APPEND own_files "${common}/${locale}")
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${own_files})
  set(PLUGIN_DATA_DIR "${merged}" PARENT_SCOPE)
endfunction()

if(ENABLE_TESTS)
  enable_testing()
endif()

add_subdirectory(libs/common)
add_subdirectory(plugins/unified-chat)
add_subdirectory(plugins/stream-alerts)
