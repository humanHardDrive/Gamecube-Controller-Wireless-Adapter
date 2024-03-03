#include <stdint.h>

#include "pico/stdlib.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/spi.h"

#include "PinDefs.h"
#include "Utils.h"
#include "gcn_rx.pio.h"
#include "gcn_tx.pio.h"

#include "ControllerComm.h"

int main()
{
    //Setup pins
    gpio_init(PAIR_PIN);
    gpio_init(FUNC_SEL_PIN);

    gpio_init(NRF_CS_PIN);
    gpio_init(NRF_CE_PIN);
    gpio_init(NRF_IRQ_PIN);

    gpio_set_dir(PAIR_PIN, GPIO_IN);
    gpio_set_dir(FUNC_SEL_PIN, GPIO_IN);

    gpio_pull_up(PAIR_PIN);
    gpio_pull_up(FUNC_SEL_PIN);

    //Setup SPI0
    spi_init(spi_default, 10000000); //NRF supports 10MHz
    gpio_set_function(SPI0_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_CLK_PIN, GPIO_FUNC_SPI);

    //Setup PIO
    //Group all the transmits together into the same PIO group to share the same clock timing
    //PIO0
    gcn_tx_init(pio0, 0, pio_add_program(pio0, &gcn_tx_program), DATA1_TX_PIN); //Controller 1
    gcn_tx_init(pio0, 1, pio_add_program(pio0, &gcn_tx_program), DATA2_TX_PIN); //Controller 2
    gcn_tx_init(pio0, 2, pio_add_program(pio1, &gcn_tx_program), DATA3_TX_PIN); //Controller 3
    gcn_tx_init(pio0, 3, pio_add_program(pio1, &gcn_tx_program), DATA4_TX_PIN); //Controller 4
    //Group all the receives together into the same PIO group to share the same clock timing
    //PIO1
    gcn_rx_init(pio1, 0, pio_add_program(pio0, &gcn_rx_program), DATA1_RX_PIN); //Controller 1
    gcn_rx_init(pio1, 1, pio_add_program(pio0, &gcn_rx_program), DATA2_RX_PIN); //Controller 2
    gcn_rx_init(pio1, 2, pio_add_program(pio1, &gcn_rx_program), DATA3_RX_PIN); //Controller 3
    gcn_rx_init(pio1, 3, pio_add_program(pio1, &gcn_rx_program), DATA4_RX_PIN); //Controller 4

    //Detect device configuration
    if(!gpio_get(FUNC_SEL_PIN))
    {
        /*
        This is the controller side interface
        Poll for controller connection
        Poll for controller values
        Report controller values back to the console side interface
        */
       SetInterfaceType(CONTROLLER_SIDE_INTERFACE);
    }
    else
    {
        /*
        This is the console side interface
        Handle polling requests from the console
        Feed console data from controller side interface
        Communicate rumbe back to controller side interface
        */
       SetInterfaceType(CONSOLE_SIDE_INTERFACE);
    }

    ControllerComm_Init();

    while(1)
    {
        ControllerComm_Background();
    }

    return 0;
}