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

### Project magic sprinkles
set(PROJECT_NAME cfg_usbsid)
set(EXECUTABLE ${PROJECT_NAME} CACHE STRING "EXECUTABLE")

### Project type
project(${PROJECT_NAME} C ASM)

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

### Source is horse ofcourse ofcourse
set(SOURCEFILES
  cfg_usbsid.c
  inih/ini.c
)

### Header directories to include
set(TARGET_INCLUDE_DIRS PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/inih
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
