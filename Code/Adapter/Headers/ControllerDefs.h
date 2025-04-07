#include <stdint.h>
#include <utility>

#include "pico/time.h"

constexpr std::pair<uint32_t, uint8_t> POLL_MSG = {0b010000000000001100000000, 24};
constexpr std::pair<uint32_t, uint8_t> POLL_RUMBLE_MSG = {0b010000000000001100000001, 24};
constexpr std::pair<uint32_t, uint8_t> PROBE_MSG = {0x00, 8};
constexpr std::pair<uint32_t, uint8_t> PROBE_RECAL_MSG = {0xFF, 8};
constexpr std::pair<uint32_t, uint8_t> PROBE_ORIGIN_MSG = {0x41, 8};
constexpr std::pair<uint32_t, uint8_t> PROBE_ORIGIN_RECAL_MSG = {0x42, 8};

constexpr std::pair<uint32_t, uint8_t> PROBE_RSP = {0x090003, 24};
constexpr std::pair<uint32_t, uint8_t> PROBE_ORIGIN_RSP = {0, 80};
constexpr std::pair<uint32_t, uint8_t> POLL_RSP = {0, 64};

typedef struct
{
    union
    {
        struct
        {
            //Analog triggers
            uint8_t RVal;
            uint8_t LVal;

            //Joysticks
            uint8_t CY;
            uint8_t CX;
            uint8_t JoyY;
            uint8_t JoyX;

            //D-Pad
            uint8_t DLeft : 1;
            uint8_t DRight : 1;
            uint8_t DDown : 1;
            uint8_t DUp : 1;

            //Buttons
            uint8_t ZBtn : 1;
            uint8_t RBtn : 1;
            uint8_t LBtn : 1;
            uint8_t pad : 1;    //Always 1
            uint8_t ABtn : 1;
            uint8_t BBtn : 1;
            uint8_t XBtn : 1;
            uint8_t YBtn : 1;
            uint8_t StartBtn : 1;    

            uint8_t pad2 : 3; //Always 0
        };
        uint64_t raw;
    };
}ControllerValues;

typedef struct
{
    uint8_t doRumble;
    uint8_t doRecal;
}ConsoleValues;

typedef struct
{
    uint8_t isConnected;
    bool bIsStale;

    ControllerValues controllerValues;
    ConsoleValues consoleValues;
}ControllerInfo;