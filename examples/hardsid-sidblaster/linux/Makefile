# Makefile for USBSID-Pico HardSID & SIDBlaster driver
# LouD 2024

CC = gcc
CXX = g++

# Default build flags (empty)
# CPPFLAGS = -g3 -shared
CPPFLAGS = -Wall
# ISSUE: THREADING IS NOT WORKING YET
CXXFLAGS = -g3 -shared -DASYNC_THREADING=0 #-DHSDBEBUG
LDFLAGS = -Wl,-export-dynamic -fPIC
# LDFLAGS =

# Default libs
CPPLIBS =
LDLIBS =

# Make default build and debug build
DEFAULT = all
.DEFAULT_GOAL := $(DEFAULT)
DEBUG := $(DEFAULT)

# Out files
# TARGET_OBJ_HARDSID := libhardsid.o
# TARGET_OBJ_HARDSIDUSB := libhardsid_usb.o
TARGET_EXEC_HARDSID := libhardsid.so
TARGET_EXEC_HARDSIDUSB := libhardsid_usb.so
TARGET_HEADER_HARDSID := libhardsid.hpp
TARGET_HEADER_HARDSID_L := hardsid.h
TARGET_HEADER_HARDSIDUSB := libhardsid_usb.hpp

# Install directories
TARGET_LIB_DIR := /usr/local/lib
TARGET_INCLUDE_DIR := /usr/local/include
TARGET_INCLUDE_DIR_LL := /usr/local/include/linux
TARGET_INCLUDE_DIR_L := /usr/include/linux

BUILD_DIR := ./output
SRC_DIRS := ./src

# Specific cpplibs
CPPLIBS += $(shell pkg-config --cflags libusb-1.0)

# Specific ldlibs
LDLIBS += $(shell pkg-config --libs libusb-1.0)

# Default source files
SRCS := $(shell find $(SRC_DIRS) -name '*.cpp' -or -name '*.c' -or -name '*.s')

# Default header file
HEADER := hardsid.hpp

# make all
all: default libhardsid libhardsid_usb clean
libhardsid: build_libhardsid
libhardsid_usb: build_libhardsid_usb

.PHONY : clean
clean:
  $(info Cleaning build directory)
  rm -f $(BUILD_DIR)/*.o
  rm -f $(BUILD_DIR)/*.a

.PHONY : clean_all
clean_all:
  $(info Removing build directory contents)
  rm -f $(BUILD_DIR)/*
  rm -f $(BUILD_DIR)/.*
  rm -f $(BUILD_DIR)/*.*

install:
  mkdir -p $(TARGET_LIB_DIR)
  mkdir -p $(TARGET_INCLUDE_DIR_L)
  mkdir -p $(TARGET_INCLUDE_DIR_LL)
  cp $(BUILD_DIR)/$(TARGET_EXEC_HARDSID) $(TARGET_LIB_DIR)
  cp $(BUILD_DIR)/$(TARGET_EXEC_HARDSIDUSB) $(TARGET_LIB_DIR)
  cp $(BUILD_DIR)/$(TARGET_HEADER_HARDSID) $(TARGET_INCLUDE_DIR)
  cp $(BUILD_DIR)/$(TARGET_HEADER_HARDSIDUSB) $(TARGET_INCLUDE_DIR)
  cp $(BUILD_DIR)/$(TARGET_HEADER_HARDSID) $(TARGET_INCLUDE_DIR_L)/$(TARGET_HEADER_HARDSID_L)
  cp $(BUILD_DIR)/$(TARGET_HEADER_HARDSID) $(TARGET_INCLUDE_DIR_LL)/$(TARGET_HEADER_HARDSID_L)

uninstall:
  rm $(TARGET_LIB_DIR)/$(TARGET_EXEC_HARDSID)
  rm $(TARGET_LIB_DIR)/$(TARGET_EXEC_HARDSIDUSB)
  rm $(TARGET_INCLUDE_DIR)/$(TARGET_HEADER_HARDSID)
  rm $(TARGET_INCLUDE_DIR)/$(TARGET_HEADER_HARDSIDUSB)
  rm $(TARGET_INCLUDE_DIR_L)/$(TARGET_HEADER_HARDSID_L)
  rm $(TARGET_INCLUDE_DIR_LL)/$(TARGET_HEADER_HARDSID_L)

default::
  $(info Creating build directory)
  mkdir -p $(BUILD_DIR)

# gcc -c hardsid.cpp
# gcc -g3 -shared -o libhardsid.so -Wl,-export-dynamic hardsid.o
build_libhardsid: CPPFLAGS += $(CPPLIBS)
build_libhardsid::
  $(info Starting $@)
  $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRCS) -o $(BUILD_DIR)/$(TARGET_EXEC_HARDSID) $(LDFLAGS) $(LDLIBS)
  cp $(SRC_DIRS)/$(HEADER) $(BUILD_DIR)/$(TARGET_HEADER_HARDSID)

build_libhardsid_usb: CPPFLAGS += $(CPPLIBS)
build_libhardsid_usb::
  $(info Starting $@)
  $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRCS) -o $(BUILD_DIR)/$(TARGET_EXEC_HARDSIDUSB) $(LDFLAGS) $(LDLIBS)
  cp $(SRC_DIRS)/$(HEADER) $(BUILD_DIR)/$(TARGET_HEADER_HARDSIDUSB)
