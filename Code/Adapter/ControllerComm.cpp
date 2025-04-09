#include "ControllerComm.h"

#include <string.h>
#include <stdio.h>

#include "pico/time.h"

#include "gcn_comm.pio.h"

#include "PinDefs.h"
#include "Utils.h"

ControllerComm::ControllerComm()
{
    auto_init_mutex(m_BackgroundLock);
    m_nStaleCount = 0;
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
        m_aControllerInfo[i].commPin = GetDevicePinMap()->controllerData[i];
        m_aControllerInfo[i].connectPin = GetDevicePinMap()->controllerStatus[i];

        //Initialize communication values
        m_aControllerInfo[i].info.isConnected = false;
        m_aControllerInfo[i].info.bIsStale = true;
        m_aControllerInfo[i].state = ControllerState::NOT_CONNECTED;
        m_aControllerInfo[i].LastPollTime = get_absolute_time();
        m_aControllerInfo[i].waitingForResponse = false;
        m_aControllerInfo[i].consecutiveTimeouts = 0;

        gpio_init(m_aControllerInfo[i].commPin);
        gpio_init(m_aControllerInfo[i].connectPin);

        gpio_set_dir(m_aControllerInfo[i].connectPin, GPIO_OUT);

        gpio_put(m_aControllerInfo[i].connectPin, true);

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

    m_nStaleCount++;
    m_bCanSleep = m_bSleepRequested = false;
    
    m_bStartupDone = false;
    m_StartupCount = 0;
    m_StartupTime = m_StartupPhaseTime = get_absolute_time();

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
    //Shift out the data
    for(int16_t i = 0; i < nWords; i++)
        pio_sm_put_blocking(pController->pio, pController->sm, pVal[i]);
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

    //Get the data from the PIO
    while(!pio_sm_is_rx_fifo_empty(pController->pio, pController->sm))
    {
        uint32_t v = (pio_sm_get_blocking(pController->pio, pController->sm));
        if(pio_sm_is_rx_fifo_empty(pController->pio, pController->sm)) //The last word is always the length
            *pLen = v;
        else
            pBuf[index++] = v; //All other words are the actual data
    }
    
    //Remove the stop bit
    pBuf[index - 1] <<= 1;
    //Decrement the length
    (*pLen)--;

    //Calculate the number of words the data would take
    nWords = (((*pLen) - 1) / 32) + 1;

    //Calculate the offset to shift the last word by
    nOffset = ((nWords * 32) - *pLen);
    if(nOffset)
        pBuf[index - 1] >>= nOffset;
}

void ControllerComm::StartupSequence()
{
    uint deltaStartupTime = absolute_time_diff_us(m_StartupTime, get_absolute_time());
    uint deltaPhaseTime = absolute_time_diff_us(m_StartupPhaseTime, get_absolute_time());
    //Check if the startup is done
    if(m_StartupCount >= 20)
    {
        //Turn off all the LEDs
        for(size_t i = 0; i < NUM_CONTROLLERS; i++)
            gpio_put(m_aControllerInfo[i].connectPin, false);

        printf("ControllerComm startup done\n");
        m_bStartupDone = true;
        return;
    }
    
    //Otherwise go through the LED sequence
    if(deltaPhaseTime > 100000)
    {
        gpio_put(m_aControllerInfo[m_StartupCount % NUM_CONTROLLERS].connectPin, false);
        m_StartupPhaseTime = get_absolute_time();
        m_StartupCount++;
    }
    else
        gpio_put(m_aControllerInfo[m_StartupCount % NUM_CONTROLLERS].connectPin, true);
}

void ControllerComm::MainBackground()
{
    bool bCanSleep = true;

    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        ControllerInterfaceBackground(m_bSleepRequested);
    else
        ConsoleInterfaceBackground(m_bSleepRequested);

    /*for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        if(m_bSleepRequested)
        {
            gpio_put(m_aControllerInfo[i].connectPin, false); //Turn off the output
            if(m_aControllerInfo[i].waitingForResponse)
                bCanSleep = false;
        }
        else
        {
            if(m_aControllerInfo[i].info.isConnected)
                gpio_put(m_aControllerInfo[i].connectPin, true);
            else
                gpio_put(m_aControllerInfo[i].connectPin, false);
        }
    }

    //Set the sleep flag
    if(m_bSleepRequested && bCanSleep)
        m_bCanSleep = true;*/
}

void ControllerComm::ControllerInterfaceBackground(bool bStopComm)
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        //Keep track of how much time has elapsed since the last message
        uint deltaTime = absolute_time_diff_us(m_aControllerInfo[i].LastPollTime, get_absolute_time());
        
        //Check if it's time to send a new message and the device isn't going to sleep
        if(!m_aControllerInfo[i].waitingForResponse && !bStopComm)
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

bool ControllerComm::IsControllerInfoStale()
{
    return (m_nStaleCount == NUM_CONTROLLERS);
}

void ControllerComm::ConsoleInterfaceBackground(bool bStopComm)
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

            if(m_nStaleCount < NUM_CONTROLLERS)
                m_nStaleCount++;

            //Only do something with it if the controller is connected
            if(m_aControllerInfo[i].info.isConnected)
            {
                bool bRumble = false;
                bool bRecal = false;

                switch(data[0])
                {
                    case POLL_RUMBLE_MSG.first:
                    bRumble = true;
                    case POLL_MSG.first:
                    if(nLen == POLL_MSG.second)
                    {
                        uint64_t val = m_aControllerInfo[i].info.controllerValues.raw;

                        Write(&m_aControllerInfo[i], (uint32_t*)&val, POLL_RSP.second);
                        m_aControllerInfo[i].info.consoleValues.doRumble = bRumble;
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
                        memcpy(&buf[0], &m_aControllerInfo[i].info.controllerValues, sizeof(ControllerValues));

                        //Set the recal flag if neccessary
                        m_aControllerInfo[i].info.consoleValues.doRecal = bRecal;
                        //Write the response
                        Write(&m_aControllerInfo[i], (uint32_t*)buf, PROBE_ORIGIN_RSP.second);
                        bKnown = true;
                    }
                    break;

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
                }

                if(!bKnown)
                {
                    printf("Unkown CMD 0x%x Len %d\n", data[0], nLen);
                    //Need to put it back to receive if it's not going to transmit
                    SwitchModeRX(&m_aControllerInfo[i]);
                }
                else
                    m_aControllerInfo[i].info.bIsStale = true;
            }
            else
            {
                //Switch back to receive if the controller does become connected
                SwitchModeRX(&m_aControllerInfo[i]);
            }
        }
        else
        {
            //Speed hack
            //Operates under the assumption that all requests complete at the same time
            //Thus if controller 1 doesn't have data, none of the others do
            //Reduces the penalty incurred by having to restart the loop because controller 2, 3, or 4 detected the interrupt first
            return;
        }
    }
}

void ControllerComm::Background()
{
    MainBackground();
}

void ControllerComm::SetControllerData(ControllerValues* pControllerData)
{
    m_nStaleCount = 0;

    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        m_aControllerInfo[i].info.controllerValues = pControllerData[i];
        m_aControllerInfo[i].info.bIsStale = false;

        if(m_aControllerInfo[i].info.controllerValues.pad2)
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

bool ControllerComm::AnyControllerConnected()
{
    for(size_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        if(m_aControllerInfo[i].info.isConnected)
            return true;
    }

    return false;
}

void ControllerComm::Sleep()
{
    m_bSleepRequested = true;
    while(!m_bCanSleep) { tight_loop_contents(); }
}

void ControllerComm::Wake()
{
    m_bSleepRequested = m_bCanSleep = false;
}
