#pragma once

#include <stdint.h>

#include "hardware/pio.h"

#include "ControllerDefs.h"

#define NUM_CONTROLLERS 4

class ControllerComm
{
public:
    ControllerComm();

    void Init();
    void Background();

    void GetControllerData(ControllerValues* pControllerData);
    void SetControllerData(ControllerValues* pControllerData);

    void GetConsoleData(ConsoleValues* pConsoleData);
    void SetConsoleData(ConsoleValues* pConsoleData);

    unsigned char AnyControllerConnected();

private:
    enum class ControllerState
    {
        NOT_CONNECTED = 0,
        STARTUP_CONFIG,
        POLLING
    };

    typedef struct
    {
        ControllerInfo info;

        PIO pio;
        uint sm;
        uint offset;
        uint pin;

        ControllerState state;
        uint8_t waitingForResponse;
        uint8_t consecutiveTimeouts;
        uint32_t LastCmd;
        absolute_time_t LastPollTime;

    }ControllerCommInfo;

    void SwitchModeTX(ControllerCommInfo* pController);
    void SwitchModeRX(ControllerCommInfo* pController);

    void Write(ControllerCommInfo* pController, uint32_t* pVal, uint8_t len);
    void Read(ControllerCommInfo* pController, uint32_t* pBuf, uint8_t* pLen);

    void ConsoleInterfaceBackground();
    void ControllerInterfaceBackground();

    ControllerCommInfo m_aControllerInfo[NUM_CONTROLLERS];
};