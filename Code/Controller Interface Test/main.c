#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "rx.pio.h"
#include "tx.pio.h"

/*Write data out to the controller through the PIO
val: Max 32-bit value to be written out
len: Number of bits to actually shift out
Appends the 'stop' bit to the end of the write
*/
static inline void controllerWrite(uint32_t val, uint8_t len)
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
    pio0->sm[0].shiftctrl |= tempLength;
    //Put the data in the state machine
    pio_sm_put_blocking(pio0, 0, val);
}

int main()
{
    uint in, out = 0;
    stdio_init_all();

    gpio_init(1);

    tx_program_init(pio0, 0, pio_add_program(pio0, &tx_program), 1, 250000); //4uS per bit = 250kHz
    rx_program_init(pio0, 1, pio_add_program(pio0, &rx_program), 2, 250000);
    while(1)
    {
        sleep_ms(100);
        controllerWrite(out, 16);
        in = rx_program_get(pio0, 1);
        printf("out=0x%x in=0x%x\n", out, (in >> 1));
        out++;
    }

    return 0;
}