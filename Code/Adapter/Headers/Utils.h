#pragma once

#include <stdint.h>
#include "PinDefs.h"

#define UNKNOWN_INTERFACE           -1
#define CONTROLLER_SIDE_INTERFACE   0
#define CONSOLE_SIDE_INTERFACE      1

void SetInterfaceType(uint8_t function);
uint8_t GetInterfaceType();

const DevicePinMap* GetDevicePinMap();