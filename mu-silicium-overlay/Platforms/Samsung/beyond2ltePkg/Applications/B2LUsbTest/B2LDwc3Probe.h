/** @file
  Guarded Exynos9820 HS PHY and DWC_usb31 event probe used by B2LUsbTest R15.
**/

#ifndef B2L_DWC3_PROBE_H_
#define B2L_DWC3_PROBE_H_

#include <Uefi.h>

#define B2L_USB_TEST_REVISION  15

EFI_STATUS
B2LDwc3RunEventProbe (
  VOID
  );

EFI_STATUS
B2LDwc3RunCacheMassStorage (
  IN EFI_HANDLE  ImageHandle
  );

#endif
