# Shared by every plugin. obs-tools-core has no libobs, Qt or curl dependency and is unit tested.
add_library(obs-tools-core STATIC)
target_sources(
  obs-tools-core
  PRIVATE
    src/core/http-client.hpp
    src/core/json-array-reader.cpp
    src/core/json-array-reader.hpp
    src/core/oauth-device.cpp
    src/core/oauth-device.hpp
    src/core/secret-codec.hpp
    src/core/text-util.cpp
    src/core/text-util.hpp
)
target_include_directories(obs-tools-core PUBLIC src "${CMAKE_SOURCE_DIR}/dep/nlohmann-json")
set_target_properties(obs-tools-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The HTTP client over libcurl and the Windows secret store
add_library(obs-tools-net STATIC)
target_sources(
  obs-tools-net
  PRIVATE src/net/curl-http-client.cpp src/net/curl-http-client.hpp src/secret-store.cpp src/secret-store.hpp
)
find_package(CURL REQUIRED)
target_link_libraries(obs-tools-net PUBLIC obs-tools-core CURL::libcurl)
if(WIN32)
  target_link_libraries(obs-tools-net PUBLIC crypt32)
endif()

if(ENABLE_TESTS)
  add_library(obs-tools-test-main STATIC tests/test-main.cpp)
  target_include_directories(obs-tools-test-main PUBLIC "${CMAKE_SOURCE_DIR}/dep/doctest")

  add_executable(common-tests)
  target_sources(common-tests PRIVATE tests/test-json-array-reader.cpp tests/test-oauth.cpp tests/test-text-util.cpp)
  target_link_libraries(common-tests PRIVATE obs-tools-core obs-tools-test-main)
  _remove_output_before_link(common-tests)
  add_test(NAME common-tests COMMAND common-tests)
endif()
