#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "PinDefs.h"
#include "Utils.h"

#include "ControllerComm.h"

static void l_ControllerInterfaceLoop()
{
    // if(!ControllerComm_AnyControllerConnected())
    // {
    //     sleep_ms(100);
    // }
}

static void l_ConsoleInterfaceLoop()
{

}

int main()
{
    stdio_init_all();

    printf("Init pins\n");
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

    printf("Init SPI\n");
    //Setup SPI0
    spi_init(spi_default, 10000000); //NRF supports 10MHz
    gpio_set_function(SPI0_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_CLK_PIN, GPIO_FUNC_SPI);

    //Detect device configuration
    if(!gpio_get(FUNC_SEL_PIN))
    {
        /*
        This is the controller side interface
        Poll for controller connection
        Poll for controller values
        Report controller values back to the console side interface
        */
       printf("Controller side interface\n");
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
       printf("Console side interface\n");
       SetInterfaceType(CONSOLE_SIDE_INTERFACE);
    }

    ControllerComm_Init();

    while(1)
    {
        ControllerComm_Background();

        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            l_ControllerInterfaceLoop();
        else if(GetInterfaceType() == CONSOLE_SIDE_INTERFACE)
            l_ConsoleInterfaceLoop();
    }

    return 0;
}