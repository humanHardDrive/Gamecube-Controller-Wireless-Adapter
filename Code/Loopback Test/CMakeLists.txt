cmake_minimum_required(VERSION 3.15)

include(pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../RF24/CMakeLists.txt)

project(loopback-test)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../RF24)

pico_sdk_init()

add_executable(loopback-test)

target_sources(loopback-test PRIVATE main.cpp)

target_link_libraries(loopback-test 
						PRIVATE pico_stdlib pico_mem_ops hardware_spi pico_rand RF24)

pico_enable_stdio_usb(loopback-test  1)
pico_enable_stdio_uart(loopback-test  0) 

pico_add_extra_outputs(loopback-test)
