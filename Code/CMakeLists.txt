cmake_minimum_required(VERSION 3.3)

include(pico_sdk_import.cmake)

project(gcn-wireless-controller)
include_directories(${CMAKE_CURRENT_LIST_DIR}/Headers)

pico_sdk_init()

add_executable(gcn-wireless-controller)

pico_generate_pio_header(gcn-wireless-controller ${CMAKE_CURRENT_LIST_DIR}/gcn_rx.pio)
pico_generate_pio_header(gcn-wireless-controller ${CMAKE_CURRENT_LIST_DIR}/gcn_tx.pio)

target_sources(gcn-wireless-controller PRIVATE main.c Utils.c ControllerComm.c)

target_link_libraries(gcn-wireless-controller 
						PRIVATE pico_stdlib hardware_pio hardware_spi)

pico_add_extra_outputs(gcn-wireless-controller)
