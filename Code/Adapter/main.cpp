#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/gpio.h"

#include "PinDefs.h"
#include "Utils.h"

#include "ControllerComm.h"
#include "WirelessComm.h"
#include "PowerManager.h"

#include "bsp/board_api.h"
#include "tusb.h"

//Communication objects
ControllerComm controllerComm;
WirelessComm wirelessComm;
PowerManager powerManager;

#define CONTROLLER_COMM_OWNS_DATA   0
#define WIRELESS_COMM_OWNS_DATA     1

//Controller data signal
uint8_t nControllerDataOwner = CONTROLLER_COMM_OWNS_DATA;
//Controller data buffer
ControllerValues controllerBuffer[NUM_CONTROLLERS];
ConsoleValues consoleBuffer[NUM_CONTROLLERS];
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
    absolute_time_t lastControllerConnectedTime = get_absolute_time();

    //Signal back that the core has started
    multicore_fifo_push_blocking(0);
    printf("Controller core started\n");

    //Initialize the controller communication
    controllerComm.Init();

    while(1)
    {
        controllerComm.Background();

        //Ping-pong the ownership of the controller data between controller and wireless
        if(nControllerDataOwner == CONTROLLER_COMM_OWNS_DATA)
        {
            if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            {
                controllerComm.GetControllerData(controllerBuffer);
                controllerComm.SetConsoleData(consoleBuffer);
            }
            else if(controllerComm.IsControllerInfoStale()) //Console side interface
            {
                controllerComm.GetConsoleData(consoleBuffer);
                controllerComm.SetControllerData(controllerBuffer);
            }

            nControllerDataOwner = WIRELESS_COMM_OWNS_DATA;
        }

        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        {
            if(!controllerComm.AnyControllerConnected() &&
                (absolute_time_diff_us(lastControllerConnectedTime, get_absolute_time()) > 5000))
            {
                lastControllerConnectedTime = get_absolute_time();
            }
            else if(controllerComm.AnyControllerConnected())
            {
                lastControllerConnectedTime = get_absolute_time();
            }
        }
    }
}

void WirelessCommunicationCore()
{
    bool bDataReceived = false;
    uint8_t nMissedMsgs = 0;
    absolute_time_t lastMsgTXTime = get_absolute_time();
    absolute_time_t lastMsgRXTime = get_absolute_time();

    printf("Wireless core started\n");

    //Init the wireless communication
    bool bInitSuccess = wirelessComm.Init();

    board_init();

    while(1)
    {
        wirelessComm.Background();

        if(bInitSuccess)
        {
            //Ping-pong the ownership of the controller data between controller and wireless
            if(nControllerDataOwner == WIRELESS_COMM_OWNS_DATA)
            {
                uint8_t rxBuf[MAX_PAYLOAD_SIZE];
                uint8_t nBytesRead = 0;

                if(nBytesRead = wirelessComm.Read(rxBuf, MAX_PAYLOAD_SIZE))
                {
                    bDataReceived = true;
                    lastMsgRXTime = get_absolute_time();
                }

                if(GetInterfaceType() == CONSOLE_SIDE_INTERFACE)
                {
                    uint deltaTime = absolute_time_diff_us(lastMsgTXTime, get_absolute_time());
                    
                    //Either a response has been recieved from the controller side interface
                    //Or more than 1 millisecond has elapsed
                    if(deltaTime > 1000)
                    {
                        lastMsgTXTime = get_absolute_time();

                        if(bDataReceived)
                        {
                            //Copy over the controller data from the controller side interface
                            memcpy(controllerBuffer, rxBuf, MAX_PAYLOAD_SIZE);
                            nMissedMsgs = 0;
                        }
                        else
                        {
                            if(nMissedMsgs < 3)
                                nMissedMsgs++;

                            if(nMissedMsgs >= 3)
                            {
                                memset(controllerBuffer, 0, sizeof(controllerBuffer));
                            }
                        }

                        //Write console side controller data
                        wirelessComm.Write(consoleBuffer, sizeof(consoleBuffer));
                        //Clear the received flag to wait to transmit again
                        bDataReceived = false;
                    }
                }
                else //Controller side interface
                {
                    //Check if there has been a message from the console side interface
                    if(bDataReceived)
                    {
                        //Copy over the controller data from the controller side interface
                        memcpy(consoleBuffer, rxBuf, sizeof(consoleBuffer));
                        //Write controller side controller data
                        wirelessComm.Write(controllerBuffer, sizeof(controllerBuffer));
                        //Clear the received flag to wait to transmit again
                        bDataReceived = false;
                    }
                }

                //Switch back to the controller comm owning data
                nControllerDataOwner = CONTROLLER_COMM_OWNS_DATA;
            }
        }
    }
}

int main()
{
    stdio_init_all();

    sleep_ms(1500);

    printf("Init pins\n");
    //Setup pins   
    gpio_init(GetDevicePinMap()->functionSelect);
    gpio_set_dir(GetDevicePinMap()->functionSelect, GPIO_IN);
    gpio_pull_up(GetDevicePinMap()->functionSelect);

    //Sleep to let the pins setup
    sleep_ms(150);

    //Detect device configuration
    if(!gpio_get(GetDevicePinMap()->functionSelect))
    {
        /*
        This is the controller side interface
        Poll for controller connection
        Poll for controller controllerValues
        Report controller controllerValues back to the console side interface
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

    //Initialize the power manager to keep the board on
    powerManager.Init();

    powerManager.AddModuleToManage(&controllerComm);
    powerManager.AddModuleToManage(&wirelessComm);

    //Start the controller communication on the second core
    multicore_launch_core1(ControllerCommunicationCore);
    //Wait for the core to start
    uint32_t g = multicore_fifo_pop_blocking();

    //Start the wireless communication loop
    WirelessCommunicationCore();

    //Should never reach this
    return 0;
}