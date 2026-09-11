####
# USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
# for interfacing one or two MOS SID chips and/or hardware SID emulators over
# (WEB)USB with your computer, phone or ASID supporting player
#
# CMakeLists.txt
# This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
# File author: LouD
#
# Copyright (c) 2024-2026 LouD
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

### Cmake compiler standard versions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

### CMake stuff for ZED
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

### Project magic sprinkles
set(PROJECT_NAME usbsidpico)
set(PROJECT_MANUFACTURER "LouD")
set(PRODUCT_STRING "USBSID-Pico")
string(TIMESTAMP MAGIC_SMOKE "%Y%m%d")  # Auto generate dated version
set(FWVERSION "0.8.1")
set(PROJECT_VERSION "${FWVERSION}-${MAGIC_SMOKE}")  # Generate compile version

### Want a cookie?
# NOTICE: ENABLING THESE DEBUGGING DEFINITIONS WILL HAVE SIGNIFICANT IMPACT AND WILL DELAY PLAYING!
# NOTICE: UART AND USBCDC ARE MUTUALLY EXCLUSIVE!
if(NOT DEFINED $ENV{DISABLE_DEBUGGING})  # MATCHES
  set(BUILD_DEBUGGING DEBUG)  # Set loglevel from CMake build steps (STATUS,DEBUG,TRACE) override with --log-level
  set(USBSID_DEBUGGING 1)     # Enable debugging ~ mandatory enable for all other logging types
  set(UART_DEBUGGING 1)       # Enable debugging over uart pins ~ USBCDC must be 0!
  set(USBCDC_DEBUGGING 0)     # Enable debugging over cdc port 2 ~ UART must be 0!
  set(FILENAME_LOGGING 0)     # Enable logging of source filename
  set(BOOT_LOGGING 0)         # Enable bootlog in usbsid.c
  set(DEFAULT_DEBUGGING 1)    # Enable debugging in usbsid.c
  set(CONFIG_DEBUGGING 1)     # Enable debugging in config.c
  set(USBIO_DEBUGGING 0)      # Enable debugging in usbsid.c
  set(GPIOBUS_DEBUGGING 0)    # Enable debugging in gpio.c
  set(MIDI_DEBUGGING 0)       # Enable debugging in midi.c
  set(MIDIVOICE_DEBUGGING 0)  # Enable debugging in midi.c ~ usMVCE/usMDAT fire on every message
  set(ASID_DEBUGGING 1)       # Enable debugging in acid.c and asid_buffer.c
  set(MEMORY_LOGGING 0)       # Enable memory map of SID 1 voices printing
  set(SIDWRITES_DEBUGGING 0)  # Enable logging of SID writes one core 2
  set(EMULATOR_DEBUGGING 1)   # Enable debugging in emulator
  set(BT_DIAG_DEBUGGING 0)    # Enable verbose per-HCI-event logging in bluetooth.c ~ floods UART, causes audible lag while playing
  set(ENABLE_PROFILING 0)     # Experimental!
endif()

### Enable / Disable build with USBSID-Player for tune playback or Cynthcart emulation
set(ONBOARD_EMULATOR 0 CACHE STRING "ONBOARD_EMULATOR")

### Enable / Disable build with PIO Uart on unused pins 26/27
set(USE_PIO_UART 0)

### Enable / Disable build with WiFi/Bluetooth Network SID Device (NSD) support on _w boards
set(ENABLE_NET 0 CACHE STRING "ENABLE_NET")

### Enable/ Disable build with CDC telemetry port (pico2 with sidplayer only!)
set(ENABLE_TELEMETRY 0 CACHE STRING "ENABLE_TELEMETRY")



#######################################
#### You no touchy after this line ####
#######################################

### Build notes
# Do not use the below setting, this _will_ break compilation on both
# rp2040 and rp2350
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -std=c++11")
# rp2040 will bug out complaining about assembly calls not being available
# rp2350 will bug out on PICO_USE_SW_SPIN_LOCK not being available

# Define ANSI Color Escapes (Using native CMake brackets for character escapes)
string(ASCII 27 Esc)
set(ColorReset  "${Esc}[m")
set(ColorBold   "${Esc}[1m")
set(Red         "${Esc}[31m")
set(Green       "${Esc}[32m")
set(Yellow      "${Esc}[33m")
set(Blue        "${Esc}[34m")
set(Magenta     "${Esc}[35m")
set(Cyan        "${Esc}[36m")

### Build steps log level
set(CMAKE_MESSAGE_LOG_LEVEL ${BUILD_DEBUGGING})
message(DEBUG "${Green}${ColorBold}LOG_LEVEL${ColorReset} is ${Yellow}${ColorBold}${CMAKE_MESSAGE_LOG_LEVEL}${ColorReset}")

message(DEBUG "Starting compilation of ${Yellow}${ColorBold}${PRODUCT_STRING}${ColorReset} firmware version ${Yellow}${ColorBold}${PROJECT_VERSION}${ColorReset}")

### Generate feature list for logging
set(FEATURE_LIST)

if(ONBOARD_EMULATOR EQUAL 1)
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}SID Player${ColorReset}")
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}Cynthcart${ColorReset}")
endif()
if(ENABLE_NET EQUAL 1)
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}WiFi${ColorReset}")
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}Bluetooth${ColorReset}")
endif()
if(USE_PIO_UART EQUAL 1)
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}PIO Uart${ColorReset}")
endif()
if(ENABLE_TELEMETRY EQUAL 1)
  list(APPEND FEATURE_LIST "${Cyan}${ColorBold}Telemetry${ColorReset}")
endif()

### Replace the semi colon in the list (This will not work: list(JOIN FEATURE_LIST ", " FEATURE_STRING))
string(REPLACE ";" ", " FEATURE_STRING "${FEATURE_LIST}")

### Set the list variable with colors, yay \o/
set(BUILD_FEATURES "${Green}${ColorBold}Build additional features:${ColorReset} ${FEATURE_STRING}")

### Optimization and debugging
# remember that enabling debugging and disabling optimizations
# will weird out tune playing and reading from and writing to
# SKPico will stop functioning. *3 is also required for
# onboard sidplayer/cynthcart.
# If any desync issues arise with DMA read_address and chain_to()
# this "could" be the cause
set(DEBUG_SYMBOLS 0)      # 0 default, 1 debug symbols & debug build enabled
set(DEBUGGING 0)          # 0 default, 1~3 equals -g1~3
set(OPTIMIZATIONS 3)      # 3 default, 3 ~ -O1 for debugging
set(WARNINGS_ONLY 0)      # 0 default, 0 error on warnings
set(TUSB_DEBUG 0)         # 0 default, 0~3 non verbose to very verbose
set(ANALYSE_STACKUSAGE 0) # 0 default, 1 enabled, 0 disabled

### Target board pico, pico_w, pico2
if(DEFINED PICO_BOARD)
  message(STATUS "${Green}${ColorBold}PICO_BOARD${ColorReset} is defined as ${Yellow}${ColorBold}${PICO_BOARD}${ColorReset}")
else()
  message(WARNING "${Green}${ColorBold}PICO_BOARD${ColorReset not defined, defaulting to ${Yellow}'pico'${ColorReset}")
  set(PICO_BOARD pico)
endif()

### Target platform rp2040, rp2350 (rp2350-arm-s)
if(DEFINED PICO_PLATFORM)
  message(STATUS "${Green}${ColorBold}PICO_PLATFORM${ColorReset} is defined as ${Yellow}${ColorBold}${PICO_PLATFORM}${ColorReset}")
else()
  if(DEFINED PICO_BOARD)
    if(PICO_BOARD STREQUAL "pico2")
      set(PICO_PLATFORM rp2350-arm-s)
      message(STATUS "${Green}${ColorBold}PICO_PLATFORM${ColorReset} not defined, board is ${Green}${ColorBold}${PICO_BOARD}${ColorReset} setting ${Green}${ColorBold}PICO_PLATFORM${ColorReset} to ${Yellow}${PICO_PLATFORM}${ColorReset}")
    endif()
  else()
    message(WARNING "${Green}${ColorBold}PICO_PLATFORM${ColorReset} not defined, defaulting to ${Yellow}${ColorBold}'rp2040'${ColorReset}")
    set(PICO_PLATFORM rp2040)
  endif()
endif()

### Header validation check
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/globals.h)
  file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/src/globals.h VALIDATION_FILE)
  set(VALIDATION_SUCCESS False)
  set(VALIDATION_LINE "#define PROJECT_VERSION")
  while(VALIDATION_FILE)
    list(POP_FRONT VALIDATION_FILE LINE)
    if (LINE MATCHES ${VALIDATION_LINE})
      string(REGEX MATCH "^#define PROJECT_VERSION" VALIDATED ${LINE})
      if(${VALIDATED} MATCHES ${VALIDATION_LINE})
        set(REDUCTION_SUCCESS True)
        message(DEBUG "VALIDATED header line '${VALIDATION_LINE}'")
        set(USBSID_MANUFACTURER "${PROJECT_MANUFACTURER} (v${PROJECT_VERSION})")
        add_compile_definitions(USBSID_MANUFACTURER="${USBSID_MANUFACTURER}")
        ### CMake shenanigens
        set(PJVER ${PROJECT_VERSION})
        add_compile_definitions(PROJECT_VERSION="${PJVER}")
        break()
      else()
        message(SEND_ERROR "ERROR header validation line is present but incorrect")
        set(REDUCTION_SUCCESS False)
        return()
      endif()
    endif()
  endwhile()
  if(NOT REDUCTION_SUCCESS)
    message(SEND_ERROR "ERROR header validation fallback line not present")
    return()
  endif()
else()
  message(SEND_ERROR "ERROR globals.h missing")
  return()
endif()

### Compilers to use
if(${PICO_PLATFORM} MATCHES rp2040)
  set(PICO_COMPILER pico_arm_cortex_m0plus_gcc)
elseif(${PICO_PLATFORM} MATCHES rp2350-arm-s)
  set(PICO_COMPILER pico_arm_cortex_m33_gcc)
else()
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
  if(TUSB_DEBUG EQUAL 0)
    set(CFG_TUSB_DEBUG 1)
    # add_compile_definitions(CFG_TUSB_DEBUG=1)
  else()
    set(CFG_TUSB_DEBUG TUSB_DEBUG)
  endif()
else()
  set(CMAKE_BUILD_TYPE Release)
  set(PICO_DEOPTIMIZED_DEBUG 0)
  ### TinyUSB debug logging
  if(TUSB_DEBUG GREATER 0)
    message(DEBUG "TinyUSB debugging set to ${TUSB_DEBUG}")
    set(CFG_TUSB_DEBUG ${TUSB_DEBUG})
    # add_compile_definitions(CFG_TUSB_DEBUG=${TUSB_DEBUG})
  else()
    set(CFG_TUSB_DEBUG 0)
  endif()
endif()

message(STATUS "Firmware build type is ${CMAKE_BUILD_TYPE}")

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
  set(O_OPT -O0)
elseif(OPTIMIZATIONS EQUAL 1)
  set(O_OPT -Og)
elseif(OPTIMIZATIONS EQUAL 2)
  set(O_OPT -Ofast)
elseif(OPTIMIZATIONS EQUAL 3)
  set(O_OPT -O3)
endif()

### Log compilation errors as warnings?
if(WARNINGS_ONLY EQUAL 1)
  set(W_OPT
    -Wall
    -Wextra
    -Wunused
  )
### Log warnings as errors
else()
  set(W_OPT
    -Werror
    -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
    -Wno-unused-function # extra check to verify no unused lingering code is present
    -Wno-maybe-uninitialized
    -Wno-unused
    -Wno-misleading-indentation
    -Wno-implicit-fallthrough
  )
endif()

### Compiler flags and options
set(COMPILE_OPTS PRIVATE
  ${D_OPT}
  ${W_OPT}
  ${O_OPT}
  -save-temps=obj
  -fverbose-asm
  -Wl,--wrap=ceil
  -Wl,--wrap=memcpy
  -Wl,--wrap=memset
  -Wl,--wrap=__aeabi_memcpy
  -Wl,--wrap=__aeabi_memset
  -Wl,--wrap=__aeabi_memcmp
  -Wl,--wrap=__aeabi_memcpy4
  -Wl,--wrap=__aeabi_memset4
  -Wl,--wrap=__aeabi_memcpy8
  -Wl,--wrap=__aeabi_memset8
  -Wl,--wrap=printf
  -Wl,--wrap=vprintf
  -Wl,--wrap=puts
)

### Stack usage debugging
if(ANALYSE_STACKUSAGE EQUAL 1)
  set(COMPILE_OPTS
    ${COMPILE_OPTS}
     -fstack-usage
     -fcallgraph-info
     -DANALYSE_STACKUSAGE=1)
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

### Executable names
list(APPEND FILENAMES
  ${PROJECT_FILENAME}-v1.0
  ${PROJECT_FILENAME}-v1.3
  ${PROJECT_FILENAME}-v1.5
)
list(APPEND LEDTYPES
  "LED"
  "LED"
  "LED"
)
list(APPEND PCBVERSIONS
  "1.0"
  "1.3"
  "1.5"
)
if(${PICO_BOARD} STREQUAL "pico" OR ${PICO_BOARD} STREQUAL "pico2")
  list(APPEND FILENAMES
    ${PROJECT_FILENAME}-v1.0-rgb
    ${PROJECT_FILENAME}-v1.3-rgb
    ${PROJECT_FILENAME}-v1.5-rgb
  )
  list(APPEND LEDTYPES
    "RGB"
    "RGB"
    "RGB"
  )
  list(APPEND PCBVERSIONS
  # "0.1"  ## Alpha builds only
  # "0.2"  ## Unreliable
  "1.0"
  # "1.1"  ## Never built
  # "1.2"  ## Incorrect gpio pins placing
  "1.3"
  # "1.4"  ## Power delivery issue
  "1.5"
  )
endif()

### Target override for Pro version
if(${PICO_BOARD} STREQUAL "weact_studio_rp2350b_core")
  set(PROJECT_PRODUCT "${PRODUCT_STRING}Pro_rp2350b")
  set(PROJECT_FILENAME "${PROJECT_NAME}pro_rp2350b")
  set(FILENAMES)
  set(LEDTYPES)
  set(PCBVERSIONS)
  list(APPEND FILENAMES ${PROJECT_FILENAME}-v1.0)
  list(APPEND LEDTYPES "PRO")
  list(APPEND PCBVERSIONS "1.0")
endif()

### Source files to compile
set(SOURCEFILES
  ${CMAKE_CURRENT_LIST_DIR}/src/usbsid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/usbsid_constants.c
  ${CMAKE_CURRENT_LIST_DIR}/src/config.c
  ${CMAKE_CURRENT_LIST_DIR}/src/config_bus.c
  ${CMAKE_CURRENT_LIST_DIR}/src/config_socket.c
  ${CMAKE_CURRENT_LIST_DIR}/src/config_logging.c
  ${CMAKE_CURRENT_LIST_DIR}/src/gpio.c
  ${CMAKE_CURRENT_LIST_DIR}/src/pio.c
  ${CMAKE_CURRENT_LIST_DIR}/src/dma.c
  ${CMAKE_CURRENT_LIST_DIR}/src/vu.c
  ${CMAKE_CURRENT_LIST_DIR}/src/bus.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_handler.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_queue.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_engine.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_voice.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_config.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_patch.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_arp_table.c
  ${CMAKE_CURRENT_LIST_DIR}/src/midi_fmopl.c
  ${CMAKE_CURRENT_LIST_DIR}/src/asid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/asid_buffer.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sysex.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid_cloneconfig.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid_detection.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid_player.c
  ${CMAKE_CURRENT_LIST_DIR}/src/sid_tests.c
  ${CMAKE_CURRENT_LIST_DIR}/src/mcu.c
  ${CMAKE_CURRENT_LIST_DIR}/src/usb_descriptors.c
)

### Header directories to include
set(TARGET_INCLUDE_DIRS PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${TINYUSB_PATH}/hw
  ${TINYUSB_PATH}/src
)

### Embedded player stuffs
if(ONBOARD_EMULATOR EQUAL 1)
  ## Empty by default for Github workflows.
  set(EMUDIR "" CACHE STRING "Path to the USBSID-Player checkout")
  # NOTICE: SIDPLAYER REQUIRES FULL -O3 OPTIMIZATIONS!!
  # NOTE TO SELF: Do NOT use set(CMAKE_CXX_FLAGS ...) here as it strips Pico SDK architecture flags!
  ## Set USBSID-Player list directory
  ## If set EMUDIR wins, then lib/usbsid-player, then local build checkout
  if(NOT EMUDIR STREQUAL "")
    if(IS_ABSOLUTE "${EMUDIR}")
      set(USPLAYER_LIST_DIR "${EMUDIR}")
    else()
      set(USPLAYER_LIST_DIR "")
      foreach(BASE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/.." "${CMAKE_BINARY_DIR}")
        get_filename_component(EMUDIR_TRY "${BASE}/${EMUDIR}" ABSOLUTE)
        if(EXISTS "${EMUDIR_TRY}/cmake/usbsid-player.cmake")
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
  elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/usbsid-player/cmake/usbsid-player.cmake")
    set(USPLAYER_LIST_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/usbsid-player")
  else()
    get_filename_component(USPLAYER_LIST_DIR
      "${CMAKE_CURRENT_LIST_DIR}/../player-repo" ABSOLUTE)
  endif()
  message(STATUS "USPLAYER_LIST_DIR is set to ${USPLAYER_LIST_DIR}")

  if(NOT EXISTS "${USPLAYER_LIST_DIR}/cmake/usbsid-player.cmake")
    message(FATAL_ERROR
      "ONBOARD_EMULATOR is on but no USBSID-Player at ${USPLAYER_LIST_DIR}.\n"
      "  EMUDIR was '${EMUDIR}'\n"
      "  Pass an absolute -DEMUDIR=/path/to/usbsid-player, or check the player "
      "out as lib/usbsid-player, or put it beside this repo as ../player-repo.")
  endif()

  ## The player brings its own file list, include paths and psiddrv target.
  include(${USPLAYER_LIST_DIR}/cmake/usbsid-player.cmake)

  set(COMPILE_OPTS ${COMPILE_OPTS} -DEMBEDDED=1)
  add_compile_definitions(ONBOARD_EMULATOR=1)
  add_compile_definitions(${USPLAYER_DEFINITIONS})

  ### Where it's at
  set(SOURCEFILES
    ${SOURCEFILES}
    ${USPLAYER_SOURCES}
  )
  ### What it wants
  set(TARGET_INCLUDE_DIRS
    ${TARGET_INCLUDE_DIRS}
    ${USPLAYER_INCLUDE_DIRS}
  )

  ### Cynthcart requires the player's MC68B50 ACIA (see cmake/usbsid-player.cmake)
  set(SOURCEFILES ${SOURCEFILES} ${USPLAYER_CYNTHCART_SOURCES})
  set(TARGET_INCLUDE_DIRS ${TARGET_INCLUDE_DIRS} ${USPLAYER_CYNTHCART_INCLUDE_DIRS})

  ### Adjust filenames for user interpretation
  foreach(FILENAME ${FILENAMES})
    list(APPEND FILENAMES_tmp "${FILENAME}_em")
  endforeach()
  set(FILENAMES ${FILENAMES_tmp})
endif()

### PIO Uart compilation additions
if(USE_PIO_UART EQUAL 1)
  add_compile_definitions(USE_PIO_UART=1)
  set(SOURCEFILES
    ${SOURCEFILES}
    ${CMAKE_CURRENT_LIST_DIR}/src/uart.c
  )
endif()

### Libraries to link
set(TARGET_LL
  hardware_clocks
  hardware_dma
  hardware_exception
  hardware_flash
  hardware_gpio
  hardware_pio
  hardware_pwm
  hardware_resets
  hardware_structs
  hardware_sync
  hardware_timer
  hardware_uart
  # hardware_interp
  pico_atomic # memcmp
  pico_cxx_options
  pico_flash
  pico_mem_ops
  pico_mem_ops_compiler
  pico_malloc
  pico_multicore
  pico_rand
  pico_stdlib
  pico_unique_id
  pico_usb_reset_interface
  pico_async_context_threadsafe_background
  tinyusb_device
  tinyusb_board
)

### Only pico_w and pico2_w have the CYW43439 chip
set(USBSID_W_BOARD 0)
if(${PICO_BOARD} STREQUAL "pico_w" OR ${PICO_BOARD} STREQUAL "pico2_w")
  set(USBSID_W_BOARD 1)
endif()

### Add _w libs and/or configure WiFi and Bluetooth for pico_w and pico2_w
set(WIFIBT_TARGET_PIO 0)
if(USBSID_W_BOARD)

  ## Network SID Device
  if(ENABLE_NET EQUAL 1)
    set(SOURCEFILES
      ${SOURCEFILES}
      ${CMAKE_CURRENT_LIST_DIR}/src/nsd.c
      ${CMAKE_CURRENT_LIST_DIR}/src/net_ring.c
      ${CMAKE_CURRENT_LIST_DIR}/src/net_wifi_config.c  # Config required either way
    )
    add_compile_definitions(USE_NSD=1)
  endif()

  ## WiFi (TCP + UDP discovery) transport and Bluetooth
  if(ENABLE_NET EQUAL 1)
    set(SOURCEFILES
      ${SOURCEFILES}
      ${CMAKE_CURRENT_LIST_DIR}/src/net_wifi.c
      ${CMAKE_CURRENT_LIST_DIR}/src/bluetooth.c
    )
    add_compile_definitions(USE_NET=1)
    add_compile_definitions(NSD_DEFAULT_PORT=6581)
  endif()

  ## WiFi & Bluetooh Link Poll mode + filename extension definition
  set(FILENAME_EXTENSION)
  if(ENABLE_NET EQUAL 1)
    set(TARGET_LL
      ${TARGET_LL}
        pico_cyw43_arch_lwip_poll
        pico_btstack_classic
        pico_btstack_cyw43
    )
    set(FILENAME_EXTENSION "_net")
  # Bluetooth only ~ Legacy build, no longer used and deprecated
  elseif(FALSE AND (ENABLE_BLUETOOTH EQUAL 1) AND NOT (ENABLE_WIFI EQUAL 1))
    set(TARGET_LL
      ${TARGET_LL}
      pico_cyw43_arch_none  # Select 'none', 'background', or 'rtos' architecture
      pico_btstack_classic  # Main stack for Classic SPP Bluetooth
      pico_btstack_cyw43    # Ties BTstack to the onboard CYW chip
    )
    set(FILENAME_EXTENSION "_netb")
  # WiFi ~ Legacy build, no longer used and deprecated
  elseif(FALSE AND (ENABLE_WIFI EQUAL 1) AND NOT (ENABLE_BLUETOOTH EQUAL 1))
    set(TARGET_LL
      ${TARGET_LL}
      pico_cyw43_arch_lwip_poll  # Polled from core 0, see net_wifi.c
      pico_lwip_nosys            # Provides the sys_arch.h NO_SYS=1 needs
    )
    set(FILENAME_EXTENSION "_netw")
  # If neither but still a _w board we require a library to control the LED
  elseif(NOT (ENABLE_NET EQUAL 1))
    set(TARGET_LL
      ${TARGET_LL}
      pico_cyw43_arch_none  # Select 'none', 'background', or 'rtos' architecture
    )
  endif()

  ## The WiFi/BT chip communicates SPI via a PIO so we need to define
  if(ENABLE_NET EQUAL 1)
    # What Pio to target
    if(${PICO_PLATFORM} MATCHES rp2350-arm-s)
      set(WIFIBT_TARGET_PIO 2)
    else()
      set(WIFIBT_TARGET_PIO 1)
    endif()

    # Enable the primary print macro for the CYW43 Wi-Fi driver
    add_compile_definitions(CYW43_PRINTF_ENABLED=1)
    # Enable detailed verbose logging, informational logs, and warning tracking
    add_compile_definitions(CYW43_LOGIC_DEBUG=1)
    # Enable lwIP Network Stack Debugging
    add_compile_definitions(LWIP_DEBUG=1)

    ### Add filename for build type
    foreach(FILENAME ${FILENAMES})
      list(APPEND FILENAMES_net "${FILENAME}${FILENAME_EXTENSION}")
    endforeach()
    set(FILENAMES ${FILENAMES_net})
  endif()
endif()

### Enable Profiling
if(ENABLE_PROFILING EQUAL 1)
  add_compile_definitions(ENABLE_PROFILING=1)
  # set(COMPILE_OPTS
  #   ${COMPILE_OPTS}
  #   -pg
  #   -mgeneral-regs-only
  #   )
  # set(TARGET_LL
  #   ${TARGET_LL}
  #   pico_stdio_semihosting
  # )
  # set(TARGET_INCLUDE_DIRS
  #     ${TARGET_INCLUDE_DIRS}
  #     ${CMAKE_CURRENT_LIST_DIR}/lib/McuLib/config
  #     ${CMAKE_CURRENT_LIST_DIR}/lib/McuLib/src
  #     ${CMAKE_CURRENT_LIST_DIR}/lib/CrashCourseC/Content/lib
  # )
  # add_link_options(-pg --specs=rdimon.specs)
  # pico_enable_stdio_semihosting(${CMAKE_PROJECT_NAME} ENABLED)
endif()

### Enable Telemetry and update the filenames if needed
if(ENABLE_TELEMETRY EQUAL 1)
  if(${PICO_BOARD} STREQUAL "pico2" OR ${PICO_BOARD} STREQUAL "pico2_w")
    add_compile_definitions(USE_TELEMETRY=1)
    ### Add filename id
    foreach(FILENAME ${FILENAMES})
      list(APPEND FILENAMES_telemetry "${FILENAME}_tl")
    endforeach()
    set(FILENAMES ${FILENAMES_telemetry})
  endif()
endif()

### Pio files
set(PIO_BUS     ${CMAKE_CURRENT_LIST_DIR}/src/pio/bus_control.pio)
set(PIO_CLOCK   ${CMAKE_CURRENT_LIST_DIR}/src/pio/clock.pio)
set(PIO_VU      ${CMAKE_CURRENT_LIST_DIR}/src/pio/vu.pio)
set(PIO_RGB     ${CMAKE_CURRENT_LIST_DIR}/src/pio/vu_rgb.pio)
set(PIO_COUNTER ${CMAKE_CURRENT_LIST_DIR}/src/pio/cycle_counter.pio)
if(USE_PIO_UART EQUAL 1)
set(PIO_UART    ${CMAKE_CURRENT_LIST_DIR}/src/pio/uart_rx.pio)
endif()

### Pico sdk path
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
#include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake) # i2s audio

### Project type
project(${PROJECT_NAME} C CXX ASM)

### init sdk
pico_sdk_init()

### Explicitely force pioasm to complete before any firmware targets on clean builds
if(TARGET pioasmBuild)
  set(PIOASM_DEPENDENCY pioasmBuild)
endif()

### Include
include_directories(".")

### Double check debugging types
if(UART_DEBUGGING EQUAL 1 AND USBCDC_DEBUGGING EQUAL 1)
  message(SEND_ERROR "ERROR cannot have both UART_DEBUGGING and USBCDC_DEBUGGING enabled!")
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
  if(FILENAME_LOGGING EQUAL 1)
    add_compile_definitions(LOG_FILENAME=1)
  endif()
  if(BOOT_LOGGING EQUAL 1)
    add_compile_definitions(USBSID_BOOTLOG=1)
  endif()
  if(CONFIG_DEBUGGING EQUAL 1)
    add_compile_definitions(CONFIG_DEBUG=1)
  endif()
  if(MEMORY_LOGGING EQUAL 1)
    add_compile_definitions(MEM_DEBUG=1)
  endif()
  if(SIDWRITES_DEBUGGING EQUAL 1)
    add_compile_definitions(WRITE_DEBUG=1)
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
  if(ASID_DEBUGGING EQUAL 1)
    add_compile_definitions(ASID_DEBUG=1)
  endif()
  if(GPIOBUS_DEBUGGING EQUAL 1)
    add_compile_definitions(USBSIDGPIO_DEBUG=1)
  endif()
  if(EMULATOR_DEBUGGING EQUAL 1)
    add_compile_definitions(EMUDEBUG=1)
  endif()
  if(BT_DIAG_DEBUGGING EQUAL 1)
    add_compile_definitions(BT_DIAG_DEBUG=1)
  endif()
endif()

### It escapes every damn time!
add_compile_definitions(MAGIC_SMOKE=${MAGIC_SMOKE})

### Captain I canna change the options anymore!
message(TRACE "C Compiler root: " ${CMAKE_FIND_ROOT_PATH})
message(TRACE "CXX Compiler root: " ${CMAKE_FIND_ROOT_PATH})
message(TRACE "C Compile time flags:")
message(TRACE "${CMAKE_C_FLAGS}")
message(TRACE "CXX Compile time flags:")
message(TRACE "${CMAKE_CXX_FLAGS}")
message(TRACE "Compile time options:")
message(TRACE "${COMPILE_OPTS}")
message(TRACE "Compile time source files:")
message(TRACE "${SOURCEFILES}")
message(TRACE "Compile time include dirs:")
message(TRACE "${TARGET_INCLUDE_DIRS}")

### Compile versions
# Strip leading zeros so the literal can never be mistaken for octal.
# Any literal starting with "0" is parsed as octal, octal digits only run 0-7.
# Version 0.8.0 threw an octal compilation error.
string(REPLACE "." "" FWVER_INT "${FWVERSION}")
string(REGEX REPLACE "^0+" "" FWVER_INT "${FWVER_INT}")
if(FWVER_INT STREQUAL "")
  set(FWVER_INT "0")
endif()

### Local build logic
string(FIND "${CMAKE_CURRENT_LIST_DIR}" "USBSID-Pico-dev" SUBSTRING_INDEX)
if(NOT SUBSTRING_INDEX EQUAL -1)
  message(STATUS "${Cyan}${ColorBold}Build is local, forcing single linker pool${ColorReset}")
  # Force linker to a single build pool for logical memory usage output
  set_property(GLOBAL PROPERTY JOB_POOLS link_pool=1)
  # Set CMake to use this pool for all link targets
  set(CMAKE_JOB_POOL_LINK link_pool)
endif()

### Build loop
foreach(FILENAME LEDTYPE PCBVERSION IN ZIP_LISTS FILENAMES LEDTYPES PCBVERSIONS)

  # set filename
  set(BUILD ${FILENAME})
  # set build version
  set(BUILD_VERSION "")
  string(CONCAT BUILD_VERSION ${PROJECT_PRODUCT} " v" ${PCBVERSION})

  # Build logging
  message(DEBUG "Building ${Yellow}${ColorBold}${FILENAME}${ColorReset} with LEDTYPE=${Yellow}${ColorBold}${LEDTYPE}${ColorReset} and MAGIC_SMOKE=${Yellow}${ColorBold}${MAGIC_SMOKE}${ColorReset}")
  if(FEATURE_LIST)
    message(STATUS ${BUILD_FEATURES})
  endif()
  message(DEBUG "Set USB manufacturer: ${Yellow}${ColorBold}${USBSID_MANUFACTURER}${ColorReset}")
  message(DEBUG "Set USB product: ${Yellow}${ColorBold}${BUILD_VERSION}${ColorReset}")

  # executable
  add_executable(${BUILD} ${SOURCEFILES})

  # Build types (with octal build error fix)
  string(REPLACE "." "" PCBVER_INT "${PCBVERSION}")
  string(REGEX REPLACE "^0+" "" PCBVER_INT "${PCBVER_INT}")
  if(PCBVER_INT STREQUAL "")
    set(PCBVER_INT "0")
  endif()

  # Default build definitions
  target_compile_definitions(${BUILD} PRIVATE USBSID
    USBSID_PRODUCT="${BUILD_VERSION}"
    PCB_VERSION="${PCBVERSION}"
    PCB_VERSION_INT=${PCBVER_INT}
    FW_VERSION_INT=${FWVER_INT}
  )

  # RGB board definition
  if(LEDTYPE STREQUAL "RGB")
    target_compile_definitions(${BUILD} PRIVATE USBSID
    USE_RGB=1
    )
  endif()

  # WiFi / Bluetooth definitions
  if (USBSID_W_BOARD AND (ENABLE_NET EQUAL 1))
    # Redirect the CYW43 wireless driver off PIO0 (the SID bus)
    target_compile_definitions(${BUILD} PRIVATE
      CYW43_SPI_PIO_PREFERRED_PIO=${WIFIBT_TARGET_PIO}
    )
  endif()

  # pio headers to compile
  pico_generate_pio_header(${BUILD} ${PIO_BUS})
  pico_generate_pio_header(${BUILD} ${PIO_CLOCK})
  pico_generate_pio_header(${BUILD} ${PIO_VU})
  if(USE_PIO_UART EQUAL 1)
  pico_generate_pio_header(${BUILD} ${PIO_UART})
  endif()
  pico_generate_pio_header(${BUILD} ${PIO_COUNTER})
  if(LEDTYPE STREQUAL "RGB")
    pico_generate_pio_header(${BUILD} ${PIO_RGB})
  endif()

  # rsiddrv dependency for sidplayer
  if(ONBOARD_EMULATOR EQUAL 1)
    add_dependencies(${BUILD} RSIDDriver)
  endif()

  # force pioasm to build first on clean builds
  if(DEFINED PIOASM_DEPENDENCY)
    add_dependencies(${BUILD} ${PIOASM_DEPENDENCY})
  endif()

  # source files to compile
  target_sources(${BUILD} PUBLIC ${SOURCEFILES})
  # compile options
  target_compile_options(${BUILD} ${COMPILE_OPTS})

  # memory map linkers
  if(${PICO_PLATFORM} MATCHES rp2350-arm-s)
    if(${PICO_BOARD} STREQUAL "weact_studio_rp2350b_core")
      message(TRACE "Memmap linker, platform ${PICO_PLATFORM} MATCHES rp2350b")
      target_compile_definitions(${BUILD} PRIVATE USBSID PICO_EMBED_XIP_SETUP=1)
      set_target_properties(${BUILD} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/src/usbsidpico-rp2350b.ld)
    else()
      message(TRACE "Memmap linker, platform ${PICO_PLATFORM} MATCHES rp2350")
      target_compile_definitions(${BUILD} PRIVATE USBSID PICO_EMBED_XIP_SETUP=1)
      set_target_properties(${BUILD} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/src/usbsidpico-rp2350.ld)
    endif()
  else()
    message(TRACE "Memmap linker, platform ${PICO_PLATFORM} EQUALS rp2040")
    set_target_properties(${BUILD} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/src/usbsidpico-rp2040.ld)
  endif()

  # tell the linker what libraries to link
  target_link_libraries(${BUILD} ${TARGET_LL})
  # link options
  target_link_options(${BUILD} PRIVATE -Xlinker --print-memory-usage)

  # target usbsid types
  target_include_directories(${BUILD} ${TARGET_INCLUDE_DIRS})
  pico_set_program_name(${BUILD} "USBSID-Pico")
  pico_set_program_version(${BUILD} "${PROJECT_VERSION}")

  # create map/bin/hex/uf2 file in addition to ELF.
  pico_add_extra_outputs(${BUILD})

  # enable uart output, disable usb output
  pico_enable_stdio_uart(${BUILD} 1)  # essentialy the same as LL pico_stdio_uart
  pico_enable_stdio_usb(${BUILD} 0)

endforeach()
