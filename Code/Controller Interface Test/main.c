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
*/
static void controllerWrite(PIO pio, uint sm, uint32_t* pVal, uint8_t len)
 {
    uint8_t nWords = ((len - 1) / 32) + 1;
    uint8_t nOffset = ((nWords * 32) - len);
    uint32_t mask = 0xFFFFFFFF << (32 - nOffset);

    pio_sm_put_blocking(pio, sm, len); //Put the number of bits onto the FIFO
    //Shift ou the data
    for(int16_t i = nWords; i > 0; i--)
    {
        uint32_t val = pVal[i - 1];
        val <<= nOffset; //Left shift to be at the end of the OSR

        //Check if data from the previous word needs to be appended onto the one being sent out
        //Only matters for every word other than the least significant
        if(i > 1)
        {
            uint32_t addIn = pVal[i - 2] & mask; //Mask off the needed bits
            addIn >>= (32 - nOffset); //Shift it appropriately to align with the current word
            val |= addIn; //Add it in
        }

        //Send out the constructed word to the FIFO
        pio_sm_put_blocking(pio, sm, val);
    }
}

/*Read data from the controller through the PIO
pBuf: Buffer of words that the data is written into
pLen: Pointer number of bits read
*/
static void controllerRead(PIO pio, uint sm, uint32_t* pBuf, uint8_t* pLen)
{
    uint8_t index = 0;
    uint8_t nWords;
    uint8_t nOffset;
    uint32_t mask;

    //Get the data from the PIO
    while(!pio_sm_is_rx_fifo_empty(pio, sm))
    {
        uint32_t v = (pio_sm_get_blocking(pio, sm));
        if(pio_sm_is_rx_fifo_empty(pio, sm)) //The last word is always the length
            *pLen = v;
        else
            pBuf[index++] = v; //All other words are the actual data
    }
    
    //Calculate the number of words the data would take
    nWords = (((*pLen) - 1) / 32) + 1;

    //Remove the idle bit
    pBuf[nWords - 1] >>= 1;
    //Decrement the length
    (*pLen)--;

    //Recalculate the number of words without the stop bit
    nWords = (((*pLen) - 1) / 32) + 1;

    //Swap endianess
    for(uint8_t i = 0; i < (nWords / 2); i++)
    {
        uint32_t swap = pBuf[i];
        pBuf[i] = pBuf[nWords - 1 - i];
        pBuf[nWords - 1 - i] = swap;
    }

    //This is essentially the inverse of the write operation
    nOffset = ((nWords * 32) - *pLen);
    mask = 0xFFFFFFFF >> (32 - nOffset);

    for(uint8_t i = 0; i < nWords; i++)
    {
        if((i > 0) && (nWords > 1))
            pBuf[i] >>= nOffset;

        if(i < (nWords - 1))
        {
            uint32_t addIn = pBuf[i + 1] & mask;
            addIn <<= (32 - nOffset);
            pBuf[i] |= addIn;
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
    uint32_t out[4] = {0};
    uint8_t nTestLen = 80;
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
        if(deltaTime > 10000)
        {
            controllerSwitchModeTX(pio0, 0, consoleOffset);
            controllerSwitchModeRX(pio1, 0, controllerOffset);

            pio_sm_clear_fifos(pio0, 0);
            pio_sm_clear_fifos(pio1, 0);

            out[0] <<= 1;
            if(!out[0])
            {
                out[0] = 1;
                out[1] <<= 1;
                printf("HB\n");

                if(!out[1])
                {
                    out[1] = 1;
                    out[2] <<= 1;
                    if(!out[2])
                    {
                        out[2] = 1;
                        out[3] <<= 1;
                        if(!out[3])
                        {
                            out[3] = 1;
                        }
                    }
                }
            }

            controllerWrite(pio0, 0, (uint32_t*)&out, nTestLen);
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

                if(len != nTestLen)
                {
                    printf("Message len doesn't match. %d vs. expected %d\n", len, nTestLen);
                }
                else
                {
                    uint8_t nWords = ((len - 1) / 32) + 1;
                    uint8_t nBytes = ((len - 1) / 8) + 1;
                    uint8_t nOffset = ((nWords * 32) - len);
                    uint32_t mask = 0xFFFFFFFF << (32 - nOffset);
                    mask = ~mask;

                    uint32_t test[4] = {0};
                    memcpy(test, out, sizeof(test));
                    test[nWords - 1] &= mask;

                    if(memcmp(test, data, nBytes))
                    {
                        printf("Message data doesn't match ");
                        for(size_t i = 0; i < nWords; i++)
                            printf("%u ", data[i]);
                        
                        printf("vs ");

                        for(size_t i = 0; i < nWords; i++)
                            printf("%u ", test[i]);
                        
                        printf("\n");
                    }
                }
            }
        }
    }

    return 0;
}