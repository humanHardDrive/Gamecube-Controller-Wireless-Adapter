#pragma once

#include "ModuleTemplate.h"

class PowerManager : public ModuleTemplate
{
public:
    PowerManager();

    bool Init();
    void Background() {}

    void Sleep();

private:
};