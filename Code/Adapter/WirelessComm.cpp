#include "WirelessComm.h"
#include "PinDefs.h"

#include "Utils.h"

WirelessComm::WirelessComm() :
    m_bInit(false)
{
}

bool WirelessComm::Init()
{
    gpio_init(GetDevicePinMap()->wirelessRX);
    gpio_init(GetDevicePinMap()->wirelessTX);
    gpio_init(GetDevicePinMap()->wirelessCE);
    gpio_init(GetDevicePinMap()->wirelessCS);

    gpio_set_dir(GetDevicePinMap()->wirelessRX, GPIO_OUT);
    gpio_set_dir(GetDevicePinMap()->wirelessTX, GPIO_OUT);
    gpio_set_dir(GetDevicePinMap()->wirelessCE, GPIO_OUT);
    gpio_set_dir(GetDevicePinMap()->wirelessCS, GPIO_OUT);

    gpio_put(GetDevicePinMap()->wirelessRX, false);
    gpio_put(GetDevicePinMap()->wirelessTX, false);
    gpio_put(GetDevicePinMap()->wirelessCE, false);

    //Handles all the SPI setup
    m_SPIBus.begin(GetDevicePinMap()->wirelessSPI, GetDevicePinMap()->wirelessSCK, GetDevicePinMap()->wirelessMOSI, GetDevicePinMap()->wirelessMISO);

    //Start the radio
    //TODO: Check to see if it's started
    if(!m_Radio.begin(&m_SPIBus, GetDevicePinMap()->wirelessCE, GetDevicePinMap()->wirelessCS))
    {
        printf("Failed to connect to radio\n");
        return false;
    }

    m_bInit = true;

    if(GetInterfaceType() == CONSOLE_SIDE_INTERFACE)
    {
        m_Radio.openWritingPipe(0x10101010);
        m_Radio.openReadingPipe(1, 0x01010101);
    }
    else
    {
        m_Radio.openWritingPipe(0x01010101);
        m_Radio.openReadingPipe(1, 0x10101010);
    }

    m_Radio.setCRCLength(rf24_crclength_e::RF24_CRC_16);
    m_Radio.setAutoAck(false);
    m_Radio.setDataRate(rf24_datarate_e::RF24_2MBPS);
    m_Radio.setPALevel(RF24_PA_MIN, true);

    m_Radio.txDelay = 0; //Autoack is disabled
    m_Radio.csDelay = 1;

    m_Radio.startListening();

    return true;
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
    if(!m_bInit)
        return false;

    gpio_put(GetDevicePinMap()->wirelessTX, true);

    m_Radio.stopListening();

    if(!m_Radio.writeFast(pBuf, len))
        printf("WriteFast failed?\n");

    if(!m_Radio.txStandBy())
        printf("Standby failed?\n");

    m_Radio.startListening();

    gpio_put(GetDevicePinMap()->wirelessTX, false);
}

uint8_t WirelessComm::Read(void *pBuf, uint8_t len)
{
    if(!m_bInit)
        return 0;

    if(m_Radio.available())
    {
        gpio_put(GetDevicePinMap()->wirelessRX, true);

        m_Radio.read(pBuf, len);

        gpio_put(GetDevicePinMap()->wirelessRX, false);
        return len;
    }

    return 0;
}

void WirelessComm::Sleep()
{
}

void WirelessComm::Wake()
{
}

void WirelessComm::PairingProcess()
{
}
