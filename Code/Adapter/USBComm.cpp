#include "USBComm.h"

#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_descriptors.h"

static absolute_time_t lastReportTime = 0;

void send_hid_report(uint8_t reportID);

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void)instance;
  (void)len;

  uint8_t nNextReportID = report[0] + 1u;

  if(nNextReportID < REPORT_ID_COUNT)
    send_hid_report(nNextReportID);
  else
    send_hid_report(REPORT_ID_GAMEPAD_1);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

void hid_task()
{
  //If a report hasn't been sent in a while, try priming the pump
  if(absolute_time_diff_us(lastReportTime, get_absolute_time()) > 100000)
    send_hid_report(REPORT_ID_GAMEPAD_1);
}

void send_hid_report(uint8_t reportID)
{
  if(!tud_hid_ready()) return;

  lastReportTime = get_absolute_time();

  switch(reportID)
  {
      case REPORT_ID_GAMEPAD_1:
      break;

      case REPORT_ID_GAMEPAD_2:
      break;

      case REPORT_ID_GAMEPAD_3:
      break;

      case REPORT_ID_GAMEPAD_4:
      break;
  }
}