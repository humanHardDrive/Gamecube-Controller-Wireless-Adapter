#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "gcn_comm.pio.h"

/*Write data out to the controller through the PIO
val: Max 32-bit value to be written out
len: Number of bits to actually shift out
Appends the 'stop' bit to the end of the write
*/
static void controllerWrite(PIO pio, uint sm, uint32_t val, uint8_t len)
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
    //Set the auto pull register to the length. This allows an arbitrary number of bits between 1 and 32 to be shifted out
    pio->sm[0].shiftctrl = (pio->sm[0].shiftctrl & ~(PIO_SM0_SHIFTCTRL_PULL_THRESH_BITS)) | ((tempLength & 0x1fu) << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB);
    //Put the data in the state machine
    pio_sm_put_blocking(pio, sm, val);
}

void controllerSwitchModeTX(PIO pio, uint sm, uint offset)
{
    pio_sm_exec_wait_blocking (pio, sm, gcn_switch_tx(offset));
}

void controllerSwitchModeRX(PIO pio, uint sm, uint offset)
{
    pio_sm_exec_wait_blocking (pio, sm, gcn_switch_rx(offset));
}

int main()
{
    uint out = 0;
    uint consoleOffset = 0, controllerOffset = 0;
    absolute_time_t lastPollTime = get_absolute_time();
    stdio_init_all();

    gpio_init(1);
    gpio_init(2);

    consoleOffset = pio_add_program(pio0, &gcn_comm_program);
    controllerOffset = pio_add_program(pio1, &gcn_comm_program);
    gcn_comm_program_init(pio0, 0, consoleOffset, 1, 250000); //4uS per bit = 250kHz
    gcn_comm_program_init(pio1, 0, controllerOffset, 2, 250000);
    while(1)
    {
        uint deltaTime = absolute_time_diff_us(lastPollTime, get_absolute_time());
        if(deltaTime > 1000000)
        {
            controllerSwitchModeTX(pio0, 0, consoleOffset);
            controllerSwitchModeRX(pio1, 0, controllerOffset);

            while(!pio_sm_is_rx_fifo_empty(pio0, 0))
                pio_sm_get_blocking(pio0, 0);

            while(!pio_sm_is_rx_fifo_empty(pio1, 0))
                pio_sm_get_blocking(pio1, 0);

            printf("Write 0x%x\n", out);
            controllerWrite(pio0, 0, out++, 8);
            lastPollTime = get_absolute_time();
        }
        else
        {
            if(!pio_sm_is_rx_fifo_empty(pio0, 0))
            {
                uint32_t v = (pio_sm_get_blocking(pio0, 0) >> 1);
            }

            if(!pio_sm_is_rx_fifo_empty(pio1, 0))
            {
                uint32_t v = (pio_sm_get_blocking(pio1, 0) >> 1);
                controllerWrite(pio1, 0, v, 8);
            }
        }
    }

    return 0;
}