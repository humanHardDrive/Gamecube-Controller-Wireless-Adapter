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

    //The tiny USB examples for HID reports shows them being handled asynchronously using the callback
    //This is handled through an interrupt and could cause issues with how the rest of the code updates controller structures
    //To get around this, only the last sent report ID is updated through the callback
    //The USBComm background method checks if the last sent report ID has updated then uses that at the trigger to send the next report 
    void SetReport(uint8_t nReportID, hid_gamepad_report_t* pReport);

    void SetLastSentReportID(uint8_t nReportID);

private:
    hid_gamepad_report_t m_aGamepadReport[REPORT_ID_COUNT];
    uint8_t m_nLastSentReportID, m_nNextSentReportID;
    absolute_time_t m_lastReportTime;
};

void hid_task();

#endif