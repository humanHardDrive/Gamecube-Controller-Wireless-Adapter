#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

#define CONTROLLER_SIDE_INTERFACE   0
#define CONSOLE_SIDE_INTERFACE      1

void SetInterfaceType(uint8_t function);
uint8_t GetInterfaceType();

#endif