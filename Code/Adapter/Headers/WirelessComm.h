#pragma once

#include "RF24.h"

enum class PAIRING_STATE
{
    UNKNOWN,
    ACTIVE,
    COMPLETED
};

class WirelessComm
{
public:
    WirelessComm();

    void Init();
    void Background();

    void Pair();
    PAIRING_STATE PairingState() {return m_PairingState;}
    bool IsPaired();

private:
    PAIRING_STATE m_PairingState = PAIRING_STATE::UNKNOWN;

    RF24 m_Radio;
    SPI m_SPIBus;

    void PairingProcess();
};