#ifndef __WIRELESS_COMM_H__
#define __WIRELESS_COMM_H__

#include <stdint.h>

enum
{
    PAIR_STATE_UNKNOWN = 0,
    PAIR_STATE_PAIRING,
    PAIR_STATE_PAIRING_COMPLETE
};

void WirelessComm_Init();

void WirelessComm_Pair();
uint8_t WirelessComm_GetPairState();
uint8_t WirelessComm_GetPairStatus();

void WirelessComm_Background();


#endif