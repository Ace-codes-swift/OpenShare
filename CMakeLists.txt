cmake_minimum_required(VERSION 3.20)

# Must be set before project(). Keeps screen_capture_lite's older CoreGraphics
# window-capture path compiling (those APIs are hard errors when targeting 15+).
if(APPLE AND "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET
      "14.0"
      CACHE STRING "Minimum macOS version" FORCE)
endif()

project(OpenShare LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_SHARED
    OFF
    CACHE BOOL "" FORCE)
set(SDL_STATIC
    ON
    CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.4)

set(BUILD_EXAMPLE
    OFF
    CACHE BOOL "" FORCE)
set(BUILD_CSHARP
    OFF
    CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS
    OFF
    CACHE BOOL "" FORCE)

# Pinned to master: the last tagged release (v11.0.0) still uses
# CGDisplayCreateImage, which is obsoleted in the macOS 15 SDK.
FetchContent_Declare(
  screen_capture_lite
  GIT_REPOSITORY https://github.com/smasherprog/screen_capture_lite.git
  GIT_TAG 0c5beb0e3c5e4f9e0fcc9203025a80ad5164e6a4)

FetchContent_MakeAvailable(SDL3 screen_capture_lite)

# Upstream bug: the static target unconditionally links Dwmapi (Windows-only)
# and omits the Apple frameworks it needs. Strip its link interface and add
# the frameworks to our executable below instead.
if(APPLE)
  set_target_properties(screen_capture_lite_static
                        PROPERTIES INTERFACE_LINK_LIBRARIES "")
endif()

set(OPENSHARE_COMMON_SOURCES
    src/common/env.cpp
    src/common/hotp.cpp
    src/common/mac_pairing.mm
    src/common/net.cpp
    src/video/vt_encoder.cpp
    src/video/vt_decoder.cpp
    src/input/mac_inject.cpp)

# Client library sources (no UI, no screen capture, no input injection).
set(OPENSHARE_CLIENT_SOURCES
    src/client/session.cpp
    src/common/hotp.cpp
    src/common/net.cpp
    src/video/vt_decoder.cpp)

set(OPENSHARE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/video
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${screen_capture_lite_SOURCE_DIR}/include)

# Embeddable viewer/client API: connect(host, port, managerCode) -> Session
add_library(OpenShareClient STATIC ${OPENSHARE_CLIENT_SOURCES})
target_include_directories(
  OpenShareClient
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/src/common
          ${CMAKE_CURRENT_SOURCE_DIR}/src/video)
set_target_properties(OpenShareClient PROPERTIES OUTPUT_NAME openshare_client)
add_library(OpenShare::Client ALIAS OpenShareClient)

# Viewer: connects, shows remote video, sends input
add_executable(OpenShare src/viewer/main.cpp ${OPENSHARE_COMMON_SOURCES})
target_include_directories(OpenShare PRIVATE ${OPENSHARE_INCLUDE_DIRS})
target_link_libraries(OpenShare PRIVATE SDL3::SDL3-static)
set_target_properties(
  OpenShare
  PROPERTIES MACOSX_BUNDLE TRUE
             MACOSX_BUNDLE_INFO_PLIST
             ${CMAKE_CURRENT_SOURCE_DIR}/packaging/OpenShare-Info.plist)

# Companion: listens, captures, encodes, injects input
add_executable(OpenShareCompanion src/companion/main.cpp ${OPENSHARE_COMMON_SOURCES})
target_include_directories(OpenShareCompanion PRIVATE ${OPENSHARE_INCLUDE_DIRS})
target_link_libraries(OpenShareCompanion PRIVATE screen_capture_lite_static)
set_target_properties(
  OpenShareCompanion
  PROPERTIES MACOSX_BUNDLE TRUE
             MACOSX_BUNDLE_INFO_PLIST
             ${CMAKE_CURRENT_SOURCE_DIR}/packaging/OpenShareCompanion-Info.plist)

# Example program that links only the client library
add_executable(openshare_client_example examples/client_example.cpp)
target_link_libraries(openshare_client_example PRIVATE OpenShareClient)

if(APPLE)
  set(OPENSHARE_APPLE_FRAMEWORKS
      "-framework CoreFoundation"
      "-framework Cocoa"
      "-framework CoreMedia"
      "-framework AVFoundation"
      "-framework CoreGraphics"
      "-framework CoreVideo"
      "-framework VideoToolbox"
      "-framework ApplicationServices"
      "-framework Carbon"
      "-framework Security")

  target_link_libraries(OpenShare PRIVATE ${OPENSHARE_APPLE_FRAMEWORKS})
  target_link_libraries(OpenShareCompanion PRIVATE ${OPENSHARE_APPLE_FRAMEWORKS})
  # Client lib needs VideoToolbox + Core* for decoding; Security for HOTP RNG paths unused but OK.
  target_link_libraries(
    OpenShareClient
    PUBLIC "-framework CoreFoundation"
           "-framework CoreMedia"
           "-framework CoreVideo"
           "-framework VideoToolbox"
           "-framework Security")
  target_link_libraries(openshare_client_example PRIVATE ${OPENSHARE_APPLE_FRAMEWORKS})
endif()
