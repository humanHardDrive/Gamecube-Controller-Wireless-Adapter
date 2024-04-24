#include <stdint.h>
#include <stdio.h>
#include <tusb.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/gpio.h"

#include "PinDefs.h"
#include "Utils.h"

#include "ControllerComm.h"
#include "WirelessComm.h"

//Communication objects
ControllerComm controllerComm;
WirelessComm wirelessComm;

#define CONTROLLER_COMM_OWNS_DATA   0
#define WIRELESS_COMM_OWNS_DATA     1

//Controller data signal
uint8_t nControllerDataOwner = CONTROLLER_COMM_OWNS_DATA;
//Controller data buffer
//This buffer is ping-ponged between the controller and console interfaces using the switch above
//Each side takes turns updating, then consuming the data (controller produces, wireless consumes)
//The switch lets either side know when it is safe to do so
//The owner of the data is responsible for switching back when they're done

/*
Function for the controller communication core
Communication between the controller/console is handled on a seperate processing core
to allow the main core to do wireless/USB communication
*/
void ControllerCommunicationCore()
{
    //Signal back that the core has started
    multicore_fifo_push_blocking(0);

    //Initialize the controller communication
    controllerComm.Init();

    while(1)
    {
        controllerComm.Background();

        //Ping-pong the ownership of the controller data between controller and wireless
        if(nControllerDataOwner == CONTROLLER_COMM_OWNS_DATA)
        {
            nControllerDataOwner = WIRELESS_COMM_OWNS_DATA;
        }
    }
}

void WirelessCommunicationCore()
{
    bool bDataReceived = false;
    absolute_time_t lastMsgTime = get_absolute_time();

    //Init the wireless communication
    wirelessComm.Init();

    while(1)
    {
        wirelessComm.Background();

        //Ping-pong the ownership of the controller data between controller and wireless
        if(nControllerDataOwner == WIRELESS_COMM_OWNS_DATA)
        {
            if(GetInterfaceType() == CONSOLE_SIDE_INTERFACE)
            {
                uint deltaTime = absolute_time_diff_us(lastMsgTime, get_absolute_time());
                //Either a response has been recieved from the controller side interface
                //Or more than 1 millisecond has elapsed
                if(bDataReceived || (deltaTime > 1000))
                {
                    //Write console side controller data
                    bDataReceived = false;
                }
            }
            else //Controller side interface
            {
                //Check if there has been a message from the console side interface
                if(bDataReceived)
                {
                    //Write controller side controller data
                    bDataReceived = false;
                }
            }

            //Switch back to the controller comm owning data
            nControllerDataOwner = CONTROLLER_COMM_OWNS_DATA;
        }
    }
}

int main()
{
    stdio_init_all();

    while (!tud_cdc_connected()) {
        sleep_ms(10);
    }

    printf("Init pins\n");
    //Setup pins
    gpio_init(PAIR_PIN);
    gpio_init(FUNC_SEL_PIN);

    gpio_set_dir(PAIR_PIN, GPIO_IN);
    gpio_set_dir(FUNC_SEL_PIN, GPIO_IN);

    gpio_pull_up(PAIR_PIN);
    gpio_pull_up(FUNC_SEL_PIN);

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

    //Start the controller communication on the second core
    multicore_launch_core1(ControllerCommunicationCore);
    //Wait for the core to start
    uint32_t g = multicore_fifo_pop_blocking();

    WirelessCommunicationCore();

    return 0;
}