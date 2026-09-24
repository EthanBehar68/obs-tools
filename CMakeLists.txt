cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTS "Build the unit tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# Platform logic with no libobs or Qt dependency, shared by the plugin and the unit tests
add_library(unified-chat-core STATIC)
target_sources(
  unified-chat-core
  PRIVATE
    src/core/chat-config.cpp
    src/core/chat-config.hpp
    src/core/chat-format.cpp
    src/core/chat-format.hpp
    src/core/chat-message.hpp
    src/core/chat-router.cpp
    src/core/chat-router.hpp
    src/core/echo-merger.cpp
    src/core/echo-merger.hpp
    src/core/http-client.hpp
    src/core/oauth-device.cpp
    src/core/oauth-device.hpp
    src/core/text-util.cpp
    src/core/text-util.hpp
    src/core/twitch-irc.cpp
    src/core/twitch-irc.hpp
    src/core/youtube-api.cpp
    src/core/youtube-api.hpp
)
target_include_directories(unified-chat-core PUBLIC src dep/nlohmann-json)
set_target_properties(unified-chat-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs unified-chat-core)

find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Gui)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/chat-dock.cpp
    src/chat-dock.hpp
    src/net/curl-http-client.cpp
    src/net/curl-http-client.hpp
    src/net/device-login.cpp
    src/net/device-login.hpp
    src/net/twitch-connection.cpp
    src/net/twitch-connection.hpp
    src/net/youtube-connection.cpp
    src/net/youtube-connection.hpp
    src/platform-icons.cpp
    src/platform-icons.hpp
    src/plugin-main.cpp
    src/settings-dialog.cpp
    src/settings-dialog.hpp
    src/target-switch.cpp
    src/target-switch.hpp
)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
endif()

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

_remove_output_before_link(${CMAKE_PROJECT_NAME})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(ENABLE_TESTS)
  enable_testing()
  add_executable(unified-chat-tests)
  target_sources(
    unified-chat-tests
    PRIVATE
      tests/test-echo-merger.cpp
      tests/test-main.cpp
      tests/test-oauth.cpp
      tests/test-router-format-config.cpp
      tests/test-text-util.cpp
      tests/test-twitch-irc.cpp
      tests/test-youtube.cpp
  )
  target_include_directories(unified-chat-tests PRIVATE dep/doctest)
  target_link_libraries(unified-chat-tests PRIVATE unified-chat-core)
  _remove_output_before_link(unified-chat-tests)
  add_test(NAME unified-chat-tests COMMAND unified-chat-tests)
endif()