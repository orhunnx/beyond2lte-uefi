/** @file
  Guarded Exynos9820 HS PHY and DWC_usb31 event probe used by B2LUsbTest R12.
**/

#ifndef B2L_DWC3_PROBE_H_
#define B2L_DWC3_PROBE_H_

#include <Uefi.h>

#define B2L_USB_TEST_REVISION  12

EFI_STATUS
B2LDwc3RunEventProbe (
  VOID
  );

#endif
