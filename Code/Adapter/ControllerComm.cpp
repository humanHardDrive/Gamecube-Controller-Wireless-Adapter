#include "ControllerComm.h"

#include <string.h>
#include <stdio.h>

#include "pico/time.h"

#include "gcn_comm.pio.h"

#include "PinDefs.h"
#include "Utils.h"

static ControllerCommInfo aControllerInfo[NUM_CONTROLLERS];

ControllerComm::ControllerComm()
{
    
}

void ControllerComm::SwitchModeTX(ControllerCommInfo* pController)
{
    pio_sm_exec_wait_blocking(pController->pio, pController->sm, gcn_comm_switch_tx(pController->offset));
}

void ControllerComm::SwitchModeRX(ControllerCommInfo* pController)
{
    pio_sm_exec_wait_blocking(pController->pio, pController->sm, gcn_comm_switch_rx(pController->offset));
}

void ControllerComm::Init()
{
    uint offset = pio_add_program(pio0, &gcn_comm_program);

    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        aControllerInfo[i].info.isConnected = 0;
        memset(&aControllerInfo[i].info.values, 0, sizeof(ControllerValues));

        aControllerInfo[i].pio = pio0;
        aControllerInfo[i].sm = i;
        aControllerInfo[i].offset = offset;
        aControllerInfo[i].pin = CONTROLLER_DATA_BASE_PIN + i;

        aControllerInfo[i].info.isConnected = false;
        aControllerInfo[i].info.LastPollTime = get_absolute_time();
        aControllerInfo[i].info.waitingForResponse = false;
        aControllerInfo[i].info.consecutiveTimeouts = 0;

        gpio_init(aControllerInfo[i].pin);

        gcn_comm_program_init(aControllerInfo[i].pio, 
                              aControllerInfo[i].sm, 
                              aControllerInfo[i].offset,
                              aControllerInfo[i].pin,
                              250000);
        
        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            SwitchModeTX(&aControllerInfo[i]);
        else
            SwitchModeRX(&aControllerInfo[i]);
    }
}

/*Write data out to the controller through the PIO
val: Max 32-bit value to be written out
len: Number of bits to actually shift out
*/
void ControllerComm::Write(ControllerCommInfo* pController, uint32_t* pVal, uint8_t len)
{
    uint8_t nWords = ((len - 1) / 32) + 1;
    uint8_t nOffset = ((nWords * 32) - len);
    uint32_t mask = 0xFFFFFFFF << (32 - nOffset);

    pio_sm_put_blocking(pController->pio, pController->sm, len); //Put the number of bits onto the FIFO
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
        pio_sm_put_blocking(pController->pio, pController->sm, val);
    }
}

/*Read data from the controller through the PIO
pBuf: Buffer of words that the data is written into
pLen: Pointer number of bits read
*/
void ControllerComm::Read(ControllerCommInfo* pController, uint32_t* pBuf, uint8_t* pLen)
{
    uint8_t index = 0;
    uint8_t nWords;
    uint8_t nOffset;
    uint32_t mask;

    //Get the data from the PIO
    while(!pio_sm_is_rx_fifo_empty(pController->pio, pController->sm))
    {
        uint32_t v = (pio_sm_get_blocking(pController->pio, pController->sm));
        if(pio_sm_is_rx_fifo_empty(pController->pio, pController->sm)) //The last word is always the length
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

void ControllerComm::ControllerInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Keep track of how much time has elapsed since the last message
        uint deltaTime = absolute_time_diff_us(aControllerInfo[i].info.LastPollTime, get_absolute_time());
        
        if(!aControllerInfo[i].info.waitingForResponse)
        {
            uint32_t cmd, nBits;
            cmd = nBits = 0;

            //If the state machine is not waiting for a message back, ensure that the response buffer is clear
            while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
                pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);

            if(!aControllerInfo[i].info.isConnected && (deltaTime > 1000000))
            {
                //Check for the controller
                cmd = 0;
                nBits = 8;
            }
            else if(aControllerInfo[i].info.isConnected && (deltaTime > 1000000))
            {
                nBits = 24;
                if(aControllerInfo[i].info.doRumble)
                    cmd = POLL_RUMBLE_CMD;
                else
                    cmd = POLL_CMD;
            }

            //A write of 0 bits is ignored
            if(nBits > 0)
            {
                aControllerInfo[i].info.waitingForResponse = true;
                aControllerInfo[i].info.LastPollTime = get_absolute_time();
                aControllerInfo[i].info.LastCmd = cmd;

                Write(&aControllerInfo[i], &cmd, nBits);
            }
        }
        else
        {
            if(deltaTime < 1000) //1mS timeout
            {
                if(pio_interrupt_get(aControllerInfo[i].pio, aControllerInfo[i].sm))
                {
                    uint32_t data[4] = {0};
                    uint8_t nLen = 0;
                    Read(&aControllerInfo[i], data, &nLen);

                    if((aControllerInfo[i].info.LastCmd == POLL_CMD) || (aControllerInfo[i].info.LastCmd == POLL_RUMBLE_CMD))
                        memcpy(&aControllerInfo[i].info.values, &data, sizeof(aControllerInfo[i].info.values));

                    aControllerInfo[i].info.consecutiveTimeouts = 0; //Reset the consecutive timeouts
                    aControllerInfo[i].info.waitingForResponse = false; //Clear the flag waiting for response
                    pio_interrupt_clear(aControllerInfo[i].pio, aControllerInfo[i].sm); //Clear the receive interrupt
                }
            }
            else
            {
                aControllerInfo[i].info.consecutiveTimeouts++;
                aControllerInfo[i].info.waitingForResponse = false;
                SwitchModeTX(&aControllerInfo[i]);

                if(aControllerInfo[i].info.isConnected && (aControllerInfo[i].info.consecutiveTimeouts >= 2))
                {
                    printf("Controller disconnect %d\n", i);
                    aControllerInfo[i].info.isConnected = false;
                }
            }
        }
    }
}

void ControllerComm::ConsoleInterfaceBackground()
{
    for(uint8_t i = 0; i < 1; i++)
    {
        /*if(pio_interrupt_get(aControllerInfo[i].pio, aControllerInfo[i].sm))
        {
            while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
            {
                uint32_t v = pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);
                v >>= 1;

                printf("Read %d 0x%x\n", i, v);
            }

            l_ControllerSwitchModeRX(&aControllerInfo[i]);
            pio_interrupt_clear(aControllerInfo[i].pio, aControllerInfo[i].sm);
        }*/
        /*if(aControllerInfo[i].info.isConnected)
        {
            if(pio_interrupt_get(aControllerInfo[i].pio, aControllerInfo[i].sm))
            {
                while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
                {
                    uint32_t v = pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);
                    v >>= 1;

                    if((v == POLL_CMD) || (v == POLL_RUMBLE_CMD))
                    {
                        l_ControllerCommWrite(&aControllerInfo[i], 0, 32);
                        l_ControllerCommWrite(&aControllerInfo[i], 0, 32);

                        aControllerInfo[i].info.doRumble = (v == POLL_RUMBLE_CMD);
                    }
                    else if(v == 0)
                    {
                        //printf("Query %d\n", i);
                        l_ControllerCommWrite(&aControllerInfo[i], 0x90020, 24);
                    }
                    else
                    {
                        //Unknown command. Flush the rest of the FIFO
                        while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
                            pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);

                        //Go back to RX
                        l_ControllerSwitchModeRX(&aControllerInfo[i]);
                    }
                }
                pio_interrupt_clear(aControllerInfo[i].pio, aControllerInfo[i].sm);
            }
        }
        else
        {
            while(!pio_sm_is_rx_fifo_empty(aControllerInfo[i].pio, aControllerInfo[i].sm))
                pio_sm_get_blocking(aControllerInfo[i].pio, aControllerInfo[i].sm);
        }*/
    }
}

void ControllerComm::Background()
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        ControllerInterfaceBackground();
    else
        ConsoleInterfaceBackground();
}

void ControllerComm::GetControllerData(void *pBuf)
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
    {
        for(size_t i = 0; i < NUM_CONTROLLERS; i++)
        {
            if(aControllerInfo[i].info.isConnected)
            {
                //Copy the controller values into the buffer
                memcpy(&((ControllerValues*)pBuf)[i], &aControllerInfo[i].info.values, sizeof(ControllerValues));
            }
            else
            {
                //If the controller is not connected, set all of the data to 0
                memset(&((ControllerValues*)pBuf)[i], 0, sizeof(ControllerValues));
            }
        }
    }
    else //Console side interface
    {
        for(size_t i = 0; i < NUM_CONTROLLERS; i++)
        {
            ((uint8_t*)pBuf)[i] = aControllerInfo[i].info.doRumble;
        }
    }
}

unsigned char ControllerComm::AnyControllerConnected()
{
    return 0;
}