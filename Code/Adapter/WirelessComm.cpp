#include "WirelessComm.h"
#include "PinDefs.h"

WirelessComm::WirelessComm()
{
}

void WirelessComm::Init()
{
    gpio_init(RX_ACTIVITY_PIN);
    gpio_init(TX_ACTIVITY_PIN);
    gpio_init(NRF_CE_PIN);

    gpio_set_dir(RX_ACTIVITY_PIN, GPIO_OUT);
    gpio_set_dir(TX_ACTIVITY_PIN, GPIO_OUT);
    gpio_set_dir(NRF_CE_PIN, GPIO_OUT);

    gpio_put(RX_ACTIVITY_PIN, false);
    gpio_put(TX_ACTIVITY_PIN, false);
    gpio_put(NRF_CE_PIN, false);

    //Handles all the SPI setup
    m_SPIBus.begin(spi0, SPI0_CLK_PIN, SPI0_MOSI_PIN, SPI0_MISO_PIN);

    //Start the radio
    //TODO: Check to see if it's started
    if(!m_Radio.begin(&m_SPIBus, NRF_CE_PIN, NRF_CS_PIN))
        printf("Failed to connect to radio\n");

    m_Radio.openWritingPipe(0x10101010);
    m_Radio.openReadingPipe(1, 0x01010101);
    m_Radio.setAutoAck(false);

    m_Radio.startListening();
}

void WirelessComm::Background()
{
    PairingProcess();
}

void WirelessComm::Pair()
{
}

bool WirelessComm::IsPaired()
{
    return false;
}

bool WirelessComm::Write(void *pBuf, uint8_t len)
{
    gpio_put(TX_ACTIVITY_PIN, true);

    m_Radio.stopListening();

    if(!m_Radio.writeFast(pBuf, len))
        printf("WriteFast failed?\n");

    if(!m_Radio.txStandBy())
        printf("Standby failed?\n");

    m_Radio.startListening();

    gpio_put(TX_ACTIVITY_PIN, false);
}

uint8_t WirelessComm::Read(void *pBuf, uint8_t len)
{
    return 0;
}

void WirelessComm::PairingProcess()
{
}
