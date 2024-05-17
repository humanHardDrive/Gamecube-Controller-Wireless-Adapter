#include <stdint.h>

#include "pico/time.h"

const uint32_t POLL_CMD = 0b010000000000001100000010;
const uint32_t POLL_RUMBLE_CMD = 0b010000000000001100000011;

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
    int8_t JoyX;
    int8_t JoyY;
    int8_t CX;
    int8_t CY;
    
    //Analog triggers
    uint8_t LVal;
    uint8_t RVal;
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