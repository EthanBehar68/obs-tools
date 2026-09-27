plugin_project()

# Platform logic with no libobs or Qt dependency, shared by the plugin and the unit tests
add_library(unified-chat-core STATIC)
target_sources(
  unified-chat-core
  PRIVATE
    src/core/bot-merger.cpp
    src/core/bot-merger.hpp
    src/core/chat-config.cpp
    src/core/chat-config.hpp
    src/core/chat-format.cpp
    src/core/chat-format.hpp
    src/core/chat-message.hpp
    src/core/chat-router.cpp
    src/core/chat-router.hpp
    src/core/echo-merger.cpp
    src/core/echo-merger.hpp
    src/core/emotes.cpp
    src/core/emotes.hpp
    src/core/mentions.cpp
    src/core/mentions.hpp
    src/core/moderation.cpp
    src/core/moderation.hpp
    src/core/name-color.cpp
    src/core/name-color.hpp
    src/core/twitch-irc.cpp
    src/core/twitch-irc.hpp
    src/core/youtube-api.cpp
    src/core/youtube-api.hpp
)
target_include_directories(unified-chat-core PUBLIC src)
target_link_libraries(unified-chat-core PUBLIC obs-tools-core)
set_target_properties(unified-chat-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs unified-chat-core obs-tools-net)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Gui)
  target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${PROJECT_NAME}
  PRIVATE
    src/chat-dock.cpp
    src/chat-dock.hpp
    src/net/asset-loader.cpp
    src/net/asset-loader.hpp
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
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

_remove_output_before_link(${PROJECT_NAME})

set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

if(ENABLE_TESTS)
  add_executable(unified-chat-tests)
  target_sources(
    unified-chat-tests
    PRIVATE
      tests/test-bot-merger.cpp
      tests/test-echo-merger.cpp
      tests/test-emotes.cpp
      tests/test-mentions.cpp
      tests/test-moderation.cpp
      tests/test-name-color.cpp
      tests/test-router-format-config.cpp
      tests/test-twitch-irc.cpp
      tests/test-youtube.cpp
  )
  target_link_libraries(unified-chat-tests PRIVATE unified-chat-core obs-tools-test-main)
  _remove_output_before_link(unified-chat-tests)
  add_test(NAME unified-chat-tests COMMAND unified-chat-tests)
endif()
