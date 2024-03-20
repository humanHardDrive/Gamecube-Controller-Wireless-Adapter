#include <string.h>
#include <stdio.h>

#include "hardware/pio.h"
#include "pico/time.h"

#include "ControllerComm.h"

#include "gcn_rx.pio.h"
#include "gcn_tx.pio.h"

#include "PinDefs.h"
#include "ControllerDefs.h"
#include "Utils.h"

#define NUM_CONTROLLERS 4

typedef struct
{
    ControllerInfo info;

    PIO TXPIO;
    uint8_t TXSM;

    PIO RXPIO;
    uint8_t RXSM;

}ControllerCommInfo;

static ControllerCommInfo aControllerInfo[NUM_CONTROLLERS];

void ControllerComm_Init()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        aControllerInfo[i].info.isConnected = 0;
        memset(&aControllerInfo[i].info.values, 0, sizeof(ControllerValues));

        aControllerInfo[i].TXPIO = pio0;
        aControllerInfo[i].RXPIO = pio1;
        aControllerInfo[i].TXSM = aControllerInfo[i].RXSM = i;
        aControllerInfo[i].info.isConnected = false;
        aControllerInfo[i].info.LastPollTime = get_absolute_time();
        aControllerInfo[i].info.waitingForResponse = false;
        aControllerInfo[i].info.consecutiveTimeouts = 0;
    }

    //Setup PIO
    //PIO0
    gcn_tx_init(pio0, 0, pio_add_program(pio0, &gcn_tx_program), CONTROLLER_1_DATA_PIN, 250000); //Controller 1
    //gcn_tx_init(pio0, 1, pio_add_program(pio0, &gcn_tx_program), CONTROLLER_2_DATA_PIN, 250000); //Controller 2
    //gcn_tx_init(pio0, 2, pio_add_program(pio1, &gcn_tx_program), CONTROLLER_3_DATA_PIN, 250000); //Controller 3
    //gcn_tx_init(pio0, 3, pio_add_program(pio1, &gcn_tx_program), CONTROLLER_4_DATA_PIN, 250000); //Controller 4
    //Group all the receives together into the same PIO group to share the same clock timing
    //PIO1
    gcn_rx_init(pio1, 0, pio_add_program(pio1, &gcn_rx_program), 2, 250000); //Controller 1
    //gcn_rx_init(pio1, 1, pio_add_program(pio0, &gcn_rx_program), CONTROLLER_2_DATA_PIN, 250000); //Controller 2
    //gcn_rx_init(pio1, 2, pio_add_program(pio1, &gcn_rx_program), CONTROLLER_3_DATA_PIN, 250000); //Controller 3
    //gcn_rx_init(pio1, 3, pio_add_program(pio1, &gcn_rx_program), CONTROLLER_4_DATA_PIN, 250000); //Controller 4    
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
    pController->TXPIO->sm[pController->TXSM].shiftctrl &= ~PIO_SM0_SHIFTCTRL_PULL_THRESH_BITS;
    //Set the auto pull register to the length. This allows an arbitrary number of bits between 1 and 32 to be shifted out
    pController->TXPIO->sm[pController->TXSM].shiftctrl |= tempLength;
    //Put the data in the state machine
    pio_sm_put_blocking(pController->TXPIO, pController->TXSM, val);
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
            while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM))
                pio_sm_get_blocking(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM);
        
            if(!aControllerInfo[i].info.isConnected && (deltaTime > 1200000))
            {
                printf("Controller poll A %d\n", i);
                aControllerInfo[i].info.lastMessage = 0;
                l_ControllerCommWrite(&aControllerInfo[i], aControllerInfo[i].info.lastMessage, 8);

                aControllerInfo[i].info.waitingForResponse = true;
                aControllerInfo[i].info.LastPollTime = get_absolute_time();
            }
            else if(aControllerInfo[i].info.isConnected && (deltaTime > 1200000))
            {
                printf("Controller poll B %d\n", i);
                if(aControllerInfo[i].info.doRumble)
                    aControllerInfo[i].info.lastMessage = POLL_RUMBLE_CMD;
                else
                    aControllerInfo[i].info.lastMessage = POLL_CMD;

                l_ControllerCommWrite(&aControllerInfo[i], aControllerInfo[i].info.lastMessage, 24);
                aControllerInfo[i].info.waitingForResponse = true;
                aControllerInfo[i].info.LastPollTime = get_absolute_time();
            }
        }
        else
        {
            if(deltaTime < 1000) //1mS timeout
            {
                if(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM)) //Check that the buffer has a message
                {
                    uint32_t v = pio_sm_get_blocking(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM);
                    printf("Controller response %d A 0x%x\n", i, v);
                    if(v & 0x01) //Check that the message has the end bit
                    {
                        v >>= 1; //Shift to remove the end bit
                        printf("Controller response %d B 0x%x\n", i, v);
                        if(v != aControllerInfo[i].info.lastMessage) //Validate that this message isn't the one we sent out
                        {
                            //Do something with the data
                            //Depends on message sent?
                            aControllerInfo[i].info.isConnected = true;
                            aControllerInfo[i].info.consecutiveTimeouts = 0;
                            aControllerInfo[i].info.waitingForResponse = false;
                        }
                    }
                }
            }
            else
            {
                printf("Controller timeout %d\n", i);
                aControllerInfo[i].info.consecutiveTimeouts++;
                aControllerInfo[i].info.waitingForResponse = false;

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
        if(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM))
        {
            uint32_t v = pio_sm_get_blocking(aControllerInfo[i].RXPIO, aControllerInfo[i].RXSM);
            if(v & 0x01) //Check that the message has the end bit
            {
                v >>= 1; //Shift to remove the end bit
                if(v != aControllerInfo[i].info.lastMessage) //New message from console
                {
                    //Poll command
                    if((v == POLL_CMD) || (v == POLL_RUMBLE_CMD))
                    {
                        //Send the controller values from wireless
                        memcpy(&aControllerInfo[i].info.lastMessage, &aControllerInfo[i].info.values, sizeof(aControllerInfo[i].info.lastMessage));
                        l_ControllerCommWrite(&aControllerInfo[i], aControllerInfo[i].info.lastMessage, 24);

                        //Set the rumble flag to be sent out to the controller interface
                        if(v == POLL_RUMBLE_CMD)
                            aControllerInfo[i].info.doRumble = true;
                    }
                }
            }
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