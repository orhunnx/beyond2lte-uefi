/** @file
  Guarded DWC_usb31 device-event probe used by B2LUsbTest R10.
**/

#ifndef B2L_DWC3_PROBE_H_
#define B2L_DWC3_PROBE_H_

#include <Uefi.h>

#define B2L_USB_TEST_REVISION  10

EFI_STATUS
B2LDwc3RunEventProbe (
  VOID
  );

#endif
