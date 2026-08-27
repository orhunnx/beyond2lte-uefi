/** @file
  Factory-style UEFI touchscreen grid for beyond2lte.

  Enabled only by yap.sh --touch-test.  It runs once after the graphics and
  absolute-pointer protocols become available, marks every cell crossed by
  an active contact, then returns to the existing boot path.  It has no
  device MMIO or memory-map access.
**/

#include <Uefi.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>

#define GRID_COLUMNS       8
#define GRID_ROWS          16
#define GRID_CELL_COUNT    (GRID_COLUMNS * GRID_ROWS)
#define GRID_MARGIN        6
#define GRID_HEADER_HEIGHT 64
#define TEST_TICKS         4500 // 45 seconds at 10 ms

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  *mPointer;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL   *mGop;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *mConIn;
STATIC BOOLEAN                         mVisited[GRID_CELL_COUNT];
STATIC UINTN                           mVisitedCount;
STATIC BOOLEAN                         mTestRan;

STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBlack  = { 0x00, 0x00, 0x00, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mGray   = { 0x50, 0x50, 0x50, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mGreen  = { 0x20, 0xD0, 0x20, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mYellow = { 0x00, 0xD8, 0xFF, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mRed    = { 0x20, 0x20, 0xD0, 0x00 };

STATIC
VOID
Fill (
  IN CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color,
  IN UINTN                                 X,
  IN UINTN                                 Y,
  IN UINTN                                 Width,
  IN UINTN                                 Height
  )
{
  mGop->Blt (
          mGop,
          (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(VOID *)Color,
          EfiBltVideoFill,
          0,
          0,
          X,
          Y,
          Width,
          Height,
          0
          );
}

STATIC
VOID
CellRectangle (
  IN  UINTN  Column,
  IN  UINTN  Row,
  OUT UINTN *X,
  OUT UINTN *Y,
  OUT UINTN *Width,
  OUT UINTN *Height
  )
{
  UINTN  ScreenWidth;
  UINTN  ScreenHeight;
  UINTN  GridWidth;
  UINTN  GridHeight;

  ScreenWidth  = mGop->Mode->Info->HorizontalResolution;
  ScreenHeight = mGop->Mode->Info->VerticalResolution;
  GridWidth    = ScreenWidth - (2 * GRID_MARGIN);
  GridHeight   = ScreenHeight - GRID_HEADER_HEIGHT - GRID_MARGIN;

  *X      = GRID_MARGIN + ((GridWidth * Column) / GRID_COLUMNS);
  *Y      = GRID_HEADER_HEIGHT + ((GridHeight * Row) / GRID_ROWS);
  *Width  = (GRID_MARGIN + ((GridWidth * (Column + 1)) / GRID_COLUMNS)) - *X;
  *Height = (GRID_HEADER_HEIGHT + ((GridHeight * (Row + 1)) / GRID_ROWS)) - *Y;
}

STATIC
VOID
DrawCell (
  IN UINTN                                     Column,
  IN UINTN                                     Row,
  IN CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *Color
  )
{
  UINTN  X;
  UINTN  Y;
  UINTN  Width;
  UINTN  Height;

  CellRectangle (Column, Row, &X, &Y, &Width, &Height);
  Fill (Color, X, Y, Width, Height);
  if ((Width > 6) && (Height > 6) && (Color == &mGray)) {
    Fill (&mBlack, X + 3, Y + 3, Width - 6, Height - 6);
  }
}

STATIC
VOID
DrawProgress (
  IN CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color
  )
{
  UINTN  ScreenWidth;
  UINTN  CompletedWidth;

  ScreenWidth = mGop->Mode->Info->HorizontalResolution;
  Fill (&mGray, GRID_MARGIN, 42, ScreenWidth - (2 * GRID_MARGIN), 14);
  CompletedWidth = ((ScreenWidth - (2 * GRID_MARGIN)) * mVisitedCount) /
                   GRID_CELL_COUNT;
  if (CompletedWidth != 0) {
    Fill (Color, GRID_MARGIN, 42, CompletedWidth, 14);
  }
}

STATIC
VOID
MarkCell (
  IN UINTN  Column,
  IN UINTN  Row
  )
{
  UINTN  Index;

  if ((Column >= GRID_COLUMNS) || (Row >= GRID_ROWS)) {
    return;
  }

  Index = (Row * GRID_COLUMNS) + Column;
  if (!mVisited[Index]) {
    mVisited[Index] = TRUE;
    mVisitedCount++;
  }

  DrawCell (Column, Row, &mGreen);
}

STATIC
VOID
MarkLine (
  IN INTN  StartColumn,
  IN INTN  StartRow,
  IN INTN  EndColumn,
  IN INTN  EndRow
  )
{
  INTN  DeltaColumn;
  INTN  DeltaRow;
  INTN  Error;
  INTN  StepColumn;
  INTN  StepRow;
  INTN  TwiceError;

  DeltaColumn = (StartColumn < EndColumn) ? EndColumn - StartColumn : StartColumn - EndColumn;
  DeltaRow    = (StartRow < EndRow) ? EndRow - StartRow : StartRow - EndRow;
  StepColumn  = (StartColumn < EndColumn) ? 1 : -1;
  StepRow     = (StartRow < EndRow) ? 1 : -1;
  Error       = DeltaColumn - DeltaRow;

  for (;;) {
    MarkCell ((UINTN)StartColumn, (UINTN)StartRow);
    if ((StartColumn == EndColumn) && (StartRow == EndRow)) {
      break;
    }

    TwiceError = 2 * Error;
    if (TwiceError > -DeltaRow) {
      Error       -= DeltaRow;
      StartColumn += StepColumn;
    }

    if (TwiceError < DeltaColumn) {
      Error    += DeltaColumn;
      StartRow += StepRow;
    }
  }
}

STATIC
BOOLEAN
PointerToCell (
  IN  CONST EFI_ABSOLUTE_POINTER_STATE  *State,
  OUT UINTN                             *Column,
  OUT UINTN                             *Row
  )
{
  UINT64  RangeX;
  UINT64  RangeY;
  UINT64  X;
  UINT64  Y;

  if ((State->ActiveButtons & EFI_ABSP_TouchActive) == 0) {
    return FALSE;
  }

  RangeX = mPointer->Mode->AbsoluteMaxX - mPointer->Mode->AbsoluteMinX;
  RangeY = mPointer->Mode->AbsoluteMaxY - mPointer->Mode->AbsoluteMinY;
  if ((RangeX == 0) || (RangeY == 0)) {
    return FALSE;
  }

  X = (State->CurrentX <= mPointer->Mode->AbsoluteMinX) ? 0 :
      State->CurrentX - mPointer->Mode->AbsoluteMinX;
  Y = (State->CurrentY <= mPointer->Mode->AbsoluteMinY) ? 0 :
      State->CurrentY - mPointer->Mode->AbsoluteMinY;
  if (X > RangeX) {
    X = RangeX;
  }
  if (Y > RangeY) {
    Y = RangeY;
  }

  *Column = (UINTN)((X * GRID_COLUMNS) / (RangeX + 1));
  *Row    = (UINTN)((Y * GRID_ROWS) / (RangeY + 1));
  if (*Column >= GRID_COLUMNS) {
    *Column = GRID_COLUMNS - 1;
  }
  if (*Row >= GRID_ROWS) {
    *Row = GRID_ROWS - 1;
  }

  return TRUE;
}

STATIC
VOID
DrawInitialScreen (
  VOID
  )
{
  UINTN  Column;
  UINTN  Row;
  UINTN  ScreenWidth;
  UINTN  ScreenHeight;

  ScreenWidth  = mGop->Mode->Info->HorizontalResolution;
  ScreenHeight = mGop->Mode->Info->VerticalResolution;
  Fill (&mBlack, 0, 0, ScreenWidth, ScreenHeight);

  if (gST->ConOut != NULL) {
    gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
    Print (L"DOKUNMATIK TESTI - KARELERI BOYA - SES TUSU: WINDOWS\n");
  }

  for (Row = 0; Row < GRID_ROWS; Row++) {
    for (Column = 0; Column < GRID_COLUMNS; Column++) {
      DrawCell (Column, Row, &mGray);
    }
  }
  DrawProgress (&mGreen);
}

STATIC
VOID
EFIAPI
RunTouchTest (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  EFI_ABSOLUTE_POINTER_STATE  State;
  EFI_INPUT_KEY               Key;
  UINTN                       Column;
  UINTN                       Row;
  UINTN                       PreviousColumn;
  UINTN                       PreviousRow;
  UINTN                       HighlightedColumn;
  UINTN                       HighlightedRow;
  UINTN                       Tick;
  BOOLEAN                     HavePrevious;
  BOOLEAN                     HaveHighlight;

  (VOID)Event;
  (VOID)Context;
  if (mTestRan) {
    return;
  }
  mTestRan = TRUE;

  DrawInitialScreen ();
  HavePrevious  = FALSE;
  HaveHighlight = FALSE;

  for (Tick = 0; Tick < TEST_TICKS; Tick++) {
    if ((mConIn != NULL) && !EFI_ERROR (mConIn->ReadKeyStroke (mConIn, &Key))) {
      break;
    }

    while (!EFI_ERROR (mPointer->GetState (mPointer, &State))) {
      if (PointerToCell (&State, &Column, &Row)) {
        if (HaveHighlight) {
          DrawCell (HighlightedColumn, HighlightedRow, &mGreen);
        }
        if (HavePrevious) {
          MarkLine (
            (INTN)PreviousColumn,
            (INTN)PreviousRow,
            (INTN)Column,
            (INTN)Row
            );
        } else {
          MarkCell (Column, Row);
        }
        DrawCell (Column, Row, &mYellow);
        PreviousColumn    = Column;
        PreviousRow       = Row;
        HighlightedColumn = Column;
        HighlightedRow    = Row;
        HavePrevious      = TRUE;
        HaveHighlight     = TRUE;
        DrawProgress (&mGreen);
      } else {
        if (HaveHighlight) {
          DrawCell (HighlightedColumn, HighlightedRow, &mGreen);
          HaveHighlight = FALSE;
        }
        HavePrevious = FALSE;
      }
    }

    if (mVisitedCount == GRID_CELL_COUNT) {
      Fill (&mGreen, GRID_MARGIN, 42,
            mGop->Mode->Info->HorizontalResolution - (2 * GRID_MARGIN), 14);
      gBS->Stall (1000000);
      return;
    }

    gBS->Stall (10000);
  }

  if (HaveHighlight) {
    DrawCell (HighlightedColumn, HighlightedRow, &mGreen);
  }
  if (mVisitedCount != GRID_CELL_COUNT) {
    Fill (&mRed, GRID_MARGIN, 42,
          mGop->Mode->Info->HorizontalResolution - (2 * GRID_MARGIN), 14);
    gBS->Stall (500000);
  }
}

EFI_STATUS
EFIAPI
B2LTouchTestEntry (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  mConIn = SystemTable->ConIn;

  Status = gBS->LocateProtocol (
                  &gEfiAbsolutePointerProtocolGuid,
                  NULL,
                  (VOID **)&mPointer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&mGop
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  RunTouchTest (NULL, NULL);
  return EFI_SUCCESS;
}
