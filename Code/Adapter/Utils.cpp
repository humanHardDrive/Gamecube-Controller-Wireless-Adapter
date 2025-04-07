#include "Utils.h"

static int8_t InterfaceType = UNKNOWN_INTERFACE;

static const DevicePinMap UnknownInterfacePinMap = {.functionSelect = 25}; //Only need the function select pin defined

static const DevicePinMap ControllerInterfacePinMap =
{
    .functionSelect = 25,

    .controllerData = {1, 28, 10, 19},
    .controllerStatus = {0, 29, 11, 18},

    .wirelessSPI = spi1,
    .wirelessMOSI = 15,
    .wirelessMISO = 12,
    .wirelessSCK = 14,
    .wirelessCS = 13,
    .wirelessCE = 16,
    .wirelessPair = 2,
    .wirelessTX = 4,
    .wirelessRX = 5,

    .powerOn = 26,
    .powerLED = 3,
    .vbusDetect = 5,
};

static const DevicePinMap ConsoleInterfacePinMap = 
{
    .functionSelect = 25,

    .controllerData = {22/*29*/, 28, 27, 26},
    .controllerStatus = {21, 20, 19, 18},

    .wirelessSPI = spi1,
    .wirelessMOSI = 11,
    .wirelessMISO = 8,
    .wirelessSCK = 14,
    .wirelessCS = 13,
    .wirelessCE = 12,
    .wirelessPair = 15,
    .wirelessTX = 17,
    .wirelessRX = 16,    

    .powerOn = 255,
    .powerLED = 255,
    .vbusDetect = 255,
};

//Default the device pin map to the unknown interface
static const DevicePinMap* pPinMap = &UnknownInterfacePinMap;

void SetInterfaceType(uint8_t nInterfaceType)
{
    switch(nInterfaceType)
    {
        case CONTROLLER_SIDE_INTERFACE:
            InterfaceType = CONTROLLER_SIDE_INTERFACE;
            pPinMap = &ControllerInterfacePinMap;
            break;

        case CONSOLE_SIDE_INTERFACE:
            InterfaceType = CONSOLE_SIDE_INTERFACE;
            pPinMap = &ConsoleInterfacePinMap;
            break;

        default:
            InterfaceType = UNKNOWN_INTERFACE;
            pPinMap = &UnknownInterfacePinMap;
            break;
    }
}

uint8_t GetInterfaceType()
{
    return InterfaceType;
}

const DevicePinMap* GetDevicePinMap()
{
    return pPinMap;
}