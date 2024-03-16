#include <stdint.h>

#include "pico/time.h"

const uint32_t POLL_CMD = 0b010000000000001100000010;
const uint32_t POLL_RUMBLE_CMD = POLL_CMD | 1;

typedef struct
{
    //Buttons
    uint8_t pad0 : 3;
    uint8_t StartBtn : 1;
    uint8_t YBtn : 1;
    uint8_t XBtn : 1;
    uint8_t BBtn : 1;
    uint8_t ABtn : 1;
    uint8_t pad1 : 1;
    uint8_t LBtn : 1;
    uint8_t RBtn : 1;
    uint8_t ZBtn : 1;
    uint8_t UpBtn : 1;
    uint8_t DownBtn : 1;
    uint8_t RightBtn : 1;
    uint8_t LeftBtn : 1;

    //Joysticks
    uint8_t JoyX;
    uint8_t JoyY;
    uint8_t CX;
    uint8_t CY;
    
    //Analog triggers
    uint8_t LVal;
    uint8_t RVal;
}ControllerValues;

typedef struct
{
    ControllerValues values;
    uint8_t isConnected;
    uint8_t doRumble;

    absolute_time_t LastPollTime;
}ControllerInfo;