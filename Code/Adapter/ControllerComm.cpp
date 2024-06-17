#include "ControllerComm.h"

#include <string.h>
#include <stdio.h>

#include "pico/time.h"

#include "gcn_comm.pio.h"

#include "PinDefs.h"
#include "Utils.h"

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
bool ControllerComm::Init()
{
    //Generate the offset for the communication PIO program
    uint offset = pio_add_program(pio0, &gcn_comm_program);

    //Setup the controller data
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Clear out the controller values
        memset(&m_aControllerInfo[i].info.controllerValues, 0, sizeof(ControllerValues));

        //Clear out console values
        m_aControllerInfo[i].info.consoleValues.doRecal = false;
        m_aControllerInfo[i].info.consoleValues.doRumble = false;

        //Initialize the PIO references
        m_aControllerInfo[i].pio = pio0;
        m_aControllerInfo[i].sm = i;
        m_aControllerInfo[i].offset = offset;
        m_aControllerInfo[i].commPin = CONTROLLER_DATA_BASE_PIN + i;
        m_aControllerInfo[i].connectPin = CONTROLLER_CONNECTED_BASE_PIN + i;

        //Initialize communication values
        m_aControllerInfo[i].info.isConnected = false;
        m_aControllerInfo[i].state = ControllerState::NOT_CONNECTED;
        m_aControllerInfo[i].LastPollTime = get_absolute_time();
        m_aControllerInfo[i].waitingForResponse = false;
        m_aControllerInfo[i].consecutiveTimeouts = 0;

        gpio_init(m_aControllerInfo[i].commPin);
        gpio_init(m_aControllerInfo[i].connectPin);

        gpio_set_dir(m_aControllerInfo[i].connectPin, GPIO_OUT);

        gpio_put(m_aControllerInfo[i].connectPin, false);

        //Initialize communication with the communication PIO
        gcn_comm_program_init(m_aControllerInfo[i].pio, //PIO
                              m_aControllerInfo[i].sm,  //State machine
                              m_aControllerInfo[i].offset, //Program offset
                              m_aControllerInfo[i].commPin, //Pin to read/write
                              250000); //Communication speed. 250KHz (4uS per bit)
        
        if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
            SwitchModeTX(&m_aControllerInfo[i]); //Controller side starts in TX to communicate with controllers
        else
            SwitchModeRX(&m_aControllerInfo[i]); //Console side starts in RX to recieve data from the console
    }

    return true;
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

void ControllerComm::ControllerInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Keep track of how much time has elapsed since the last message
        uint deltaTime = absolute_time_diff_us(m_aControllerInfo[i].LastPollTime, get_absolute_time());
        
        //Check if it's time to send a new message
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
                    cmd = PROBE_MSG.first;
                    nBits = PROBE_MSG.second;
                }
                break;

                case ControllerState::STARTUP_CONFIG:
                if(deltaTime > 12000)
                {
                    cmd = PROBE_ORIGIN_MSG.first;
                    nBits = PROBE_ORIGIN_MSG.second;
                }
                break;

                case ControllerState::POLLING:
                if(deltaTime > 1000)
                {
                    if(m_aControllerInfo[i].info.consoleValues.doRecal)
                    {
                        nBits = PROBE_ORIGIN_RECAL_MSG.second;
                        cmd = PROBE_ORIGIN_RECAL_MSG.first;
                        
                        printf("Controller %d recal\n", i);
                        m_aControllerInfo[i].info.consoleValues.doRecal = false;
                    }
                    else
                    {
                        nBits = POLL_MSG.second;
                        if(m_aControllerInfo[i].info.consoleValues.doRumble)
                            cmd = POLL_RUMBLE_MSG.first;
                        else
                            cmd = POLL_MSG.first;
                    }
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
        else //Handle waiting for a response
        {
            if(deltaTime < 1000) //1mS timeout
            {
                //Check if the RX interrupt is set
                if(pio_interrupt_get(m_aControllerInfo[i].pio, m_aControllerInfo[i].sm))
                {
                    uint32_t data[4] = {0};
                    uint8_t nLen = 0;
                    //Read the data
                    Read(&m_aControllerInfo[i], data, &nLen);

                    switch(m_aControllerInfo[i].LastCmd)
                    {
                        case POLL_MSG.first:
                        case POLL_RUMBLE_MSG.first:
                        memcpy(&m_aControllerInfo[i].info.controllerValues, data, sizeof(m_aControllerInfo[i].info.controllerValues));
                        
                        break;

                        case PROBE_ORIGIN_RECAL_MSG.first:
                        case PROBE_ORIGIN_MSG.first:
                        printf("Staring to poll controller %d\n", i);
                        m_aControllerInfo[i].state = ControllerState::POLLING;
                        break;

                        case PROBE_MSG.first:
                        printf("Controller %d connected 0x%x (%d)\n", i, data[0], nLen);
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
                    m_aControllerInfo[i].state = ControllerState::NOT_CONNECTED;
                    memset(&m_aControllerInfo[i].info.controllerValues, 0, sizeof(ControllerValues));
                }
            }
        }
    }
}

void ControllerComm::ConsoleInterfaceBackground()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        uint deltaTime = absolute_time_diff_us(m_aControllerInfo[i].LastPollTime, get_absolute_time());
        m_aControllerInfo[i].LastPollTime = get_absolute_time();

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
                bool bRecal = false;

                switch(data[0])
                {
                    case PROBE_RECAL_MSG.first:
                    bRecal = true;
                    case PROBE_MSG.first:
                    if(nLen == PROBE_MSG.second)
                    {
                        m_aControllerInfo[i].info.consoleValues.doRecal = bRecal;
                        Write(&m_aControllerInfo[i], (uint32_t*)&PROBE_RSP.first, PROBE_RSP.second);
                        bKnown = true;
                    }
                    break;

                    case PROBE_ORIGIN_RECAL_MSG.first:
                    bRecal = true;
                    case PROBE_ORIGIN_MSG.first:
                    if(nLen == PROBE_ORIGIN_MSG.second)
                    {
                        uint8_t buf[12] = {0}; //12 to align it to 32-bit
                        //Copy in the controller data with the 2 null bytes
                        memcpy(&buf[2], &m_aControllerInfo[i].info.controllerValues, sizeof(ControllerValues));

                        //Set the recal flag if neccessary
                        m_aControllerInfo[i].info.consoleValues.doRecal = bRecal;
                        //Write the response
                        Write(&m_aControllerInfo[i], (uint32_t*)buf, PROBE_ORIGIN_RSP.second);
                        bKnown = true;
                    }
                    break;

                    case POLL_RUMBLE_MSG.first:
                    bRumble = true;
                    case POLL_MSG.first:
                    if(nLen == POLL_MSG.second)
                    {
                        uint32_t buf[2] = {0};
                        memcpy(buf, &m_aControllerInfo[i].info.controllerValues, sizeof(ControllerValues));

                        Write(&m_aControllerInfo[i], buf, POLL_RSP.second);
                        m_aControllerInfo[i].info.consoleValues.doRumble = bRumble;
                        bKnown = true;
                    }
                    break;
                }

                if(!bKnown)
                    printf("Unkown CMD 0x%x Len %d %u\n", data[0], nLen, deltaTime);
            }
            
            if(!bKnown)
                Write(&m_aControllerInfo[i], (uint32_t*)&PROBE_MSG.first, PROBE_MSG.second);
        }
    }
}

void ControllerComm::Background()
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        ControllerInterfaceBackground();
    else
        ConsoleInterfaceBackground();

    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        if(m_aControllerInfo[i].info.isConnected)
            gpio_put(m_aControllerInfo[i].connectPin, true);
        else
            gpio_put(m_aControllerInfo[i].connectPin, false);
    }
}

void ControllerComm::SetControllerData(ControllerValues* pControllerData)
{
    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        m_aControllerInfo[i].info.controllerValues = pControllerData[i];

        if(m_aControllerInfo[i].info.controllerValues.pad)
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
        memcpy(&(pControllerData[i]), &m_aControllerInfo[i].info.controllerValues, sizeof(ControllerValues));
    }
}

void ControllerComm::GetConsoleData(ConsoleValues* pConsoleData)
{
    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        memcpy(&(pConsoleData[i]), &m_aControllerInfo[i].info.consoleValues, sizeof(ConsoleValues));
    }
}

void ControllerComm::SetConsoleData(ConsoleValues* pConsoleData)
{
    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        memcpy(&m_aControllerInfo[i].info.consoleValues, &(pConsoleData[i]), sizeof(ConsoleValues));
    }
}

unsigned char ControllerComm::AnyControllerConnected()
{
    return 0;
}

void ControllerComm::Sleep()
{
}
