#include "bsp/board_api.h"
#include "tusb.h"

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)index;
    (void)langid;

    return NULL;
}

uint8_t const* tud_descriptor_device_cb()
{
    return NULL;
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return NULL;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return NULL;
}