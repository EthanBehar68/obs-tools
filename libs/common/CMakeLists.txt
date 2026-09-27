# Shared by every plugin. obs-tools-core has no libobs, Qt or curl dependency and is unit tested.
add_library(obs-tools-core STATIC)
target_sources(
  obs-tools-core
  PRIVATE
    src/core/accounts.cpp
    src/core/accounts.hpp
    src/core/http-client.hpp
    src/core/json-array-reader.cpp
    src/core/json-array-reader.hpp
    src/core/oauth-device.cpp
    src/core/oauth-device.hpp
    src/core/secret-codec.cpp
    src/core/secret-codec.hpp
    src/core/text-util.cpp
    src/core/text-util.hpp
)
target_include_directories(obs-tools-core PUBLIC src "${CMAKE_SOURCE_DIR}/dep/nlohmann-json")
set_target_properties(obs-tools-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# HTTP over libcurl, the Windows secret store, the shared accounts file and the sign-in flow. Uses the
# including plugin's obs_log and obs_module_config_path.
add_library(obs-tools-net STATIC)
target_sources(
  obs-tools-net
  PRIVATE
    src/net/account-store.cpp
    src/net/account-store.hpp
    src/net/curl-http-client.cpp
    src/net/curl-http-client.hpp
    src/net/device-login.cpp
    src/net/device-login.hpp
    src/secret-store.cpp
    src/secret-store.hpp
)
find_package(libobs REQUIRED)
find_package(CURL REQUIRED)
target_include_directories(obs-tools-net PRIVATE "${CMAKE_SOURCE_DIR}/libs/obs-support")
target_link_libraries(obs-tools-net PUBLIC obs-tools-core CURL::libcurl OBS::libobs)
if(WIN32)
  target_link_libraries(obs-tools-net PUBLIC crypt32)
endif()

# Tools > OBS Tools and the shared Accounts window. Its strings are in data/locale, merged into each plugin's.
if(ENABLE_FRONTEND_API AND ENABLE_QT)
  add_library(obs-tools-ui STATIC)
  target_sources(
    obs-tools-ui
    PRIVATE src/ui/accounts-dialog.cpp src/ui/accounts-dialog.hpp src/ui/obs-tools-menu.cpp src/ui/obs-tools-menu.hpp
  )
  find_package(obs-frontend-api REQUIRED)
  find_package(Qt6 COMPONENTS Widgets Core Gui)
  target_link_libraries(obs-tools-ui PUBLIC obs-tools-net OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets)
endif()

if(ENABLE_TESTS)
  add_library(obs-tools-test-main STATIC tests/test-main.cpp)
  target_include_directories(obs-tools-test-main PUBLIC "${CMAKE_SOURCE_DIR}/dep/doctest")

  add_executable(common-tests)
  target_sources(
    common-tests
    PRIVATE tests/test-accounts.cpp tests/test-json-array-reader.cpp tests/test-oauth.cpp tests/test-text-util.cpp
  )
  target_link_libraries(common-tests PRIVATE obs-tools-core obs-tools-test-main)
  _remove_output_before_link(common-tests)
  add_test(NAME common-tests COMMAND common-tests)
endif()
