cmake_minimum_required(VERSION 3.3)

include(pico_sdk_import.cmake)

project(gcn-wireless-controller)

pico_sdk_init()

add_executable(gcn-wireless-controller
	main.c
)

target_link_libraries(gcn-wireless-controller pico_stdlib)

pico_add_extra_outputs(gcn-wireless-controller)
