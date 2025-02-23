####
# USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
# MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
# phone or ASID supporting player
#
# CMakeLists.txt
# This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
# File author: LouD
#
# Copyright (c) 2024-2025 LouD
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

### Cmake minimum version
cmake_minimum_required(VERSION 3.17)

### Project magic sprinkles
set(PROJECT_NAME usbsidpico)
set(PROJECT_MANUFACTURER "LouD")
set(PRODUCT_STRING "USBSID-Pico")
set(MAGIC_SMOKE "20250212")
set(PROJECT_VERSION "0.3.1-BETA.${MAGIC_SMOKE}")  # Must be the same as in config.h

### Want a cookie?
# NOTICE: ENABLING THESE DEBUGGING DEFINITIONS WILL HAVE SIGNIFICANT IMPACT AND WILL DELAY PLAYING!
# NOTICE: UART AND USBCDC ARE MUTUALLY EXCLUSIVE!
if(NOT DEFINED $ENV{DISABLE_DEBUGGING})  # MATCHES
  set(USBSID_DEBUGGING 1)     # Enable debugging ~ mandatory enable for all other logging types
  set(UART_DEBUGGING 1)       # Enable debugging over uart pins
  set(USBCDC_DEBUGGING 0)     # Enable debugging over cdc port 2
  set(MEMORY_LOGGING 0)       # Enable memory map of SID 1 voices printing
  set(DEFAULT_DEBUGGING 1)    # Enable debugging in usbsid.c
  set(USBIO_DEBUGGING 0)      # Enable debugging in usbsid.c
  set(CONFIG_DEBUGGING 1)     # Enable debugging in config.c
  set(GPIOBUS_DEBUGGING 0)    # Enable debugging in gpio.c
  set(MIDI_DEBUGGING 0)       # Enable debugging in midi.c
  set(MIDIVOICE_DEBUGGING 0)  # Enable debugging in midi.c
endif()


#######################################
#### You no touchy after this line ####
#######################################

### Optimization and debugging
# remember that enabling debugging and disabling optimizations
# will weird out tune playing and reading from and writing to
# SKPico will stop functioning
SET(DEBUG_SYMBOLS 0)
SET(DEBUGGING 0)
SET(OPTIMIZATIONS 3)

### Target board pico, pico_w, pico2
if(DEFINED PICO_BOARD)
  message("PICO_BOARD is defined as ${PICO_BOARD}")
else()
  message("PICO_BOARD not defined, defaulting to 'pico'")
  set(PICO_BOARD pico)
endif()

### Target platform rp2040, rp2350 (rp2350-arm-s)
if(DEFINED PICO_PLATFORM)
  message("PICO_PLATFORM is defined as ${PICO_PLATFORM}")
else()
  if(DEFINED PICO_BOARD)
    if(PICO_BOARD STREQUAL "pico2")
      set(PICO_PLATFORM rp2350)
      message("PICO_PLATFORM not defined, board is ${PICO_BOARD} setting PICO_PLATFORM to ${PICO_PLATFORM}")
    endif()
  else()
    message("PICO_PLATFORM not defined, defaulting to 'rp2040'")
    set(PICO_PLATFORM rp2040)
  endif()
endif()

### Target product & output filename
if(${PICO_BOARD} STREQUAL "pico")
  set(PROJECT_PRODUCT ${PRODUCT_STRING})
  set(PROJECT_FILENAME ${PROJECT_NAME})
elseif(${PICO_BOARD} STREQUAL "pico_w")
  set(PROJECT_PRODUCT "${PRODUCT_STRING}_W")
  set(PROJECT_FILENAME "${PROJECT_NAME}_w")
elseif(${PICO_BOARD} STREQUAL "pico2")
  set(PROJECT_PRODUCT "${PRODUCT_STRING}2")
  set(PROJECT_FILENAME "${PROJECT_NAME}2")
elseif(${PICO_BOARD} STREQUAL "pico2_w")
  set(PROJECT_PRODUCT "${PRODUCT_STRING}2_W")
  set(PROJECT_FILENAME "${PROJECT_NAME}2_w")
endif()

### Version validation
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/config.h)
  file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/src/config.h VALIDATION_FILE)
  set(VALIDATION_SUCCESS False)
  set(VALIDATION_LINE "#define PROJECT_VERSION")
  while(VALIDATION_FILE)
    list(POP_FRONT VALIDATION_FILE LINE)
    if (LINE MATCHES ${VALIDATION_LINE})
      string(REGEX REPLACE "^${VALIDATION_LINE} " "" VERSION ${LINE})
      if(${VERSION} MATCHES ${PROJECT_VERSION})
        set(REDUCTION_SUCCESS True)
        message("VALIDATED config version ${VERSION} matches project version \"${PROJECT_VERSION}\"")
        set(USBSID_PRODUCT ${PROJECT_PRODUCT})
        set(USBSID_MANUFACTURER "${PROJECT_MANUFACTURER} (v${PROJECT_VERSION})")
        add_compile_definitions(USBSID_PRODUCT="${USBSID_PRODUCT}")
        add_compile_definitions(USBSID_MANUFACTURER="${USBSID_MANUFACTURER}")
        message("Set USB manufacturer: ${USBSID_MANUFACTURER}")
        message("Set USB product: ${USBSID_PRODUCT}")
        break()
      else()
      message("ERROR config version ${VERSION} does not match project version \"${PROJECT_VERSION}\"")
        set(REDUCTION_SUCCESS False)
        return()
      endif()
    endif()
  endwhile()
else()
  message("ERROR config.h missing")
  return()
endif()

### Cmake compiler standard versions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

### Compilers to use
if(${PICO_PLATFORM} MATCHES rp2040)
  set(PICO_COMPILER pico_arm_gcc)  # required for mem_ops on rp2040
endif()

### Target environment path
set(CMAKE_FIND_ROOT_PATH $ENV{PICO_ENV_PATH})
set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

### Optimizations
set(PICO_DEFAULT_FLOAT_IMPL pico_float_default)
set(PICO_DEFAULT_DOUBLE_IMPL pico_double_default)

### Build with debug symbols
if(DEBUG_SYMBOLS EQUAL 1)
  set(CMAKE_BUILD_TYPE Debug)  # for debugging
  set(PICO_DEOPTIMIZED_DEBUG 1)
endif()

### Define compile time debugging
if(DEBUGGING EQUAL 0)
  set(D_OPT -DNDEBUG)
elseif(DEBUGGING EQUAL 1)
  set(D_OPT -g1)
elseif(DEBUGGING EQUAL 2)
  set(D_OPT -g2)
elseif(DEBUGGING EQUAL 3)
  set(D_OPT -g3)
endif()

### Define compile time optimizations
if(OPTIMIZATIONS EQUAL 0)
  set(C_OPT -O0)
elseif(OPTIMIZATIONS EQUAL 1)
  set(C_OPT -Og)
elseif(OPTIMIZATIONS EQUAL 2)
  set(C_OPT -Ofast)
elseif(OPTIMIZATIONS EQUAL 3)
  set(C_OPT -O3)
endif()

### Compiler flags and options
set(COMPILE_OPTS PRIVATE
  ${D_OPT}
  -Wall
  -Wunused
  -Werror
  -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
  -Wno-unused-function # extra check to verify no unused lingering code is present
  -Wno-maybe-uninitialized
  -save-temps
  ${C_OPT}
  -fverbose-asm
  -Wl,--wrap=ceil
  -Wl,--wrap=memcpy
  -Wl,--wrap=memset
  -Wl,--wrap=__aeabi_memcpy
  -Wl,--wrap=__aeabi_memset
  -Wl,--wrap=__aeabi_memcpy4
  -Wl,--wrap=__aeabi_memset4
  -Wl,--wrap=__aeabi_memcpy8
  -Wl,--wrap=__aeabi_memset8
  -Wl,--wrap=printf
  -Wl,--wrap=vprintf
  -Wl,--wrap=puts
)
message("Compile time flags: ${COMPILE_OPTS}")

### Executable names
list(APPEND FILENAMES
  ${PROJECT_FILENAME}
)
list(APPEND PICOTYPES
  "LED"
)
if(${PICO_BOARD} STREQUAL "pico")
  list(APPEND FILENAMES
    ${PROJECT_FILENAME}-rgb
  )
  list(APPEND PICOTYPES
    "RGB"
  )
endif()

### Source files to compile
set(SOURCEFILES
  ${CMAKE_CURRENT_LIST_DIR}/src/usbsid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/config.c
  ${CMAKE_CURRENT_LIST_DIR}/src/gpio.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi.c
  # ${CMAKE_CURRENT_LIST_DIR}/src/midi_patches.c
  # ${CMAKE_CURRENT_LIST_DIR}/src/midi_bankmsb0.c
  ${CMAKE_CURRENT_LIST_DIR}/src/asid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/mcu.c
  ${CMAKE_CURRENT_LIST_DIR}/src/usb_descriptors.c
)

### Libraries to link
set(TARGET_LL
  hardware_clocks
  hardware_dma
  hardware_flash
  hardware_gpio
  hardware_pio
  hardware_pwm
  hardware_resets
  hardware_sync
  hardware_timer
  hardware_uart
  pico_flash
  pico_mem_ops
  pico_mem_ops_compiler
  pico_multicore
  pico_stdlib
  pico_unique_id
  pico_usb_reset_interface
  pico_async_context_threadsafe_background
  tinyusb_device
  tinyusb_board
)

### Pio files
set(PIO_BUS ${CMAKE_CURRENT_LIST_DIR}/src/pio/bus_control.pio)
set(PIO_CLOCK ${CMAKE_CURRENT_LIST_DIR}/src/pio/clock.pio)
set(PIO_RGB ${CMAKE_CURRENT_LIST_DIR}/src/pio/ws2812.pio)

### Header folders to include
set(TARGET_INCLUDE_DIRS PRIVATE
  .
  src
  ${TINYUSB_PATH}/hw
  ${TINYUSB_PATH}/src
)

### Pico sdk path
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

### Project type
project(${PROJECT_NAME} C CXX ASM)

### init sdk
pico_sdk_init()

### Include
include_directories(".")

### Double check debugging types
if(UART_DEBUGGING EQUAL 1 AND USBCDC_DEBUGGING EQUAL 1)
  message("ERROR cannot have both UART_DEBUGGING and USBCDC_DEBUGGING enabled!")
  return()
endif()

### Logging definitions
if(USBSID_DEBUGGING EQUAL 1)
  if(UART_DEBUGGING EQUAL 1)
    add_compile_definitions(USBSID_UART=1)
  endif()
  if(USBCDC_DEBUGGING EQUAL 1)
    add_compile_definitions(USB_PRINTF=1)
  endif()
  if(DEFAULT_DEBUGGING EQUAL 1)
    add_compile_definitions(USBSID_DEBUG=1)
  endif()
  if(MEMORY_LOGGING EQUAL 1)
    add_compile_definitions(MEM_DEBUG=1)
  endif()
  if(CONFIG_DEBUGGING EQUAL 1)
    add_compile_definitions(CONFIG_DEBUG=1)
  endif()
  if(USBIO_DEBUGGING EQUAL 1)
    add_compile_definitions(USBIO_DEBUG=1)
  endif()
  if(MIDI_DEBUGGING EQUAL 1)
    add_compile_definitions(MIDI_DEBUG=1)
  endif()
  if(MIDIVOICE_DEBUGGING EQUAL 1)
    add_compile_definitions(MIDIVOICE_DEBUG=1)
  endif()
  if(GPIOBUS_DEBUGGING EQUAL 1)
    add_compile_definitions(USBSIDGPIO_DEBUG=1)
  endif()
endif()

### It escapes every damn time!
add_compile_definitions(MAGIC_SMOKE=${MAGIC_SMOKE})

#add_compile_definitions(PICO_QUEUE_MAX_LEVEL=6)

### Compile targets
foreach(FILENAME PICOTYPE IN ZIP_LISTS FILENAMES PICOTYPES)
  # set filename
  set(BUILD ${FILENAME})
  message(STATUS "Building ${FILENAME} with PICOTYPE=${PICOTYPE} and MAGIC_SMOKE=${MAGIC_SMOKE}")
  # executable
  add_executable(${BUILD} ${SOURCEFILES})
  if(PICOTYPE STREQUAL "RGB")
    target_compile_definitions(${BUILD} PRIVATE USBSID USE_RGB=1)
  else()
    target_compile_definitions(${BUILD} PRIVATE USBSID)
  endif()
  # pio addition
  pico_generate_pio_header(${BUILD} ${PIO_BUS})
  pico_generate_pio_header(${BUILD} ${PIO_CLOCK})
  if(PICOTYPE STREQUAL "RGB")
    pico_generate_pio_header(${BUILD} ${PIO_RGB})
  endif()
  # source files to compile
  target_sources(${BUILD} PUBLIC ${SOURCEFILES})
  target_compile_options(${BUILD} ${COMPILE_OPTS})
  target_link_options(${BUILD} PRIVATE -Xlinker --print-memory-usage)
  if(${PICO_SDK_VERSION_MAJOR} LESS 2)
    pico_set_linker_script(${BUILD} ${PICO_SDK_PATH}/src/memmap_custom.ld)
  else()
    if(${PICO_PLATFORM} MATCHES rp2350)
      pico_set_linker_script(${BUILD} ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld)
    else()
      pico_set_linker_script(${BUILD} ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld)
    endif()
  endif()
  # tell the linker what libraries to link
  target_link_libraries(${BUILD} ${TARGET_LL})
  if (PICO_CYW43_SUPPORTED)
    target_link_libraries(${BUILD} pico_cyw43_arch_none)
  endif()
  # target usbsid types
  target_include_directories(${BUILD} ${TARGET_INCLUDE_DIRS})
  pico_set_program_name(${BUILD} "USBSID-Pico")
  pico_set_program_version(${BUILD} $PROJECT_VERSION)
  # create map/bin/hex/uf2 file in addition to ELF.
  pico_add_extra_outputs(${BUILD})
  # enable uart output, disable usb output
  pico_enable_stdio_uart(${BUILD} 1)  # essentialy the same as LL pico_stdio_uart
  pico_enable_stdio_usb(${BUILD} 0)
endforeach()
