#pragma once

class ModuleTemplate
{
public:
    virtual bool Init() = 0;
    virtual void Background() = 0;

    virtual void Sleep() = 0;
    virtual void Wake() = 0;
};