#include "USBComm.h"

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_descriptors.h"

USBComm::USBComm()
{
  m_nNextSentReportID = m_nLastSentReportID = REPORT_ID_COUNT;
}

void USBComm::Background()
{
  //If a report hasn't been sent in a while, try priming the pump
  if(absolute_time_diff_us(m_lastReportTime, get_absolute_time()) > 100000)
  {
    m_nNextSentReportID = m_nLastSentReportID = REPORT_ID_COUNT;
  }
  else if(m_nNextSentReportID == m_nLastSentReportID)
  {
    if(tud_hid_ready())
    {
      m_nNextSentReportID++;
      if(m_nNextSentReportID >= REPORT_ID_COUNT)
        m_nNextSentReportID = REPORT_ID_GAMEPAD_1;

      tud_hid_report(m_nNextSentReportID, &m_aGamepadReport[m_nNextSentReportID], sizeof(hid_gamepad_report_t));
    }
  }
}

void USBComm::SetReport(uint8_t nReportID, hid_gamepad_report_t *pReport)
{
  if(nReportID >= REPORT_ID_COUNT)
    return;
  
  if(!pReport)
    return;

  memcpy(&m_aGamepadReport[nReportID], pReport, sizeof(hid_gamepad_report_t));
}

void USBComm::SetLastSentReportID(uint8_t nReportID)
{
  m_nLastSentReportID = nReportID;
  m_lastReportTime = get_absolute_time();
}
