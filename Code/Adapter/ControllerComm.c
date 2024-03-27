#include <string.h>
#include <stdio.h>

#include "hardware/pio.h"
#include "pico/time.h"

#include "ControllerComm.h"

#include "gcn_comm.pio.h"

#include "PinDefs.h"
#include "ControllerDefs.h"
#include "Utils.h"

#define NUM_CONTROLLERS 4

typedef struct
{
    ControllerInfo info;

    PIO pio;
    uint sm;
    uint offset;
    uint pin;
}ControllerCommInfo;

static ControllerCommInfo aControllerInfo[NUM_CONTROLLERS];

static void l_ControllerSwitchModeTX(ControllerCommInfo* pController)
{
    pio_sm_exec_wait_blocking(pController->pio, pController->sm, gcn_comm_switch_tx(pController->offset));
}

static void l_ControllerSwitchModeRX(ControllerCommInfo* pController)
{
    pio_sm_exec_wait_blocking(pController->pio, pController->sm, gcn_comm_switch_rx(pController->offset));
}

void ControllerComm_Init()
{
    uint offset = pio_add_program(pio0, &gcn_comm_program);

    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        aControllerInfo[i].info.isConnected = 0;
        memset(&aControllerInfo[i].info.values, 0, sizeof(ControllerValues));

        aControllerInfo[i].pio = pio0;
        aControllerInfo[i].sm = i;
        aControllerInfo[i].offset = offset;
        aControllerInfo[i].pin = CONTROLLER_DATA_BASE_PIN + i;

        aControllerInfo[i].info.isConnected = false;
        aControllerInfo[i].info.LastPollTime = get_absolute_time();
        aControllerInfo[i].info.waitingForResponse = false;
        aControllerInfo[i].info.consecutiveTimeouts = 0;

        gpio_init(aControllerInfo[i].pin);

        gcn_comm_program_init(aControllerInfo[i].pio, 
                              aControllerInfo[i].sm, 
                              aControllerInfo[i].offset,
                              aControllerInfo[i].pin,
                              250000);
        
        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            l_ControllerSwitchModeTX(&aControllerInfo[i]);
        else
            l_ControllerSwitchModeRX(&aControllerInfo[i]);
    }
}

/*Write data out to the controller through the PIO
val: Max 32-bit value to be written out
len: Number of bits to actually shift out
Appends the 'stop' bit to the end of the write
*/
static void l_ControllerCommWrite(ControllerCommInfo* pController, uint32_t val, uint8_t len)
{
    uint32_t tempLength = len;

    //Nothing more than 31 bits can be written as the OSR is 32 bits
    //32 bits can't be written because there needs to be room for the appended stop
    if(len >= 32)
        return;

    //Increment the length to account for the 'stop' bit
    tempLength++;

    val <<= 1; //Shift the input value over to account for the stop bit
    val++; //Add the stop bit
    val <<= (32 - tempLength); //Shift the value to the left because the OSR is configured to shift out to the left

    //If writing 32 bits the auto pull needs to be set to 0
    //See data sheet
    if(tempLength == 32)
        tempLength = 0;

    //Shift the length over to align to the PULL_THRESH of the SHIFTCTRL register
    tempLength <<= PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB;
    //Clear out the old length
    pController->pio->sm[pController->sm].shiftctrl &= ~PIO_SM0_SHIFTCTRL_PULL_THRESH_BITS;
    //Set the auto pull register to the length. This allows an arbitrary number of bits between 1 and 32 to be shifted out
    pController->pio->sm[pController->sm].shiftctrl |= tempLength;
    //Put the data in the state machine
    pio_sm_put_blocking(pController->pio, pController->sm, val);
}

static void l_ControllerInterfaceBackground()
{
    for(uint8_t i = 0; i < 1; i++)
    {
        bool bDoPoll = false;
        uint deltaTime = absolute_time_diff_us(aControllerInfo[i].info.LastPollTime, get_absolute_time());
        
        //If we're not waiting for a message back, ensure that the response buffer is clear
        if(!aControllerInfo[i].info.waitingForResponse)
        {
            while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
                pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);

            if(!aControllerInfo[i].info.isConnected && (deltaTime > 1200000))
            {
                printf("Controller poll A %d\n", i);
                l_ControllerCommWrite(&aControllerInfo[i], 0, 8);

                aControllerInfo[i].info.waitingForResponse = true;
                aControllerInfo[i].info.LastPollTime = get_absolute_time();
            }
            else if(aControllerInfo[i].info.isConnected && (deltaTime > 1200000))
            {
                printf("Controller poll B %d\n", i);
                if(aControllerInfo[i].info.doRumble)
                    l_ControllerCommWrite(&aControllerInfo[i], POLL_RUMBLE_CMD, 24);
                else
                    l_ControllerCommWrite(&aControllerInfo[i], POLL_CMD, 24);

                aControllerInfo[i].info.waitingForResponse = true;
                aControllerInfo[i].info.LastPollTime = get_absolute_time();
            }
        }
        else
        {
            if(deltaTime < 1000) //1mS timeout
            {
                if(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm)) //Check that the buffer has a message
                {
                    uint32_t v = pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);
                    printf("Controller response %d A 0x%x\n", i, v);
                    if(v & 0x01) //Check that the message has the end bit
                    {
                        v >>= 1; //Shift to remove the end bit
                        printf("Controller response %d B 0x%x\n", i, v);
                        aControllerInfo[i].info.waitingForResponse = false;
                    }
                }
            }
            else
            {
                printf("Controller timeout %d\n", i);
                aControllerInfo[i].info.consecutiveTimeouts++;
                aControllerInfo[i].info.waitingForResponse = false;
                l_ControllerSwitchModeTX(&aControllerInfo[i]);

                if(aControllerInfo[i].info.isConnected && aControllerInfo[i].info.consecutiveTimeouts > 2)
                {
                    printf("Controller disconnect %d\n", i);
                    aControllerInfo[i].info.isConnected = false;
                }
            }
        }
    }
}

static void l_ConsoleInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        if(aControllerInfo[i].info.isConnected)
        {

        }
    }
}

void ControllerComm_Background()
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        l_ControllerInterfaceBackground();
    else
        l_ConsoleInterfaceBackground();
}

unsigned char ControllerComm_AnyControllerConnected()
{
    return 0;
}