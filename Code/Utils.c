#include "Utils.h"

static uint8_t InterfaceType;

void SetInterfaceType(uint8_t nInterfaceType)
{
    InterfaceType = nInterfaceType;
}

uint8_t GetInterfaceType()
{
    return InterfaceType;
}