#pragma once

#include <stdint.h>

#include "hardware/pio.h"
#include "pico/mutex.h"

#include "ControllerDefs.h"
#include "ModuleTemplate.h"

#define NUM_CONTROLLERS 4

class ControllerComm : public ModuleTemplate
{
public:
    ControllerComm();

    bool Init();
    void Background();

    void GetControllerData(ControllerValues* pControllerData);
    void SetControllerData(ControllerValues* pControllerData);

    void GetConsoleData(ConsoleValues* pConsoleData);
    void SetConsoleData(ConsoleValues* pConsoleData);

    bool AnyControllerConnected();

    bool IsControllerInfoStale();

    void Sleep();
    void Wake();

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
        uint commPin;
        uint connectPin;

        ControllerState state;
        uint8_t waitingForResponse;
        uint8_t consecutiveTimeouts;
        uint32_t LastCmd;
        absolute_time_t LastPollTime;

    }ControllerCommInfo;

    void StartupSequence();
    void MainBackground();

    void SwitchModeTX(ControllerCommInfo* pController);
    void SwitchModeRX(ControllerCommInfo* pController);

    void Write(ControllerCommInfo* pController, uint32_t* pVal, uint8_t len);
    void Read(ControllerCommInfo* pController, uint32_t* pBuf, uint8_t* pLen);

    void ConsoleInterfaceBackground(bool bStopComm, bool* bCanSleep);
    void ControllerInterfaceBackground(bool bStopComm, bool* bCanSleep);

    ControllerCommInfo m_aControllerInfo[NUM_CONTROLLERS];
    bool m_bSleepRequested, m_bCanSleep;

    bool m_bStartupDone;
    absolute_time_t m_StartupTime, m_StartupPhaseTime;
    uint m_StartupCount;
    uint m_nStaleCount;

    mutex_t m_BackgroundLock;
};