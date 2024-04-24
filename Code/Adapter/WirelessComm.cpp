#include "WirelessComm.h"
#include "PinDefs.h"

#define MAX_PAYLOAD_SIZE    32

WirelessComm::WirelessComm()
{
}

void WirelessComm::Init()
{
    //Handles all the SPI setup
    m_SPIBus.begin(spi0, SPI0_CLK_PIN, SPI0_MOSI_PIN, SPI0_MISO_PIN);

    //Start the radio
    //TODO: Check to see if it's started
    m_Radio.begin(&m_SPIBus, NRF_CE_PIN, NRF_CS_PIN);
}

void WirelessComm::Background()
{
    PairingProcess();
}

void WirelessComm::Pair()
{
    if(m_PairingState != PAIRING_STATE::ACTIVE)
    {

    }
}

bool WirelessComm::IsPaired()
{
    return false;
}

void WirelessComm::PairingProcess()
{
    if(m_PairingState == PAIRING_STATE::ACTIVE)
    {

    }
}
