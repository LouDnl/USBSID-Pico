####
# USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
# MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
# phone or ASID supporting player
#
# CMakeLists.txt
# This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
# File author: LouD
#
# Copyright (c) 2024 LouD
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
cmake_minimum_required(VERSION 3.17)

set(PROJECT_NAME usbsidpico)  # projectname
set(PROJECT_VERSION "0.0.1-ALPHA")
set(CMAKE_BUILD_TYPE Debug)  # build with debug symbols
set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

### Executables
list(APPEND FILENAMES
  ${PROJECT_NAME}-singlesid
  ${PROJECT_NAME}-singleskpico
  ${PROJECT_NAME}-dualsid
  ${PROJECT_NAME}-skpico-singlesid
  ${PROJECT_NAME}-dualskpico
  ${PROJECT_NAME}-dualskpico-mixed)
list(APPEND SIDTYPES 0 1 2 3 4 5)

# default environment options
set(SOURCEFILES
  ${CMAKE_CURRENT_LIST_DIR}/src/usbsid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/gpio.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi.c
  ${CMAKE_CURRENT_LIST_DIR}/src/asid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/mcu.c
  ${CMAKE_CURRENT_LIST_DIR}/src/usb_descriptors.c)
# COMPILE_OPTS -Wall -Werror -Wno-maybe-uninitialized -save-temps -fverbose-asm)
set(COMPILE_OPTS PRIVATE -Wno-maybe-uninitialized -fverbose-asm)
set(TARGET_LL
  pico_stdlib
  pico_unique_id
  pico_multicore
  pico_stdio_usb
  pico_usb_reset_interface
  tinyusb_device
  tinyusb_board
  hardware_clocks
  hardware_resets
  hardware_uart
  hardware_pwm
  hardware_pio)
set(PIO ${CMAKE_CURRENT_LIST_DIR}/src/clock.pio)
set(PIO_RGB ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
set(TARGET_INCLUDE_DIRS PRIVATE
  .
  src
  $ENV{PICO_SDK_PATH}/lib/tinyusb/hw
  $ENV{PICO_SDK_PATH}/lib/tinyusb/src)

# rp2040 sdk
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

# project
project(${PROJECT_NAME} C CXX ASM)

# init sdk
pico_sdk_init()

include_directories(".")

foreach(FILENAME SIDTYPE IN ZIP_LISTS FILENAMES SIDTYPES)
  # set filename
  set(BUILD ${FILENAME})
  message(STATUS "Building ${FILENAME} with SIDTYPE=${SIDTYPE}")
  # executable
  add_executable(${BUILD} ${SOURCEFILES})
  # pio addition
  pico_generate_pio_header(${BUILD} ${PIO})
  # source files to compile
  target_sources(${BUILD} PUBLIC ${SOURCEFILES})
  target_compile_options(${BUILD} ${COMPILE_OPTS})
  target_compile_definitions(${BUILD} PRIVATE USBSID SIDTYPES=${SIDTYPE})
  # tell the linker what libraries to link
  target_link_libraries(${BUILD} ${TARGET_LL})
  # target sid types
  target_include_directories(${BUILD} ${TARGET_INCLUDE_DIRS})
  pico_set_program_name(${BUILD} "USBSIDPico")
  pico_set_program_version(${BUILD} $PROJECT_VERSION)
  # create map/bin/hex/uf2 file in addition to ELF.
  pico_add_extra_outputs(${BUILD})
  # enable uart output, disable usb output
  pico_enable_stdio_uart(${BUILD} 1)
  pico_enable_stdio_usb(${BUILD} 0)
endforeach()

foreach(FILENAME SIDTYPE IN ZIP_LISTS FILENAMES SIDTYPES)
  # override filename for rgb compile
  set(FILENAME ${FILENAME}-rgb)
  # set filename
  set(BUILD ${FILENAME})
  message(STATUS "Building ${FILENAME} with SIDTYPE=${SIDTYPE}")
  # executable
  add_executable(${BUILD} ${SOURCEFILES})
  # pio addition
  pico_generate_pio_header(${BUILD} ${PIO})
  pico_generate_pio_header(${BUILD} ${PIO_RGB})
  # source files to compile
  target_sources(${BUILD} PUBLIC ${SOURCEFILES})
  target_compile_options(${BUILD} ${COMPILE_OPTS})
  target_compile_definitions(${BUILD} PRIVATE USBSID SIDTYPES=${SIDTYPE} USE_RGB=1)
  # tell the linker what libraries to link
  target_link_libraries(${BUILD} ${TARGET_LL})
  # target sid types
  target_include_directories(${BUILD} ${TARGET_INCLUDE_DIRS})
  pico_set_program_name(${BUILD} "USBSIDPico")
  pico_set_program_version(${BUILD} $PROJECT_VERSION)
  # create map/bin/hex/uf2 file in addition to ELF.
  pico_add_extra_outputs(${BUILD})
  # enable uart output, disable usb output
  pico_enable_stdio_uart(${BUILD} 1)
  pico_enable_stdio_usb(${BUILD} 0)
endforeach()
