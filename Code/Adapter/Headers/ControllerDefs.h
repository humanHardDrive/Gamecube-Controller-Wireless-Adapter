#include <stdint.h>

#include "pico/time.h"

const uint32_t POLL_CMD = 0b010000000000001100000010;
const uint32_t POLL_RUMBLE_CMD = 0b010000000000001100000011;

typedef struct
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
    uint8_t pad : 1;
    uint8_t ABtn : 1;
    uint8_t BBtn : 1;
    uint8_t XBtn : 1;
    uint8_t YBtn : 1;
    uint8_t StartBtn : 1;
}ControllerValues;

typedef struct
{
    ControllerValues values;
    
    uint8_t isConnected;
    uint8_t doRumble;
    uint8_t waitingForResponse;

    uint8_t consecutiveTimeouts;

    uint32_t LastCmd;
    absolute_time_t LastPollTime;
}ControllerInfo;