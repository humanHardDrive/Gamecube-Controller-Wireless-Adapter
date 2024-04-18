#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
static void controllerWrite(PIO pio, uint sm, uint32_t* pVal, uint8_t len)
 {
    uint8_t nWords = ((len - 1) / 32) + 1;

    uint8_t nOffset = ((nWords * 32) - len);
    uint32_t mask = 0xFFFFFFFF << (32 - nOffset);

    pio_sm_put_blocking(pio, sm, len);
    for(int16_t i = nWords; i > 0; i--)
    {
        uint32_t val = pVal[i - 1];
        val <<= nOffset;

        if(i > 1)
        {
            uint32_t addIn = pVal[i - 2] & mask;
            addIn >>= (32 - nOffset);
            val |= addIn;
        }

        pio_sm_put_blocking(pio, sm, val);
    }
}

static void controllerRead(PIO pio, uint sm, uint32_t* pBuf, uint8_t* pLen)
{
    uint8_t index = 0;
    uint8_t nWords;
    uint8_t nOffset;
    uint32_t mask;

    while(!pio_sm_is_rx_fifo_empty(pio, sm))
    {
        uint32_t v = (pio_sm_get_blocking(pio, sm));
        if(pio_sm_is_rx_fifo_empty(pio, sm))
            *pLen = v;
        else
            pBuf[index++] = v;
    }
    
    nWords = (((*pLen) - 1) / 32) + 1;

    //Remove the idle bit
    pBuf[nWords - 1] >>= 1;
    (*pLen)--;

    nWords = (((*pLen) - 1) / 32) + 1;

    //Swap endianess
    for(uint8_t i = 0; i < (nWords / 2); i++)
    {
        uint32_t swap = pBuf[i];
        pBuf[i] = pBuf[nWords - 1 - i];
        pBuf[nWords - 1 - i] = swap;
    }

    nOffset = ((nWords * 32) - *pLen);
    mask = 0xFFFFFFFF >> (32 - nOffset);

    for(uint8_t i = 0; i < nWords; i++)
    {
        if(i < (nWords - 1))
        {
            uint32_t addIn = pBuf[i + 1] & mask;
            addIn <<= (32 - nOffset);
            pBuf[i] |= addIn;
        }
        else
        {
            pBuf[i] >>= nOffset;
        }
    }
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
    uint64_t out = 0;
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

            pio_sm_clear_fifos(pio0, 0);
            pio_sm_clear_fifos(pio1, 0);

            out++;
            controllerWrite(pio0, 0, (uint32_t*)&out, 64);
            lastPollTime = get_absolute_time();
        }
        else
        {
            if(pio_interrupt_get(pio0, 0))
            {
                while(!pio_sm_is_rx_fifo_empty(pio0, 0))
                    pio_sm_get_blocking(pio0, 0);
                
                pio_interrupt_clear(pio0, 0);
            }

            if(pio_interrupt_get(pio1, 0))
            {
                uint8_t len = 0;
                uint32_t data[4] = {0};

                controllerRead(pio1, 0, data, &len);
                pio_interrupt_clear(pio1, 0);
                controllerWrite(pio1, 0, data, len);
            }
        }
    }

    return 0;
}