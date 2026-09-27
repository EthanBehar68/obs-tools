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

if(ENABLE_TESTS)
  enable_testing()
endif()

add_subdirectory(libs/common)
add_subdirectory(plugins/unified-chat)
