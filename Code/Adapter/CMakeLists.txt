cmake_minimum_required(VERSION 3.3)

include(pico_sdk_import.cmake)

project(gcn-wireless-controller)
include_directories(${CMAKE_CURRENT_LIST_DIR}/Headers)

pico_sdk_init()

add_executable(gcn-wireless-controller)

pico_generate_pio_header(gcn-wireless-controller ${CMAKE_CURRENT_LIST_DIR}/gcn_comm.pio)

target_sources(gcn-wireless-controller PRIVATE main.c Utils.c ControllerComm.c)

target_link_libraries(gcn-wireless-controller 
						PRIVATE pico_stdlib hardware_pio hardware_spi)

pico_enable_stdio_usb(gcn-wireless-controller  1)
pico_enable_stdio_uart(gcn-wireless-controller  0) 

pico_add_extra_outputs(gcn-wireless-controller)
