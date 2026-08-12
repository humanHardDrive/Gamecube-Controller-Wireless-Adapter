cmake_minimum_required(VERSION 3.15)

include(pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../RF24/CMakeLists.txt)

project(gcn-wireless-controller C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_LIST_DIR}/Headers)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../RF24)

#add_compile_options(-Og)

pico_sdk_init()

add_definitions(-DSYS_CLK_MHZ=200)

add_executable(gcn-wireless-controller)

pico_generate_pio_header(gcn-wireless-controller ${CMAKE_CURRENT_LIST_DIR}/gcn_comm.pio)

target_include_directories(gcn-wireless-controller PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_sources(gcn-wireless-controller PRIVATE main.cpp usb_descriptors.cpp Utils.cpp PowerManager.cpp ControllerComm.cpp WirelessComm.cpp
	USBComm.cpp)

target_link_libraries(gcn-wireless-controller 
						PRIVATE pico_stdlib pico_mem_ops pico_multicore hardware_pio hardware_spi tinyusb_device tinyusb_board RF24)

pico_enable_stdio_usb(gcn-wireless-controller  0)
pico_enable_stdio_uart(gcn-wireless-controller  1)

pico_add_extra_outputs(gcn-wireless-controller)
