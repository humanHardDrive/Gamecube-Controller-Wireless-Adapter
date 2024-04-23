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

enum
{
    POWER_DOWN_STATE = 0,
    POWERING_UP_STATE,
    STANDBY_STATE,
    RX_STATE,
    TX_STATE
};

//Register map
#define REG_CONFIG      0x00
#define MASK_RX_DR      0x40
#define MASK_TX_DS      0x20
#define MASK_MAX_RT     0x10
#define EN_CRC          0x80
#define CRCO            0x04
#define PWR_UP          0x02
#define PRIM_RX         0x01

#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_SETUP_AW    0x03
#define REG_SETUP_RETR  0x04
#define REG_RF_CH       0x05

static uint8_t nPairingState = PAIR_STATE_UNKNOWN;
static uint8_t nWirelessState = POWER_DOWN_STATE;
static absolute_time_t powerUpTime = 0;

static void l_ReadRegister(uint8_t nRegister, uint8_t* pData, uint8_t len)
{

}

static void l_WriteRegister(uint8_t nRegister, uint8_t* pData, uint8_t len)
{
    gpio_put(NRF_CS_PIN, 0);
    spi_write_blocking(spi_default, pData, len);
    gpio_put(NRF_CS_PIN, 1);
}

static void l_SendData(uint8_t* pData, uint8_t len)
{

}

static void l_ReadData(uint8_t* pData, uint8_t len)
{

}

static void l_PowerUp()
{
    uint8_t config;

    l_ReadRegister(REG_CONFIG, &config, 1);
    config |= PWR_UP;
    l_WriteRegister(REG_CONFIG, &config, 1);
}

static void l_PowerDown()
{
    uint8_t config;
    
    l_ReadRegister(REG_CONFIG, &config, 1);
    config &= (uint8_t)~PWR_UP;
    l_WriteRegister(REG_CONFIG, &config, 1);
}

static void l_SwitchModeRX()
{

}

static void l_SwitchModeTX()
{

}

static void l_SetChannel(uint8_t nChannel)
{
    uint8_t data[2] = {0};
    data[0] = WRITE_REG_CMD | REG_RF_CH;
    data[1] = nChannel;
}

static void l_SetPower(uint8_t nPower)
{

}

static void l_ManageWirelessState()
{
    switch(nWirelessState)
    {
        case POWER_DOWN_STATE:
        l_PowerUp();

        powerUpTime = get_absolute_time();
        nWirelessState = POWERING_UP_STATE;
        break;

        case POWERING_UP_STATE:
        uint deltaTime = absolute_time_diff_us(powerUpTime, get_absolute_time());
        break;

        case STANDBY_STATE:
        break;

        case TX_STATE:
        break;

        case RX_STATE:
        //Do nothing
        break;
    }
}

void WirelessComm_Init()
{
    //Setup pins
    gpio_init(NRF_CS_PIN);
    gpio_init(NRF_CE_PIN);
    gpio_init(NRF_IRQ_PIN);

    gpio_put(NRF_CE_PIN, 0);
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