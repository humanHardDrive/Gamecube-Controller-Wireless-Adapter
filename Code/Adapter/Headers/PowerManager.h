#pragma once

#include <cstdint>

#include "ModuleTemplate.h"

#define NUM_MANAGED_MODULES     2

class PowerManager : public ModuleTemplate
{
public:
    PowerManager();

    bool Init();
    void Background() {}

    void Sleep();
    void Sleep(uint32_t millis);

    void Wake() {}

    bool AddModuleToManage(ModuleTemplate* pModule);

private:
    ModuleTemplate* m_aPowerManagedModule[NUM_MANAGED_MODULES];
};