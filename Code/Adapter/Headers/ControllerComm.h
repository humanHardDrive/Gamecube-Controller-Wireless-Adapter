#pragma once

#include <stdint.h>

#include "hardware/pio.h"

#include "ControllerDefs.h"

#define NUM_CONTROLLERS 4

typedef struct
{
    ControllerInfo info;

    PIO pio;
    uint sm;
    uint offset;
    uint pin;
}ControllerCommInfo;

class ControllerComm
{
public:
    ControllerComm();

    void Init();
    void Background();

    void GetControllerData(void* pBuf);

    unsigned char AnyControllerConnected();

private:
    void SwitchModeTX(ControllerCommInfo* pController);
    void SwitchModeRX(ControllerCommInfo* pController);

    void Write(ControllerCommInfo* pController, uint32_t* pVal, uint8_t len);
    void Read(ControllerCommInfo* pController, uint32_t* pBuf, uint8_t* pLen);

    void ConsoleInterfaceBackground();
    void ControllerInterfaceBackground();

    ControllerCommInfo m_aControllerInfo[NUM_CONTROLLERS];
};