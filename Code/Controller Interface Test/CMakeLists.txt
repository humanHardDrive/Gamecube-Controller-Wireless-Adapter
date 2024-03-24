cmake_minimum_required(VERSION 3.3)

include(pico_sdk_import.cmake)

project(Controller-Interface-Test)

pico_sdk_init()

add_executable(Controller-Interface-Test)

pico_generate_pio_header(Controller-Interface-Test ${CMAKE_CURRENT_LIST_DIR}/gcn_comm.pio)

target_sources(Controller-Interface-Test PRIVATE main.c)

target_link_libraries(Controller-Interface-Test 
						PRIVATE pico_stdlib hardware_pio)

pico_enable_stdio_usb(Controller-Interface-Test  1)
pico_enable_stdio_uart(Controller-Interface-Test  0) 

pico_add_extra_outputs(Controller-Interface-Test)
