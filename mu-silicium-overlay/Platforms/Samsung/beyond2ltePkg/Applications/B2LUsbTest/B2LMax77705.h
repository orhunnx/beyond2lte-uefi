#ifndef B2L_MAX77705_H_
#define B2L_MAX77705_H_

#include <Uefi.h>

#define B2L_USB_TEST_REVISION  8

VOID
B2LMax77705ShowGpio (
  VOID
  );

BOOLEAN
B2LMax77705ShowController (
  VOID
  );

VOID
B2LMax77705ShowData (
  VOID
  );

#endif
