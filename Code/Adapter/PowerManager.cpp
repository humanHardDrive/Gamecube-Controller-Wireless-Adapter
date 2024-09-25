#include "PowerManager.h"

#include "pico/stdlib.h"

#include "Utils.h"

PowerManager::PowerManager()
{
}

bool PowerManager::Init()
{
    gpio_init(GetDevicePinMap()->powerOn);
    gpio_init(GetDevicePinMap()->powerLED);
    gpio_init(GetDevicePinMap()->vbusDetect);

    gpio_set_dir(GetDevicePinMap()->powerOn, GPIO_OUT);
    gpio_set_dir(GetDevicePinMap()->powerLED, GPIO_OUT);
    gpio_set_dir(GetDevicePinMap()->vbusDetect, GPIO_IN);

    gpio_put(GetDevicePinMap()->powerOn, true);
    gpio_put(GetDevicePinMap()->powerLED, true);

    return true;
}

void PowerManager::Sleep()
{
    Sleep(100);
}

void PowerManager::Sleep(uint32_t millis)
{
    for(size_t i = 0; i < NUM_MANAGED_MODULES; i++)
    {
        if(m_aPowerManagedModule[i])
            m_aPowerManagedModule[i]->Sleep();
    }

    sleep_ms(millis);

    for(size_t i = 0; i < NUM_MANAGED_MODULES; i++)
    {
        if(m_aPowerManagedModule[i])
            m_aPowerManagedModule[i]->Wake();
    }    
}

bool PowerManager::AddModuleToManage(ModuleTemplate *pModule)
{
    for(size_t i = 0; i < NUM_MANAGED_MODULES; i++)
    {
        if(!m_aPowerManagedModule[i])
        {
            m_aPowerManagedModule[i] = pModule;
            return true;
        }
    }

    return false;
}