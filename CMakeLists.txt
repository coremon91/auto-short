cmake_minimum_required(VERSION 3.16)

# /usr/bin/c++ on this image is Clang and does not link libstdc++.
# Use g++ unless the caller already chose a compiler.
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(_XDCAM_GXX NAMES g++ g++-13)
    if(_XDCAM_GXX)
        set(CMAKE_CXX_COMPILER "${_XDCAM_GXX}" CACHE FILEPATH "C++ compiler")
    endif()
endif()

project(xdcam_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DECKLINK_SDK_DIR "" CACHE PATH "Directory that contains DeckLinkAPI.h and DeckLinkAPIDispatch.cpp")

find_path(DECKLINK_INCLUDE_DIR DeckLinkAPI.h
    HINTS ${DECKLINK_SDK_DIR} $ENV{DECKLINK_SDK_DIR}
    PATHS /usr/include /usr/local/include /opt/blackmagic /opt/BlackmagicDeckLinkSDK
    PATH_SUFFIXES include Linux/include)

set(XDCAM_HAVE_DECKLINK 0)
set(XDCAM_DECKLINK_NOTE "DeckLink SDK headers were not present at configure time, so the DeckLink backend was left out of this build")
if(DECKLINK_INCLUDE_DIR AND EXISTS "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
    set(XDCAM_HAVE_DECKLINK 1)
    set(XDCAM_DECKLINK_NOTE "DeckLink SDK headers were present at configure time and the DeckLink backend is included")
elseif(DECKLINK_INCLUDE_DIR)
    set(XDCAM_DECKLINK_NOTE "DeckLinkAPI.h was found but DeckLinkAPIDispatch.cpp was not beside it, so the DeckLink backend was left out of this build")
endif()

message(STATUS "XDCAM: ${XDCAM_DECKLINK_NOTE}")

configure_file(
    "${CMAKE_SOURCE_DIR}/src/build_config.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/build_config.hpp"
    @ONLY)

set(XDCAM_FIXTURE_CPP "${CMAKE_BINARY_DIR}/generated/fixture_data.cpp")
add_custom_command(
    OUTPUT "${XDCAM_FIXTURE_CPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND python3 "${CMAKE_SOURCE_DIR}/cmake/embed_fixtures.py"
            "${XDCAM_FIXTURE_CPP}"
            kFixtureOneInput "${CMAKE_SOURCE_DIR}/fixtures/one_input.json"
            kFixtureTwoInput "${CMAKE_SOURCE_DIR}/fixtures/two_input.json"
            kFixtureFourInput "${CMAKE_SOURCE_DIR}/fixtures/four_input.json"
            kFixtureBusy "${CMAKE_SOURCE_DIR}/fixtures/busy.json"
            kFixtureNoAudio8ch "${CMAKE_SOURCE_DIR}/fixtures/no_audio_8ch.json"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/cmake/embed_fixtures.py"
        "${CMAKE_SOURCE_DIR}/fixtures/one_input.json"
        "${CMAKE_SOURCE_DIR}/fixtures/two_input.json"
        "${CMAKE_SOURCE_DIR}/fixtures/four_input.json"
        "${CMAKE_SOURCE_DIR}/fixtures/busy.json"
        "${CMAKE_SOURCE_DIR}/fixtures/no_audio_8ch.json"
    VERBATIM)

add_library(xdcam_core STATIC
    src/json.cpp
    src/config.cpp
    src/mock_backend.cpp
    src/preflight.cpp
    src/report.cpp
    src/runtime_info.cpp
    src/backend.cpp
    "${XDCAM_FIXTURE_CPP}")
target_include_directories(xdcam_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_options(xdcam_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(XDCAM_HAVE_DECKLINK)
    target_sources(xdcam_core PRIVATE
        src/decklink_backend.cpp
        "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
    target_include_directories(xdcam_core PRIVATE "${DECKLINK_INCLUDE_DIR}")
    target_link_libraries(xdcam_core PUBLIC ${CMAKE_DL_LIBS})
    set_source_files_properties("${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp"
        PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(src/decklink_backend.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-error")
endif()

add_executable(xdcam-scan src/main.cpp)
target_link_libraries(xdcam-scan PRIVATE xdcam_core)
target_compile_options(xdcam-scan PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(phase0_tests tests/phase0_tests.cpp)
target_link_libraries(phase0_tests PRIVATE xdcam_core)
target_compile_options(phase0_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(phase0_tests PRIVATE XDCAM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

enable_testing()
add_test(NAME phase0_tests COMMAND phase0_tests)
add_test(NAME cli_scan COMMAND bash "${CMAKE_SOURCE_DIR}/tests/cli_scan.sh" $<TARGET_FILE:xdcam-scan>)
add_test(NAME cli_one COMMAND bash "${CMAKE_SOURCE_DIR}/tests/cli_one.sh" $<TARGET_FILE:xdcam-scan> "${CMAKE_SOURCE_DIR}/config/one_channel.json")
add_test(NAME cli_four COMMAND bash "${CMAKE_SOURCE_DIR}/tests/cli_four.sh" $<TARGET_FILE:xdcam-scan> "${CMAKE_SOURCE_DIR}/config/four_channel.json")
add_test(NAME cli_reject COMMAND bash "${CMAKE_SOURCE_DIR}/tests/cli_reject.sh" $<TARGET_FILE:xdcam-scan>)
