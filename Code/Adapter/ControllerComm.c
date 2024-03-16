#include <string.h>
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
        aControllerInfo[i].info.LastPollTime = get_absolute_time();
    }

    //Setup PIO
    //Group all the transmits together into the same PIO group to share the same clock timing
    //PIO0
    gcn_tx_init(pio0, 0, pio_add_program(pio0, &gcn_tx_program), DATA1_TX_PIN, 250000); //Controller 1
    //gcn_tx_init(pio0, 1, pio_add_program(pio0, &gcn_tx_program), DATA2_TX_PIN, 250000); //Controller 2
    //gcn_tx_init(pio0, 2, pio_add_program(pio1, &gcn_tx_program), DATA3_TX_PIN, 250000); //Controller 3
    //gcn_tx_init(pio0, 3, pio_add_program(pio1, &gcn_tx_program), DATA4_TX_PIN, 250000); //Controller 4
    //Group all the receives together into the same PIO group to share the same clock timing
    //PIO1
    //gcn_rx_init(pio1, 0, pio_add_program(pio0, &gcn_rx_program), DATA1_RX_PIN, 250000); //Controller 1
    //gcn_rx_init(pio1, 1, pio_add_program(pio0, &gcn_rx_program), DATA2_RX_PIN, 250000); //Controller 2
    //gcn_rx_init(pio1, 2, pio_add_program(pio1, &gcn_rx_program), DATA3_RX_PIN, 250000); //Controller 3
    //gcn_rx_init(pio1, 3, pio_add_program(pio1, &gcn_rx_program), DATA4_RX_PIN, 250000); //Controller 4    
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
    //Set the auto pull register to the length. This allows an arbitrary number of bits between 1 and 32 to be shifted out
    pController->TXPIO->sm[pController->TXSM].shiftctrl |= tempLength;
    //Put the data in the state machine
    pio_sm_put_blocking(pController->TXPIO, pController->TXSM, val);
}

static void l_ControllerInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        uint deltaTime = absolute_time_diff_us(aControllerInfo[i].info.LastPollTime, get_absolute_time());
        if(!aControllerInfo[i].info.isConnected && (deltaTime > 12000))
        {
            l_ControllerCommWrite(&aControllerInfo[i], 0, 8);
            aControllerInfo[i].info.LastPollTime = get_absolute_time();
        }
        else if(aControllerInfo[i].info.isConnected && (deltaTime > 1500))
        {
            if(aControllerInfo[i].info.doRumble)
                l_ControllerCommWrite(&aControllerInfo[i], POLL_RUMBLE_CMD, 24);
            else
                l_ControllerCommWrite(&aControllerInfo[i], POLL_CMD, 24);

            aControllerInfo[i].info.LastPollTime = get_absolute_time();
        }
    }
}

static void l_ConsoleInterfaceBackground()
{

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