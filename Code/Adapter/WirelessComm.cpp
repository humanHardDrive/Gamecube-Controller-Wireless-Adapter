#include "WirelessComm.h"
#include "PinDefs.h"

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

bool WirelessComm::Write(void *pBuf, uint8_t len)
{
    if(PairingState() == PAIRING_STATE::COMPLETED)
    {

    }

    return false;
}

uint8_t WirelessComm::Read(void *pBuf, uint8_t len)
{
    return 0;
}

void WirelessComm::PairingProcess()
{
    if(m_PairingState == PAIRING_STATE::ACTIVE)
    {

    }
}
