#pragma once

#include <hardware/spi.h>

#include "ControllerComm.h"

typedef struct
{
    //Device control pins
    uint functionSelect;

    //Controller comm pins
    uint controllerData[NUM_CONTROLLERS];
    uint controllerStatus[NUM_CONTROLLERS];

    //Wireless comm pins
    spi_inst_t* wirelessSPI;
    uint wirelessMOSI;
    uint wirelessMISO;
    uint wirelessSCK;
    uint wirelessCS;
    uint wirelessCE;
    uint wirelessPair;
    uint wirelessTX;
    uint wirelessRX;

    //Power control pins
    uint powerOn;
    uint powerLED;
    uint vbusDetect;
}DevicePinMap;