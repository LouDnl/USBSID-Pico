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
cmake_minimum_required(VERSION 3.23)

set(PROJECT_NAME usbsidpico)  # projectname
set(PROJECT_VERSION "0.0.2-ALPHA")
set(CMAKE_BUILD_TYPE Debug)  # build with debug symbols
set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
# where is the target environment toolchain is located
set(CMAKE_FIND_ROOT_PATH /home/loud/.pico-sdk/toolchain/13_2_Rel1/)

set(FAMILY "rp2040")
set(BOARD "raspberry_pi_pico")

### Executables
list(APPEND FILENAMES
  ${PROJECT_NAME}-singlesid
  ${PROJECT_NAME}-singleskpico
  ${PROJECT_NAME}-dualsid
  ${PROJECT_NAME}-skpico-singlesid
  ${PROJECT_NAME}-dualskpico
  ${PROJECT_NAME}-dualskpico-mixed
)
list(APPEND SIDTYPE 0 1 2 3 4 5)

# default environment options
set(SOURCEFILES
  ${CMAKE_CURRENT_LIST_DIR}/src/usbsid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/gpio.c
  # ${CMAKE_CURRENT_LIST_DIR}/src/midi.c
  # ${CMAKE_CURRENT_LIST_DIR}/src/asid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/mcu.c
  ${CMAKE_CURRENT_LIST_DIR}/src/usb_descriptors.c
  )
# COMPILE_OPTS -Wall -Werror -Wno-maybe-uninitialized -save-temps -fverbose-asm)
set(COMPILE_OPTS PRIVATE -Wno-maybe-uninitialized -fverbose-asm)
set(TARGET_LL
  pico_stdlib
  pico_unique_id
  pico_multicore
  pico_stdio_usb
  pico_stdio_uart
  pico_usb_reset_interface
  tinyusb_device
  tinyusb_board
  hardware_gpio
  hardware_clocks
  hardware_resets
  hardware_uart
  hardware_irq
  hardware_dma
  hardware_timer
  # hardware_adc
  hardware_pwm
  hardware_pio
)
set(PIO_BUS ${CMAKE_CURRENT_LIST_DIR}/src/bus_control.pio)
set(PIO_CLOCK ${CMAKE_CURRENT_LIST_DIR}/src/clock.pio)
# set(PIO_RGB ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
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

# NOTICE: ENABLING THESE DEBUGGING DEFINITIONS WILL SIGNIFICANTLY DELAY PLAYING!
## Enable UART
add_compile_definitions(USBSID_UART=1)
## Enable memory map of SID 1 voices printing
add_compile_definitions(MEM_DEBUG=1)
## Enable debugging in usbsid.c
add_compile_definitions(USBSID_DEBUG=1)
# add_compile_definitions(USBIO_DEBUG=1)
# add_compile_definitions(ADDR_DEBUG=1)
# add_compile_definitions(BUFF_DEBUG=1)
## Enable debugging in midi.c
# add_compile_definitions(MIDI_DEBUG=1)
# add_compile_definitions(IOMIDI_DEBUG=1)
## Enable debugging in asid.c
# add_compile_definitions(ASID_DEBUG=1)
## Enable debugging in gpio.c
# add_compile_definitions(USBSIDGPIO_DEBUG=1)

foreach(FILENAME SIDTYPE IN ZIP_LISTS FILENAMES SIDTYPE)
  # set filename
  set(BUILD ${FILENAME})
  message(STATUS "Building ${FILENAME} with SIDTYPE=${SIDTYPE}")
  # executable
  add_executable(${BUILD} ${SOURCEFILES})
  # pio addition
  pico_generate_pio_header(${BUILD} ${PIO_BUS})
  pico_generate_pio_header(${BUILD} ${PIO_CLOCK})
  # source files to compile
  target_sources(${BUILD} PUBLIC ${SOURCEFILES})
  target_compile_options(${BUILD} ${COMPILE_OPTS})
  target_compile_definitions(${BUILD} PRIVATE USBSID SIDTYPE=${SIDTYPE})
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

# foreach(FILENAME SIDTYPE IN ZIP_LISTS FILENAMES SIDTYPES)
#   # override filename for rgb compile
#   set(FILENAME ${FILENAME}-rgb)
#   # set filename
#   set(BUILD ${FILENAME})
#   message(STATUS "Building ${FILENAME} with SIDTYPE=${SIDTYPE}")
#   # executable
#   add_executable(${BUILD} ${SOURCEFILES})
#   # pio addition
#   pico_generate_pio_header(${BUILD} ${PIO_BUS})
#   pico_generate_pio_header(${BUILD} ${PIO_CLOCK})
#   pico_generate_pio_header(${BUILD} ${PIO_RGB})
#   # source files to compile
#   target_sources(${BUILD} PUBLIC ${SOURCEFILES})
#   target_compile_options(${BUILD} ${COMPILE_OPTS})
#   target_compile_definitions(${BUILD} PRIVATE USBSID SIDTYPES=${SIDTYPE} USE_RGB=1)
#   # tell the linker what libraries to link
#   target_link_libraries(${BUILD} ${TARGET_LL})
#   # target sid types
#   target_include_directories(${BUILD} ${TARGET_INCLUDE_DIRS})
#   pico_set_program_name(${BUILD} "USBSIDPico")
#   pico_set_program_version(${BUILD} $PROJECT_VERSION)
#   # create map/bin/hex/uf2 file in addition to ELF.
#   pico_add_extra_outputs(${BUILD})
#   # enable uart output, disable usb output
#   pico_enable_stdio_uart(${BUILD} 1)
#   pico_enable_stdio_usb(${BUILD} 0)
# endforeach()
