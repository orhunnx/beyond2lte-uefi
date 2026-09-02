#ifndef B2L_TOUCH_UI_H_
#define B2L_TOUCH_UI_H_

#include <Uefi.h>

typedef struct {
  CONST CHAR16  *Label;
  CONST CHAR16  *Detail;
  BOOLEAN        Dangerous;
} B2L_TOUCH_UI_OPTION;

EFI_STATUS
B2LTouchUiInitialize (
  VOID
  );

EFI_STATUS
B2LTouchUiChoose (
  IN  CONST CHAR16               *Title,
  IN  CONST CHAR16               *Subtitle,
  IN  CONST B2L_TOUCH_UI_OPTION  *Options,
  IN  UINTN                       OptionCount,
  IN  UINTN                       InitialSelection,
  OUT UINTN                      *Selection
  );

VOID
B2LTouchUiDrawStopButton (
  IN CONST CHAR16  *Label
  );

BOOLEAN
B2LTouchUiPollStopButton (
  VOID
  );

#endif
