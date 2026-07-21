# OpenShare
# Copyright (C) 2026 Ace Jones / ATech
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.20)

# This branch is Linux-only. Use the Mac branch for macOS builds.
if(APPLE)
  message(FATAL_ERROR "OpenShare linux branch cannot be built on macOS. Checkout the Mac branch.")
endif()
if(NOT UNIX)
  message(FATAL_ERROR "OpenShare linux branch requires a Unix/Linux host.")
endif()

project(OpenShare LANGUAGES CXX)

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

FetchContent_Declare(
  screen_capture_lite
  GIT_REPOSITORY https://github.com/smasherprog/screen_capture_lite.git
  GIT_TAG 0c5beb0e3c5e4f9e0fcc9203025a80ad5164e6a4)

FetchContent_MakeAvailable(SDL3 screen_capture_lite)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
                  libavcodec libavutil libswscale)
find_package(OpenSSL REQUIRED)
find_package(X11 REQUIRED)

if(NOT X11_Xtst_FOUND)
  message(FATAL_ERROR "libXtst is required (XTest input injection).")
endif()

set(OPENSHARE_X11_LIBS
    ${X11_LIBRARIES}
    ${X11_Xtst_LIB}
    ${X11_Xext_LIB}
    ${X11_Xfixes_LIB})
if(X11_Xinerama_FOUND)
  list(APPEND OPENSHARE_X11_LIBS ${X11_Xinerama_LIB})
endif()

set(OPENSHARE_COMMON_SOURCES
    src/common/env.cpp
    src/common/hotp.cpp
    src/common/pairing.cpp
    src/common/net.cpp
    src/video/h264_encoder.cpp
    src/video/h264_decoder.cpp
    src/input/x11_inject.cpp)

set(OPENSHARE_CLIENT_SOURCES
    src/client/session.cpp
    src/common/hotp.cpp
    src/common/net.cpp
    src/video/h264_decoder.cpp)

set(OPENSHARE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/video
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${screen_capture_lite_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR})

add_library(OpenShareClient STATIC ${OPENSHARE_CLIENT_SOURCES})
target_include_directories(
  OpenShareClient
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/src/common
          ${CMAKE_CURRENT_SOURCE_DIR}/src/video)
target_link_libraries(OpenShareClient PUBLIC PkgConfig::FFMPEG OpenSSL::Crypto)
set_target_properties(OpenShareClient PROPERTIES OUTPUT_NAME openshare_client)
add_library(OpenShare::Client ALIAS OpenShareClient)

add_executable(OpenShare src/viewer/main.cpp ${OPENSHARE_COMMON_SOURCES})
target_include_directories(OpenShare PRIVATE ${OPENSHARE_INCLUDE_DIRS})
target_link_libraries(
  OpenShare PRIVATE SDL3::SDL3-static PkgConfig::FFMPEG OpenSSL::Crypto ${OPENSHARE_X11_LIBS})

add_executable(OpenShareCompanion src/companion/main.cpp ${OPENSHARE_COMMON_SOURCES})
target_include_directories(OpenShareCompanion PRIVATE ${OPENSHARE_INCLUDE_DIRS})
target_link_libraries(
  OpenShareCompanion
  PRIVATE screen_capture_lite_static PkgConfig::FFMPEG OpenSSL::Crypto ${OPENSHARE_X11_LIBS})

add_executable(openshare_client_example examples/client_example.cpp)
target_link_libraries(openshare_client_example PRIVATE OpenShareClient)
