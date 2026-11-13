####
# USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
# for interfacing one or two MOS SID chips and/or hardware SID emulators over
# (WEB)USB with your computer, phone or ASID supporting player
#
# usbsid-driver.cmake
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

### Locate the USBSID-Pico-driver sources for the example tools.
###
### Sets:
###   US_DRIVER_DIR      directory holding USBSID.cpp
###   US_DRIVER_SOURCES  driver sources to compile into the tool
###
### Looked for in this order, the first holding USBSID.cpp wins:
###   -DUS_DRIVER_DIR=<dir>                  explicit, relative to the tool dir
###   examples/../lib/usbsid-driver[/src]    where CI checks the driver out
###   ../driver-repo/src                     driver working copy beside repo/
###
### A driver checkout keeps its sources in src/, a symlink may point straight
### at src/: both layouts are accepted.

set(US_DRIVER_DIR "" CACHE STRING "Directory holding USBSID.cpp, empty to search")

set(US_DRIVER_CANDIDATES "")
if(NOT US_DRIVER_DIR STREQUAL "")
  get_filename_component(US_DRIVER_GIVEN
    "${US_DRIVER_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  list(APPEND US_DRIVER_CANDIDATES "${US_DRIVER_GIVEN}" "${US_DRIVER_GIVEN}/src")
else()
  list(APPEND US_DRIVER_CANDIDATES
    "${CMAKE_CURRENT_LIST_DIR}/../lib/usbsid-driver/src"
    "${CMAKE_CURRENT_LIST_DIR}/../lib/usbsid-driver"
    "${CMAKE_CURRENT_LIST_DIR}/../../driver-repo/src")
endif()

set(US_DRIVER_FOUND "")
foreach(US_DRIVER_TRY ${US_DRIVER_CANDIDATES})
  if(EXISTS "${US_DRIVER_TRY}/USBSID.cpp" AND EXISTS "${US_DRIVER_TRY}/USBSIDInterface.cpp")
    get_filename_component(US_DRIVER_FOUND "${US_DRIVER_TRY}" ABSOLUTE)
    break()
  endif()
endforeach()

if(US_DRIVER_FOUND STREQUAL "")
  string(REPLACE ";" "\n    " US_DRIVER_LIST "${US_DRIVER_CANDIDATES}")
  message(FATAL_ERROR
    "No USBSID-Pico-driver found. Looked for USBSID.cpp in:\n"
    "    ${US_DRIVER_LIST}\n"
    "  Pass -DUS_DRIVER_DIR=<driver checkout>/src, or check the driver out into\n"
    "  repo/lib/usbsid-driver (https://github.com/LouDnl/USBSID-Pico-driver).")
endif()

set(US_DRIVER_DIR "${US_DRIVER_FOUND}" CACHE STRING "Directory holding USBSID.cpp, empty to search" FORCE)
set(US_DRIVER_SOURCES
  ${US_DRIVER_DIR}/USBSID.cpp
  ${US_DRIVER_DIR}/USBSIDInterface.cpp
)
message(STATUS "USBSID-Pico-driver sources: ${US_DRIVER_DIR}")
