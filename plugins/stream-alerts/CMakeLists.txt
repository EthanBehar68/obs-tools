plugin_project()

# Alert logic with no libobs or Qt dependency, shared by the plugin and the unit tests
add_library(stream-alerts-core STATIC)
target_sources(
  stream-alerts-core
  PRIVATE
    src/core/alert-queue.cpp
    src/core/alert-queue.hpp
    src/core/alerts-config.cpp
    src/core/alerts-config.hpp
    src/core/platform-events.cpp
    src/core/platform-events.hpp
)
target_include_directories(stream-alerts-core PUBLIC src)
target_link_libraries(stream-alerts-core PUBLIC obs-tools-core)
set_target_properties(stream-alerts-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Widgets Core Gui)
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets stream-alerts-core obs-tools-ui
)

target_sources(
  ${PROJECT_NAME}
  PRIVATE
    src/alerts-controller.cpp
    src/alerts-controller.hpp
    src/alerts-dialog.cpp
    src/alerts-dialog.hpp
    src/net/listeners.cpp
    src/net/listeners.hpp
    src/plugin-main.cpp
)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

_remove_output_before_link(${PROJECT_NAME})

plugin_merge_data()

set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

if(ENABLE_TESTS)
  add_executable(stream-alerts-tests)
  target_sources(stream-alerts-tests PRIVATE tests/test-alert-queue.cpp tests/test-platform-events.cpp)
  target_link_libraries(stream-alerts-tests PRIVATE stream-alerts-core obs-tools-test-main)
  _remove_output_before_link(stream-alerts-tests)
  add_test(NAME stream-alerts-tests COMMAND stream-alerts-tests)
endif()
