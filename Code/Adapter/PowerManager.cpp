#include "PowerManager.h"

#include "pico/stdlib.h"

#include "PinDefs.h"

PowerManager::PowerManager()
{
}

bool PowerManager::Init()
{
    gpio_init(POWER_CTRL_PIN);
    gpio_init(POWER_ON_PIN);

    gpio_set_dir(POWER_CTRL_PIN, GPIO_OUT);
    gpio_set_dir(POWER_ON_PIN, GPIO_OUT);

    gpio_put(POWER_CTRL_PIN, false);
    gpio_put(POWER_ON_PIN, true);

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
