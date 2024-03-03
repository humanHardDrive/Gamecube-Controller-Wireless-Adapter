#include "ControllerComm.h"

#include "ControllerDefs.h"
#include "Utils.h"

#define NUM_CONTROLLERS 4

static ControllerInfo aControllerInfo[NUM_CONTROLLERS];

void ControllerComm_Init()
{
    for(uint8_t i = 0; i < NUM_CONTROLLERS; i++)
    {
        aControllerInfo[i].isConnected = 0;
        memset(&aControllerInfo[i].values, 0, sizeof(ControllerValues));
    }
}

static void l_ControllerInterfaceBackground()
{
}

static void l_ConsoleInterfaceBackground()
{

}

void ControllerComm_Background()
{
    if(GetInterfaceType() == CONTROLLER_SIDE_INTERFACE)
        l_ControllerInterfaceBackground();
    else
        l_ConsoleInterfaceBackground();
}