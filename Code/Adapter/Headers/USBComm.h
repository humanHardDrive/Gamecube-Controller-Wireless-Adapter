#ifndef __USB_COMM_H__
#define __USB_COMM_H__

#include <stdint.h>
#include <stdlib.h>

#include "tusb.h"
#include "usb_descriptors.h"

class USBComm
{
    public:
    USBComm();
    ~USBComm() = default;

    void Background();

    void SetReport(uint8_t nReportID, hid_gamepad_report_t* pReport);

    void SetLastSentReportID(uint8_t nReportID);

private:
    hid_gamepad_report_t m_aGamepadReport[REPORT_ID_COUNT];
    uint8_t m_nLastSentReportID, m_nNextSentReportID;
    absolute_time_t m_lastReportTime;
};

void hid_task();

#endif