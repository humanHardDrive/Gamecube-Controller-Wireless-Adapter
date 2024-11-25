#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/rand.h"

#include "hardware/gpio.h"

#include "RF24.h"

#define MAX_PAYLOAD_SIZE    32

int main()
{
    RF24 radio;
    SPI SPIBus;

    uint SCKPin, MOSIPin, MISOPin;
    uint CSPin, CEPin;
    uint RXPin, TXPin;

    uint8_t messageOutBuffer[MAX_PAYLOAD_SIZE];
    uint8_t messageInBuffer[MAX_PAYLOAD_SIZE];

    absolute_time_t lastMsgTXTime = get_absolute_time();

    bool bMaster = false;

    stdio_init_all();

    printf("Initializing IO\n");

    gpio_init(25);
    gpio_set_dir(25, GPIO_IN);
    gpio_pull_up(25);

    if(!gpio_get(25))
    {
        SCKPin = 14;
        MOSIPin = 15;
        MISOPin = 12;

        CSPin = 13;
        CEPin = 16;

        RXPin = 5;
        TXPin = 4;
    }
    else
    {
        SCKPin = 14;
        MOSIPin = 11;
        MISOPin = 8;

        CSPin = 13;
        CEPin = 12;

        RXPin = 16;
        TXPin = 17;
    }

    gpio_init(RXPin);
    gpio_init(TXPin);
    gpio_init(CEPin);
    gpio_init(CSPin);

    gpio_set_dir(RXPin, GPIO_OUT);
    gpio_set_dir(TXPin, GPIO_OUT);
    gpio_set_dir(CEPin, GPIO_OUT);
    gpio_set_dir(CSPin, GPIO_OUT);

    gpio_put(RXPin, false);
    gpio_put(TXPin, false);
    gpio_put(CEPin, false);

    SPIBus.begin(spi1, SCKPin, MOSIPin, MISOPin);
    
    printf("Initializing radio...");

    if(!radio.begin(&SPIBus, CEPin, CSPin))
    {
        printf("failed");
        while(1) {;}
    }
    printf("success\n");

    printf("Configuring radio...\n");
    if(gpio_get(25))
    {
        radio.openWritingPipe(0x10101010);
        radio.openReadingPipe(1, 0x01010101);
    }
    else
    {
        radio.openWritingPipe(0x01010101);
        radio.openReadingPipe(1, 0x10101010);
    }

    radio.setCRCLength(rf24_crclength_e::RF24_CRC_16);
    radio.setAutoAck(false);
    radio.setDataRate(rf24_datarate_e::RF24_2MBPS);
    radio.setPALevel(RF24_PA_MIN, true);

    //radio.txDelay = 100; //Autoack is disabled
    radio.csDelay = 1;

    radio.startListening();

    printf("Configure device as master (Y/N)\n");
    char c = getchar();

    if(c == 'Y' || c == 'y')
    {
        bMaster = true;
        printf("Device configured as master\n");
    }
    else
    {
        printf("Device configured as slave\n");
    }

    printf("Starting loopback...\n");

    while(1)
    {
        if(bMaster)
        {
            uint deltaTime = absolute_time_diff_us(lastMsgTXTime, get_absolute_time());
            if(deltaTime > 500000)
            {
                printf("Sending ");
                for(uint i = 0; i < MAX_PAYLOAD_SIZE; i++)
                {
                    messageOutBuffer[i] = (uint8_t)get_rand_32();
                    printf("0x%x ", messageOutBuffer[i]);
                }
                printf("\n");

                gpio_put(TXPin, true);

                radio.stopListening();

                if(!radio.writeFast(messageOutBuffer, MAX_PAYLOAD_SIZE))
                    printf("Write failed\n");


                if(!radio.txStandBy())
                    printf("Standby failed\n");

                radio.startListening();

                gpio_put(TXPin, false);

                lastMsgTXTime = get_absolute_time();
            }
        }

        if(radio.available())
        {
            gpio_put(RXPin, true);
            radio.read(messageInBuffer, MAX_PAYLOAD_SIZE);

            if(bMaster)
            {
                bool bMatch = true;
                for(uint i = 0; i < MAX_PAYLOAD_SIZE && bMatch; i++)
                {
                    if(messageOutBuffer[i] != messageInBuffer[i])
                        bMatch = false;
                }

                if(!bMatch)
                    printf("RX mismatch\n");
                else
                    printf("RX match\n");
            }
            else
            {
                printf("Recieved ");
                for(uint i = 0; i < MAX_PAYLOAD_SIZE; i++)
                    printf("0x%x ", messageInBuffer[i]);
                printf("\n");

                gpio_put(TXPin, true);

                radio.stopListening();

                if(!radio.writeFast(messageInBuffer, MAX_PAYLOAD_SIZE))
                    printf("Write failed\n");

                if(!radio.txStandBy())
                    printf("Standby failed\n");

                radio.startListening();

                gpio_put(TXPin, false);
            }

            gpio_put(RXPin, false);
        }
    }

    return 0;
}