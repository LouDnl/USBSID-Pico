####
# USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
# for interfacing one or two MOS SID chips and/or hardware SID emulators over
# (WEB)USB with your computer, phone or ASID supporting player
#
# CMakeLists.txt
# This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
# File author: LouD
#
# Copyright (c) 2026 LouD
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
####

### Usage
# cmake -S . -B build && cmake --build build -j$(nproc)

### Cmake minimum version
cmake_minimum_required(VERSION 3.17)

### Cmake compiler standard versions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

### CMake stuff for ZED
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

### Project magic sprinkles
set(PROJECT_NAME send_sid)
set(EXECUTABLE ${PROJECT_NAME} CACHE STRING "EXECUTABLE")

### Project type
project(${PROJECT_NAME} C CXX ASM)

### Include
include_directories(".")

### Create and enable PkgConfig
find_package(PkgConfig REQUIRED)

### Create `PkgConfig::<MODULE>` variables
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET libusb-1.0)

### Libraries to link
if (UNIX)
set(TARGET_LL
  PkgConfig::libusb
)
endif (UNIX)
if (WIN32)
set(TARGET_LL
  PkgConfig::libusb
  -lwinmm
)
endif (WIN32)

### SID Player source
set(EMUDIR "" CACHE STRING "Path to the USBSID-Player checkout")
if(NOT EMUDIR STREQUAL "")
  ## A relative EMUDIR is ambiguous, and it fails in a way that reads like a
  ## different fault. `if(EXISTS)` resolves a relative path against the
  ## working directory while `include()` resolves it against the including
  ## file's directory, so `cmake -S master -DEMUDIR=master/lib/usbsid-player`
  ## passes the existence check below and then fails the include with
  ## "could not find requested file", having looked in master/master/lib.
  ## Resolve it here and carry an absolute path from this point on.
  if(IS_ABSOLUTE "${EMUDIR}")
    set(USPLAYER_LIST_DIR "${EMUDIR}")
  else()
    set(USPLAYER_LIST_DIR "")
    foreach(BASE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/.." "${CMAKE_BINARY_DIR}")
      get_filename_component(EMUDIR_TRY "${BASE}/${EMUDIR}" ABSOLUTE)
      if(EXISTS "${EMUDIR_TRY}/src/file/songlengths.cpp")
        set(USPLAYER_LIST_DIR "${EMUDIR_TRY}")
        message(STATUS "EMUDIR '${EMUDIR}' is relative, resolved to ${USPLAYER_LIST_DIR}")
        break()
      endif()
    endforeach()
    ## Nothing matched. Keep the most likely reading so the error below names
    ## a real path instead of an empty one.
    if(USPLAYER_LIST_DIR STREQUAL "")
      get_filename_component(USPLAYER_LIST_DIR "${CMAKE_SOURCE_DIR}/${EMUDIR}" ABSOLUTE)
    endif()
  endif()
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/usbsid-player/src/file/songlengths.cpp")
  set(USPLAYER_LIST_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/usbsid-player")
else()
  get_filename_component(USPLAYER_LIST_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../player-repo" ABSOLUTE)
endif()
message(STATUS "USPLAYER_LIST_DIR is set to ${USPLAYER_LIST_DIR}")

### Source is horse ofcourse ofcourse
set(SOURCEFILES
  send_sid.c
  sl_interface.cpp
  ${USPLAYER_LIST_DIR}/src/file/songlengths.cpp
)

### Header directories to include
set(TARGET_INCLUDE_DIRS PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${USPLAYER_LIST_DIR}/src
  /usr/local/lib
  /usr/local/include
  /usr/lib
  /usr/include
)

### Compile time
add_executable(${EXECUTABLE} ${SOURCEFILES})
### Copy build output to main directory
add_custom_command(TARGET
  ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${EXECUTABLE}> ${CMAKE_CURRENT_LIST_DIR})
### Remove build output from build directory
add_custom_command(TARGET
  ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm $<TARGET_FILE:${EXECUTABLE}>)

target_include_directories(${EXECUTABLE} ${TARGET_INCLUDE_DIRS})
target_link_libraries(${EXECUTABLE} ${TARGET_LL})
target_sources(${EXECUTABLE} PUBLIC ${SOURCEFILES})
