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

static absolute_time_t LastPollTime;

void ControllerComm_Init()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        aControllerInfo[i].info.isConnected = 0;
        memset(&aControllerInfo[i].info.values, 0, sizeof(ControllerValues));

        aControllerInfo[i].TXPIO = pio0;
        aControllerInfo[i].RXPIO = pio1;
        aControllerInfo[i].TXSM = aControllerInfo[i].RXSM = i;
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

    LastPollTime = get_absolute_time();
}

static void l_ControllerCommWrite(ControllerCommInfo* pController, uint32_t val, uint8_t len)
{
    uint32_t tempLength = len;

    if(!pController)
        return;

    if(len >= 32)
        return;

    //Set the auto pull to be the length of data to write + 1
    tempLength++;
    if(tempLength == 32)
        tempLength = 0;

    //Shift the length over to align to the PULL_THRESH of the SHIFTCTRL register
    tempLength <<= PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB;
    pController->TXPIO->sm[pController->TXSM].shiftctrl |= tempLength;

    pio_sm_put_blocking(pController->TXPIO, pController->TXSM, val);
}

static void l_ControllerInterfaceBackground()
{
    absolute_time_t currentTime = get_absolute_time();

    if(absolute_time_diff_us(LastPollTime, currentTime) > 12000)
    {
        for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
        {
            l_ControllerCommWrite(&aControllerInfo[i], 0, 8);
        }
        LastPollTime = currentTime;
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