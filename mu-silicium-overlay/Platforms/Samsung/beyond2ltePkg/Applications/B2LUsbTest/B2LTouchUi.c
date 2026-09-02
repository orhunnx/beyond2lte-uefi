/** @file
  Large-target touchscreen UI for B2LUsbTest.

  This module consumes only the EFI Absolute Pointer protocol already exposed
  by B2LTouchDxe.  It performs no direct HSI2C, GPIO or MMIO access.  Every
  action is committed on a press-and-release inside the same rectangle; stale
  input is drained before a new screen is armed.  Volume, power and special
  keys remain available as a recovery path.
**/

#include <Uefi.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include "B2LTouchUi.h"

#define B2L_TOUCH_UI_MAX_OPTIONS  4
#define B2L_TOUCH_UI_MARGIN_DIV   16

typedef struct {
  UINTN  X;
  UINTN  Y;
  UINTN  Width;
  UINTN  Height;
} B2L_TOUCH_UI_RECT;

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  *mPointer;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL   *mGop;
STATIC BOOLEAN                         mInitialized;
STATIC B2L_TOUCH_UI_RECT               mOptionRect[B2L_TOUCH_UI_MAX_OPTIONS];
STATIC B2L_TOUCH_UI_RECT               mStopRect;
STATIC BOOLEAN                         mStopVisible;
STATIC BOOLEAN                         mStopArmed;
STATIC BOOLEAN                         mStopWasActive;
STATIC BOOLEAN                         mStopPressedInside;

STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBlack = {
  0x00, 0x00, 0x00, 0x00
};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBlue = {
  0xA0, 0x58, 0x18, 0x00
};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mSelected = {
  0x20, 0xA0, 0x20, 0x00
};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mDanger = {
  0x20, 0x20, 0xB0, 0x00
};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMuted = {
  0x48, 0x48, 0x48, 0x00
};

STATIC
UINTN
TextLength (
  IN CONST CHAR16  *Text
  )
{
  UINTN  Length;

  if (Text == NULL) {
    return 0;
  }
  for (Length = 0; Text[Length] != L'\0'; Length++) {
  }
  return Length;
}

STATIC
VOID
Fill (
  IN CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color,
  IN CONST B2L_TOUCH_UI_RECT              *Rect
  )
{
  if ((mGop == NULL) || (Rect->Width == 0) || (Rect->Height == 0)) {
    return;
  }
  mGop->Blt (
          mGop,
          (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(VOID *)Color,
          EfiBltVideoFill,
          0,
          0,
          Rect->X,
          Rect->Y,
          Rect->Width,
          Rect->Height,
          0
          );
}

STATIC
VOID
PrintCentered (
  IN CONST CHAR16  *Text,
  IN UINTN          PixelY,
  IN UINTN          Attribute
  )
{
  EFI_STATUS  Status;
  UINTN       Columns;
  UINTN       Rows;
  UINTN       Column;
  UINTN       Row;
  UINTN       Length;

  if ((Text == NULL) || (gST->ConOut == NULL)) {
    return;
  }
  Columns = 80;
  Rows    = 25;
  Status  = gST->ConOut->QueryMode (
                            gST->ConOut,
                            gST->ConOut->Mode->Mode,
                            &Columns,
                            &Rows
                            );
  if (EFI_ERROR (Status) || (Columns == 0) || (Rows == 0)) {
    Columns = 80;
    Rows    = 25;
  }

  Length = TextLength (Text);
  Column = (Length < Columns) ? (Columns - Length) / 2 : 0;
  if ((mGop != NULL) && (mGop->Mode->Info->VerticalResolution != 0)) {
    Row = (PixelY * Rows) / mGop->Mode->Info->VerticalResolution;
  } else {
    Row = PixelY;
  }
  if (Row >= Rows) {
    Row = Rows - 1;
  }
  gST->ConOut->SetAttribute (gST->ConOut, Attribute);
  gST->ConOut->SetCursorPosition (gST->ConOut, Column, Row);
  Print (L"%s", Text);
}

STATIC
BOOLEAN
PointerToPixel (
  IN  CONST EFI_ABSOLUTE_POINTER_STATE  *State,
  OUT UINTN                             *X,
  OUT UINTN                             *Y
  )
{
  UINT64  RangeX;
  UINT64  RangeY;
  UINT64  PointerX;
  UINT64  PointerY;

  if ((mPointer == NULL) || (mGop == NULL) || (mPointer->Mode == NULL) ||
      (State == NULL) || (X == NULL) || (Y == NULL)) {
    return FALSE;
  }
  RangeX = mPointer->Mode->AbsoluteMaxX - mPointer->Mode->AbsoluteMinX;
  RangeY = mPointer->Mode->AbsoluteMaxY - mPointer->Mode->AbsoluteMinY;
  if ((RangeX == 0) || (RangeY == 0)) {
    return FALSE;
  }

  PointerX = (State->CurrentX <= mPointer->Mode->AbsoluteMinX) ? 0 :
             State->CurrentX - mPointer->Mode->AbsoluteMinX;
  PointerY = (State->CurrentY <= mPointer->Mode->AbsoluteMinY) ? 0 :
             State->CurrentY - mPointer->Mode->AbsoluteMinY;
  if (PointerX > RangeX) {
    PointerX = RangeX;
  }
  if (PointerY > RangeY) {
    PointerY = RangeY;
  }

  *X = (UINTN)((PointerX * (mGop->Mode->Info->HorizontalResolution - 1ULL)) /
               RangeX);
  *Y = (UINTN)((PointerY * (mGop->Mode->Info->VerticalResolution - 1ULL)) /
               RangeY);
  return TRUE;
}

STATIC
BOOLEAN
PointInRect (
  IN UINTN                     X,
  IN UINTN                     Y,
  IN CONST B2L_TOUCH_UI_RECT  *Rect
  )
{
  return (X >= Rect->X) && (Y >= Rect->Y) &&
         (X < (Rect->X + Rect->Width)) &&
         (Y < (Rect->Y + Rect->Height));
}

STATIC
UINTN
HitOption (
  IN CONST EFI_ABSOLUTE_POINTER_STATE  *State,
  IN UINTN                              OptionCount
  )
{
  UINTN  Index;
  UINTN  X;
  UINTN  Y;

  if (!PointerToPixel (State, &X, &Y)) {
    return MAX_UINTN;
  }
  for (Index = 0; Index < OptionCount; Index++) {
    if (PointInRect (X, Y, &mOptionRect[Index])) {
      return Index;
    }
  }
  return MAX_UINTN;
}

STATIC
BOOLEAN
DrainPointer (
  VOID
  )
{
  EFI_ABSOLUTE_POINTER_STATE  State;
  BOOLEAN                     Active;

  Active = FALSE;
  if (mPointer == NULL) {
    return FALSE;
  }
  while (!EFI_ERROR (mPointer->GetState (mPointer, &State))) {
    Active = (State.ActiveButtons & EFI_ABSP_TouchActive) != 0;
  }
  return Active;
}

STATIC
VOID
DrawChoiceScreen (
  IN CONST CHAR16               *Title,
  IN CONST CHAR16               *Subtitle,
  IN CONST B2L_TOUCH_UI_OPTION  *Options,
  IN UINTN                       OptionCount,
  IN UINTN                       Selected
  )
{
  B2L_TOUCH_UI_RECT  Screen;
  UINTN              AvailableHeight;
  UINTN              ButtonHeight;
  UINTN              Gap;
  UINTN              Index;
  UINTN              Margin;
  UINTN              Top;
  CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color;

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK));
  if (mGop == NULL) {
    Print (L"%s\n%s\n\n", Title, Subtitle);
    for (Index = 0; Index < OptionCount; Index++) {
      Print (L"%c %s - %s\n", (Index == Selected) ? L'>' : L' ',
             Options[Index].Label, Options[Index].Detail);
    }
    Print (L"\nSES +/-: sec   POWER: onay   BIXBY/POWER uzun: geri\n");
    return;
  }

  Screen.X      = 0;
  Screen.Y      = 0;
  Screen.Width  = mGop->Mode->Info->HorizontalResolution;
  Screen.Height = mGop->Mode->Info->VerticalResolution;
  Fill (&mBlack, &Screen);

  Margin          = Screen.Width / B2L_TOUCH_UI_MARGIN_DIV;
  Top             = Screen.Height / 5;
  AvailableHeight = (Screen.Height * 3) / 5;
  Gap             = Screen.Height / 50;
  ButtonHeight    = (AvailableHeight - ((OptionCount - 1) * Gap)) /
                    OptionCount;

  PrintCentered (Title, Screen.Height / 20,
                 EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK));
  PrintCentered (Subtitle, Screen.Height / 9,
                 EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));

  for (Index = 0; Index < OptionCount; Index++) {
    mOptionRect[Index].X      = Margin;
    mOptionRect[Index].Y      = Top + (Index * (ButtonHeight + Gap));
    mOptionRect[Index].Width  = Screen.Width - (2 * Margin);
    mOptionRect[Index].Height = ButtonHeight;
    Color = Options[Index].Dangerous ? &mDanger : &mBlue;
    if (Index == Selected) {
      Color = &mSelected;
    }
    Fill (Color, &mOptionRect[Index]);
    PrintCentered (
      Options[Index].Label,
      mOptionRect[Index].Y + (mOptionRect[Index].Height / 2) -
      (Screen.Height / 80),
      EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)
      );
    PrintCentered (
      Options[Index].Detail,
      mOptionRect[Index].Y + (mOptionRect[Index].Height / 2) +
      (Screen.Height / 40),
      EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
      );
  }

  PrintCentered (
    L"Dokunarak sec  |  SES +/- + POWER  |  BIXBY/POWER uzun: geri",
    (Screen.Height * 93) / 100,
    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
    );
}

EFI_STATUS
B2LTouchUiInitialize (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mInitialized) {
    return (mPointer != NULL) ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  mInitialized = TRUE;

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&mGop
                  );
  if (EFI_ERROR (Status)) {
    mGop = NULL;
  }
  Status = gBS->LocateProtocol (
                  &gEfiAbsolutePointerProtocolGuid,
                  NULL,
                  (VOID **)&mPointer
                  );
  if (EFI_ERROR (Status)) {
    mPointer = NULL;
    return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
B2LTouchUiChoose (
  IN  CONST CHAR16               *Title,
  IN  CONST CHAR16               *Subtitle,
  IN  CONST B2L_TOUCH_UI_OPTION  *Options,
  IN  UINTN                       OptionCount,
  IN  UINTN                       InitialSelection,
  OUT UINTN                      *Selection
  )
{
  EFI_ABSOLUTE_POINTER_STATE  State;
  EFI_EVENT                   Events[2];
  EFI_INPUT_KEY               Key;
  EFI_STATUS                  Status;
  UINTN                       Current;
  UINTN                       EventCount;
  UINTN                       EventIndex;
  UINTN                       KeyEventIndex;
  UINTN                       Hit;
  UINTN                       Pressed;
  BOOLEAN                     Armed;
  BOOLEAN                     Active;
  BOOLEAN                     WasActive;

  if ((Title == NULL) || (Subtitle == NULL) || (Options == NULL) ||
      (Selection == NULL) || (OptionCount == 0) ||
      (OptionCount > B2L_TOUCH_UI_MAX_OPTIONS) ||
      (InitialSelection >= OptionCount)) {
    return EFI_INVALID_PARAMETER;
  }
  B2LTouchUiInitialize ();
  mStopVisible = FALSE;
  Current      = InitialSelection;
  Pressed      = MAX_UINTN;
  WasActive    = DrainPointer ();
  Armed        = !WasActive;
  DrawChoiceScreen (Title, Subtitle, Options, OptionCount, Current);

  EventCount    = 0;
  KeyEventIndex = MAX_UINTN;
  if ((mPointer != NULL) && (mPointer->WaitForInput != NULL)) {
    Events[EventCount++] = mPointer->WaitForInput;
  }
  if ((gST->ConIn != NULL) && (gST->ConIn->WaitForKey != NULL)) {
    KeyEventIndex          = EventCount;
    Events[EventCount++]   = gST->ConIn->WaitForKey;
  }
  if (EventCount == 0) {
    return EFI_UNSUPPORTED;
  }

  for (;;) {
    Status = gBS->WaitForEvent (EventCount, Events, &EventIndex);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (EventIndex == KeyEventIndex) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (EFI_ERROR (Status)) {
        continue;
      }
      if (Key.ScanCode == SCAN_ESC) {
        return EFI_ABORTED;
      }
      if (Key.ScanCode == SCAN_UP) {
        Current = (Current == 0) ? OptionCount - 1 : Current - 1;
        DrawChoiceScreen (Title, Subtitle, Options, OptionCount, Current);
      } else if (Key.ScanCode == SCAN_DOWN) {
        Current = (Current + 1) % OptionCount;
        DrawChoiceScreen (Title, Subtitle, Options, OptionCount, Current);
      } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        *Selection = Current;
        return EFI_SUCCESS;
      }
      continue;
    }

    while (!EFI_ERROR (mPointer->GetState (mPointer, &State))) {
      Active = (State.ActiveButtons & EFI_ABSP_TouchActive) != 0;
      Hit    = HitOption (&State, OptionCount);
      if (!Armed) {
        if (!Active) {
          Armed = TRUE;
        }
        WasActive = Active;
        continue;
      }
      if (Active) {
        if (!WasActive) {
          Pressed = Hit;
          if (Pressed != MAX_UINTN) {
            Current = Pressed;
            DrawChoiceScreen (Title, Subtitle, Options, OptionCount, Current);
          }
        } else if (Hit != Pressed) {
          Pressed = MAX_UINTN;
        }
      } else if (WasActive) {
        if ((Pressed != MAX_UINTN) && (Hit == Pressed)) {
          *Selection = Pressed;
          return EFI_SUCCESS;
        }
        Pressed = MAX_UINTN;
      }
      WasActive = Active;
    }
  }
}

VOID
B2LTouchUiDrawStopButton (
  IN CONST CHAR16  *Label
  )
{
  B2L_TOUCH_UI_RECT  Screen;

  if ((Label == NULL) || EFI_ERROR (B2LTouchUiInitialize ()) ||
      (mGop == NULL)) {
    return;
  }
  Screen.Width  = mGop->Mode->Info->HorizontalResolution;
  Screen.Height = mGop->Mode->Info->VerticalResolution;
  mStopRect.X      = Screen.Width / B2L_TOUCH_UI_MARGIN_DIV;
  mStopRect.Y      = (Screen.Height * 78) / 100;
  mStopRect.Width  = Screen.Width - (2 * mStopRect.X);
  mStopRect.Height = (Screen.Height * 14) / 100;
  Fill (&mMuted, &mStopRect);
  PrintCentered (
    Label,
    mStopRect.Y + (mStopRect.Height / 2),
    EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)
    );

  if (!mStopVisible) {
    mStopWasActive     = DrainPointer ();
    mStopArmed         = !mStopWasActive;
    mStopPressedInside = FALSE;
  }
  mStopVisible = TRUE;
}

BOOLEAN
B2LTouchUiPollStopButton (
  VOID
  )
{
  EFI_ABSOLUTE_POINTER_STATE  State;
  UINTN                       X;
  UINTN                       Y;
  BOOLEAN                     Active;
  BOOLEAN                     Inside;

  if (!mStopVisible || (mPointer == NULL)) {
    return FALSE;
  }
  while (!EFI_ERROR (mPointer->GetState (mPointer, &State))) {
    Active = (State.ActiveButtons & EFI_ABSP_TouchActive) != 0;
    Inside = PointerToPixel (&State, &X, &Y) && PointInRect (X, Y, &mStopRect);
    if (!mStopArmed) {
      if (!Active) {
        mStopArmed = TRUE;
      }
      mStopWasActive = Active;
      continue;
    }
    if (Active) {
      if (!mStopWasActive) {
        mStopPressedInside = Inside;
      } else if (!Inside) {
        mStopPressedInside = FALSE;
      }
    } else if (mStopWasActive) {
      if (mStopPressedInside && Inside) {
        mStopPressedInside = FALSE;
        mStopWasActive     = FALSE;
        return TRUE;
      }
      mStopPressedInside = FALSE;
    }
    mStopWasActive = Active;
  }
  return FALSE;
}
