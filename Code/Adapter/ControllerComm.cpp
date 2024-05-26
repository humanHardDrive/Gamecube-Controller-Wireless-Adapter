#include "ControllerComm.h"

#include <string.h>
#include <stdio.h>

#include "pico/time.h"

#include "gcn_comm.pio.h"

#include "PinDefs.h"
#include "Utils.h"

static ControllerValues NO_CONNECT;

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

/*

*/
void ControllerComm::Init()
{
    memset(&NO_CONNECT, 0, sizeof(ControllerValues));

    //Generate the offset for the communication PIO program
    uint offset = pio_add_program(pio0, &gcn_comm_program);

    //Setup the controller data
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Clear out the controller values
        memset(&m_aControllerInfo[i].info.values, 0, sizeof(ControllerValues));

        //Initialize the PIO references
        m_aControllerInfo[i].pio = pio0;
        m_aControllerInfo[i].sm = i;
        m_aControllerInfo[i].offset = offset;
        m_aControllerInfo[i].pin = CONTROLLER_DATA_BASE_PIN + i;

        //Initialize communication values
        m_aControllerInfo[i].info.isConnected = false;
        m_aControllerInfo[i].state = ControllerState::NOT_CONNECTED;
        m_aControllerInfo[i].LastPollTime = get_absolute_time();
        m_aControllerInfo[i].waitingForResponse = false;
        m_aControllerInfo[i].consecutiveTimeouts = 0;

        gpio_init(m_aControllerInfo[i].pin);

        //Initialize communication with the communication PIO
        gcn_comm_program_init(m_aControllerInfo[i].pio, //PIO
                              m_aControllerInfo[i].sm,  //State machine
                              m_aControllerInfo[i].offset, //Program offset
                              m_aControllerInfo[i].pin, //Pin to read/write
                              250000); //Communication speed. 250KHz (4uS per bit)
        
        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            SwitchModeTX(&m_aControllerInfo[i]); //Controller side starts in TX to communicate with controllers
        else
            SwitchModeRX(&m_aControllerInfo[i]); //Console side starts in RX to recieve data from the console
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
        else if(nWords > 1)
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
        uint deltaTime = absolute_time_diff_us(m_aControllerInfo[i].LastPollTime, get_absolute_time());
        
        if(!m_aControllerInfo[i].waitingForResponse)
        {
            uint32_t cmd, nBits;
            cmd = nBits = 0;

            //If the state machine is not waiting for a message back, ensure that the response buffer is clear
            while(!pio_sm_is_rx_fifo_empty(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm))
                pio_sm_get_blocking(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm);

            switch(m_aControllerInfo[i].state)
            {
                case ControllerState::NOT_CONNECTED:
                if(deltaTime > 12000)
                {
                    cmd = CONNECTED_MSG.first;
                    nBits = CONNECTED_MSG.second;
                }
                break;

                case ControllerState::STARTUP_CONFIG:
                if(deltaTime > 12000)
                {
                    cmd = CONFIG_MSG.first;
                    nBits = CONFIG_MSG.second;
                }
                break;

                case ControllerState::POLLING:
                if(deltaTime > 1000)
                {
                    nBits = POLL_CMD.second;
                    if(m_aControllerInfo[i].info.doRumble)
                        cmd = POLL_RUMBLE_CMD.first;
                    else
                        cmd = POLL_CMD.first;
                }
                break;
            }

            //A write of 0 bits is ignored
            if(nBits > 0)
            {
                m_aControllerInfo[i].waitingForResponse = true;
                m_aControllerInfo[i].LastPollTime = get_absolute_time();
                m_aControllerInfo[i].LastCmd = cmd;

                Write(&m_aControllerInfo[i], &cmd, nBits);
            }
        }
        else
        {
            if(deltaTime < 1000) //1mS timeout
            {
                if(pio_interrupt_get(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm))
                {
                    uint32_t data[4] = {0};
                    uint8_t nLen = 0;
                    Read(&m_aControllerInfo[i], data, &nLen);

                    switch(m_aControllerInfo[i].LastCmd)
                    {
                        case POLL_CMD.first:
                        case POLL_RUMBLE_CMD.first:
                        memcpy(&m_aControllerInfo[i].info.values, data, sizeof(m_aControllerInfo[i].info.values));
                        
                        break;

                        case CONFIG_MSG.first:
                        memcpy(&m_aControllerInfo[i].info.configInfo, data, sizeof(m_aControllerInfo[i].info.configInfo));
                        printf("Staring to poll controller %d\n", i);
                        m_aControllerInfo[i].state = ControllerState::POLLING;
                        break;

                        case CONNECTED_MSG.first:
                        printf("Controller %d connected 0x%x (%d)\n", i, data[0], nLen);
                        m_aControllerInfo[i].info.id = data[0];
                        m_aControllerInfo[i].info.isConnected = true;
                        m_aControllerInfo[i].state = ControllerState::STARTUP_CONFIG;
                        break;
                    }                        

                    m_aControllerInfo[i].consecutiveTimeouts = 0; //Reset the consecutive timeouts
                    m_aControllerInfo[i].waitingForResponse = false; //Clear the flag waiting for response
                    pio_interrupt_clear(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm); //Clear the receive interrupt
                }
            }
            else
            {
                m_aControllerInfo[i].consecutiveTimeouts++;
                m_aControllerInfo[i].waitingForResponse = false;
                SwitchModeTX(&m_aControllerInfo[i]);

                if(m_aControllerInfo[i].info.isConnected && (m_aControllerInfo[i].consecutiveTimeouts >= 2))
                {
                    printf("Controller %d disconnected\n", i);
                    m_aControllerInfo[i].info.isConnected = false;
                    memset(&m_aControllerInfo[i].info.values, 0, sizeof(ControllerValues));
                }
            }
        }
    }
}

void ControllerComm::ConsoleInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        if(pio_interrupt_get(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm))
        {
            //Read the data to clear the buffer
            uint32_t data[4] = {0};
            bool bKnown = false;
            uint8_t nLen = 0;
            
            pio_interrupt_clear(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm);
            Read(&m_aControllerInfo[i], data, &nLen);

            //Only do something with it if the controller is connected
            if(m_aControllerInfo[i].info.isConnected)
            {
                bool bRumble = false;

                switch(data[0])
                {
                    case CONNECTED_MSG.first:
                    if(nLen == CONNECTED_MSG.second)
                    {
                        Write(&m_aControllerInfo[i], (uint32_t*)&CONTROLLER_ID, 24);
                        bKnown = true;
                    }
                    break;

                    case CONFIG_MSG.first:
                    if(nLen == CONFIG_MSG.second)
                    {
                        Write(&m_aControllerInfo[i], m_aControllerInfo[i].info.configInfo, 80);
                        bKnown = true;
                    }
                    break;

                    case POLL_RUMBLE_CMD.first:
                    bRumble = true;
                    case POLL_CMD.first:
                    if(nLen == POLL_CMD.second)
                    {
                        Write(&m_aControllerInfo[i], (uint32_t*)&m_aControllerInfo[i].info.values, 64);
                        m_aControllerInfo[i].info.doRumble = bRumble;
                        bKnown = true;
                    }
                    break;
                }

                if(!bKnown)
                    printf("Unkown CMD 0x%x Len %d\n", data[0], nLen);
            }
            
            if(!bKnown)
                SwitchModeRX(&m_aControllerInfo[i]);
        }
    }
}

void ControllerComm::Background()
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        ControllerInterfaceBackground();
    else
        ConsoleInterfaceBackground();
}

void ControllerComm::SetControllerData(ControllerValues* pControllerData)
{
    for(size_t i = 0; i < 1; i++)
    {
        m_aControllerInfo[i].info.values = pControllerData[i];

        if(m_aControllerInfo[i].info.values.pad)
        {
            if(!m_aControllerInfo[i].info.isConnected)
            {
                printf("Controller %d connected\n", i);
                m_aControllerInfo[i].info.isConnected = true;
            }
        }
        else
        {
            if(m_aControllerInfo[i].info.isConnected)
            {
                printf("Controller %d disconnected\n", i);
                m_aControllerInfo[i].info.isConnected = false;
            }
        }
    }
}

void ControllerComm::GetControllerData(ControllerValues* pControllerData)
{
    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Copy the controller values into the buffer
        memcpy(&(pControllerData[i]), &m_aControllerInfo[i].info.values, sizeof(ControllerValues));
    }
}

void ControllerComm::GetConsoleData(void *pBuf)
{
}

void ControllerComm::SetConsolData(void *pBuf)
{
}

unsigned char ControllerComm::AnyControllerConnected()
{
    return 0;
}