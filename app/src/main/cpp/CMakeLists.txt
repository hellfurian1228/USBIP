cmake_minimum_required(VERSION 3.22.1)
project("usbip_server")

add_library(usbip_server SHARED
        usbip_server.cpp)

find_library(log-lib log)

target_link_libraries(usbip_server
        ${log-lib})
