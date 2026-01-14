cmake_minimum_required(VERSION 3.17)
project(config-tool C)

add_executable(config-tool cfg_usbsid.c inih/ini.c)
target_include_directories(config-tool PRIVATE /usr/include/libusb-1.0)
target_link_libraries(config-tool PUBLIC usb-1.0)
