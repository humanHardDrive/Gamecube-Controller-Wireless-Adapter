#pragma once

#include "ModuleTemplate.h"

#include "RF24.h"

#define MAX_PAYLOAD_SIZE    32

enum class PAIRING_STATE
{
    UNKNOWN,
    ACTIVE,
    COMPLETED
};

class WirelessComm : public ModuleTemplate
{
public:
    WirelessComm();

    bool Init();
    void Background();

    void Pair();
    PAIRING_STATE PairingState() {return m_PairingState;}
    bool IsPaired();

    bool Write(void* pBuf, uint8_t len);
    uint8_t Read(void* pBuf, uint8_t len);

    void Sleep();
    void Wake();

private:   
    void PairingProcess();

    PAIRING_STATE m_PairingState = PAIRING_STATE::UNKNOWN;

    RF24 m_Radio;
    SPI m_SPIBus;
};