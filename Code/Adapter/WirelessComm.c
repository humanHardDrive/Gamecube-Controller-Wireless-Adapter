#include "WirelessComm.h"

#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "PinDefs.h"

//Commands
#define READ_REG_CMD    0
#define WRITE_REG_CMD   0b00100000
#define READ_RX_CMD     0b01100001
#define WRITE_TX_CMD    0b10100000
#define ACTIVATE_CMD    0b01010000

//Register map
#define REG_RF_CH       0x05

static uint8_t nPairingState = PAIR_STATE_UNKNOWN;

static void l_ReadRegister(uint8_t nRegister, uint8_t* pData, uint8_t len)
{

}

static void l_WriteRegister(uint8_t nRegister, uint8_t* pData, uint8_t len)
{

}

static void l_SendData(uint8_t* pData, uint8_t len)
{

}

static void l_ReadData(uint8_t* pData, uint8_t len)
{

}

static void l_SetChannel(uint8_t nChannel)
{
    uint8_t data[2] = {0};
    data[0] = WRITE_REG_CMD | REG_RF_CH;
    data[1] = nChannel;

    gpio_put(NRF_CS_PIN, 0);
    spi_write_blocking(spi_default, data, 2);
    gpio_put(NRF_CS_PIN, 1);
}

static void l_SetPower(uint8_t nPower)
{
    
}

void WirelessComm_Init()
{
    //Setup pins
    gpio_init(NRF_CS_PIN);
    gpio_init(NRF_CE_PIN);
    gpio_init(NRF_IRQ_PIN);

    gpio_put(NRF_CS_PIN, 1);

    //Setup SPI
    spi_init(spi_default, 10000000); //NRF supports 10MHz
    gpio_set_function(SPI0_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI0_CLK_PIN, GPIO_FUNC_SPI);
}

uint8_t WirelessComm_GetPairState()
{
    return nPairingState;
}

uint8_t WirelessComm_GetPairStatus()
{
    return 0;
}

void WirelessComm_Pair()
{
    if(nPairingState == PAIR_STATE_PAIRING)
    {

    }
    else
        nPairingState = PAIR_STATE_PAIRING;
}

void WirelessComm_Background()
{

}