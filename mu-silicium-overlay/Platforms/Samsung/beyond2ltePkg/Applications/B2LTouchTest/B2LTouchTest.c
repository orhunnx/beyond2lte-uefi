/** @file
  Factory-style UEFI touchscreen test application for beyond2lte.

  Invoked by startup.nsh only when yap.sh --touch-test exports the binary to
  the ESP.  Running at EFI Shell application level guarantees that input and
  timer events are dispatchable.  It never scans buses or accesses memory
  directly; the diagnostic protocol reads only the known HSI2C10 device MMIO.
**/

#include <Uefi.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/B2LTouchDiagnostic.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>

#define GRID_COLUMNS       8
#define GRID_ROWS          16
#define GRID_CELL_COUNT    (GRID_COLUMNS * GRID_ROWS)
#define GRID_MARGIN        6
#define GRID_HEADER_HEIGHT 64
#define TEST_TIMEOUT       450000000ULL // 45 seconds in 100 ns units
#define PAGE_TIMEOUT       200000000ULL // 20 seconds in 100 ns units

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  *mPointer;
STATIC B2L_TOUCH_DIAGNOSTIC_PROTOCOL  *mDiagnostic;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL   *mGop;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *mConIn;
STATIC BOOLEAN                         mVisited[GRID_CELL_COUNT];
STATIC UINTN                           mVisitedCount;

// Read-only inventory copied from the merged beyond2lte Android DT aliases.
// Displaying it performs no MMIO access and sends no transaction to any bus.
STATIC CONST UINT32  mHsI2cAddress[] = {
  0x10860000, 0x10870000, 0x10880000, 0x10890000,
  0x10450000, 0x10460000, 0x10470000, 0x10480000,
  0x10490000, 0x104A0000, 0x104B0000, 0x104C0000,
  0x104D0000, 0x104E0000, 0x104F0000, 0x10500000,
  0x108A0000, 0x108B0000, 0x108C0000, 0x108D0000,
  0x108E0000, 0x108F0000, 0x10900000, 0x10910000,
  0x10920000, 0x10930000, 0x10940000, 0x10950000,
  0x10520000, 0x10530000, 0x10540000, 0x10550000,
  0x10560000, 0x10570000, 0x10580000, 0x10590000,
  0x10970000, 0x10990000, 0x109A0000, 0x15D00000,
  0x15D10000, 0x15D20000, 0x15D30000, 0x15D40000,
  0x15D50000, 0x15D60000, 0x15D70000
};

STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBlack  = { 0x00, 0x00, 0x00, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mGray   = { 0x50, 0x50, 0x50, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mGreen  = { 0x20, 0xD0, 0x20, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mYellow = { 0x00, 0xD8, 0xFF, 0x00 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mRed    = { 0x20, 0x20, 0xD0, 0x00 };

STATIC
VOID
WaitForKeyOrTimeout (
  VOID
  )
{
  EFI_EVENT      Events[2];
  EFI_EVENT      Timer;
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Count;
  UINTN          Index;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = gBS->SetTimer (Timer, TimerRelative, PAGE_TIMEOUT);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Timer);
    return;
  }

  Count           = 0;
  Events[Count++] = Timer;
  if ((mConIn != NULL) && (mConIn->WaitForKey != NULL)) {
    Events[Count++] = mConIn->WaitForKey;
  }

  Status = gBS->WaitForEvent (Count, Events, &Index);
  if (!EFI_ERROR (Status) && (Count == 2) && (Index == 1)) {
    mConIn->ReadKeyStroke (mConIn, &Key);
  }

  gBS->CloseEvent (Timer);
}

STATIC
VOID
ShowHsI2cInventory (
  VOID
  )
{
  UINTN  Bus;
  UINTN  Row;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== HSI2C HARITASI - ANDROID DT - AKTIF TARAMA YOK ===\n");
  Print (L"47 denetleyici; yalniz adres listesi. B10 dokunmatik adayi.\n\n");
  for (Row = 0; Row < 16; Row++) {
    for (Bus = Row; Bus < ARRAY_SIZE (mHsI2cAddress); Bus += 16) {
      if (Bus == 10) {
        Print (L"B%02u*=0x%08x  ", Bus, mHsI2cAddress[Bus]);
      } else {
        Print (L"B%02u =0x%08x  ", Bus, mHsI2cAddress[Bus]);
      }
    }
    Print (L"\n");
  }
  Print (L"\n*=DT: touchscreen at 48, USI=1042101C, GPP1 4/5 F2\n");
  Print (L"Tus: ayrintili dokunmatik tanisina gec (20 sn otomatik)\n");
  WaitForKeyOrTimeout ();
}

STATIC
VOID
PrintBytes (
  IN CONST UINT8  *Bytes,
  IN UINTN         Start,
  IN UINTN         End
  )
{
  UINTN  Index;

  for (Index = Start; Index < End; Index++) {
    Print (L"%02x ", Bytes[Index]);
  }
  Print (L"\n");
}

STATIC
VOID
ShowTouchDiagnostic (
  IN CONST CHAR16  *Phase
  )
{
  B2L_TOUCH_DIAGNOSTIC_DATA  Data;
  EFI_STATUS                 Status;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L TOUCH TANI REV%u: %s ===\n",
         B2L_TOUCH_DIAGNOSTIC_REVISION, Phase);
  Print (L"B10 CTRL=104B0000 USI=1042101C SLAVE=48 PIN=GPP1 4/5 F2\n");

  if (mDiagnostic == NULL) {
    Print (L"TANI PROTOKOLU YOK: B2LTouchDxe yuklenmemis.\n");
    Print (L"AbsolutePointer: %s\n", (mPointer == NULL) ? L"YOK" : L"VAR");
    Print (L"Tus: devam (20 sn otomatik)\n");
    WaitForKeyOrTimeout ();
    return;
  }

  Status = mDiagnostic->GetDiagnostic (mDiagnostic, &Data);
  if (EFI_ERROR (Status)) {
    Print (L"TANI OKUNAMADI: %r\n", Status);
    WaitForKeyOrTimeout ();
    return;
  }

  Print (L"PTR=%s INIT=%r PROBE=%r RESET=%r\n",
         (mPointer == NULL) ? L"YOK" : L"VAR",
         Data.InitStatus, Data.ProbeStatus, Data.ResetStatus);
  Print (L"PTRSTAT=%r POLL=%r DIS=%u EXIT=%u ERR=%u\n",
         Data.PointerStatus, Data.LastPollStatus, Data.Disabled,
         Data.ExitingBootServices, Data.PollErrorCount);
  Print (L"ID=%02x %02x %02x %02x %02x  FWINT=%02x BOOT=%02x\n",
         Data.DeviceId[0], Data.DeviceId[1], Data.DeviceId[2],
         Data.DeviceId[3], Data.DeviceId[4], Data.FirmwareIntegrity,
         Data.BootStatus);
  Print (L"XFER=%Lu OK=%Lu FAIL=%Lu LAST=%r\n",
         Data.TransferCount, Data.TransferSuccessCount,
         Data.TransferFailureCount, Data.LastTransferStatus);
  Print (L"CMD=%02x LEN=%u POLLS=%Lu RAW=%Lu PARSED=%Lu\n",
         Data.LastCommand, Data.LastReadLength, Data.PollCount,
         Data.RawEventCount, Data.ParsedEventCount);
  Print (L"READ00: ");
  PrintBytes (Data.LastRead, 0, 16);
  Print (L"READ16: ");
  PrintBytes (Data.LastRead, 16, B2L_TOUCH_DIAGNOSTIC_READ_MAX);
  Print (L"USI_MODE=%08x CTRL=%08x FIFO_CTRL=%08x\n",
         Data.UsiMode, Data.Ctrl, Data.FifoCtrl);
  Print (L"INT_EN=%08x INT_STAT=%08x FIFO_STAT=%08x\n",
         Data.IntEnable, Data.IntStatus, Data.FifoStatus);
  Print (L"CONF=%08x AUTO=%08x CMDREG=%08x\n",
         Data.Conf, Data.AutoConf, Data.Command);
  Print (L"TRANS=%08x ADDR=%08x\n", Data.TransferStatus, Data.Address);
  Print (L"\nFotografla. Tus: devam (20 sn otomatik)\n");
  WaitForKeyOrTimeout ();
}

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
EFI_STATUS
RunTouchTest (
  VOID
  )
{
  EFI_ABSOLUTE_POINTER_STATE  State;
  EFI_EVENT                   Events[3];
  EFI_EVENT                   TimeoutEvent;
  EFI_INPUT_KEY               Key;
  EFI_STATUS                  Status;
  UINTN                       Column;
  UINTN                       EventCount;
  UINTN                       EventIndex;
  UINTN                       KeyEventIndex;
  UINTN                       Row;
  UINTN                       PreviousColumn;
  UINTN                       PreviousRow;
  UINTN                       HighlightedColumn;
  UINTN                       HighlightedRow;
  UINTN                       TimeoutEventIndex;
  BOOLEAN                     HavePrevious;
  BOOLEAN                     HaveHighlight;

  DrawInitialScreen ();
  HavePrevious  = FALSE;
  HaveHighlight = FALSE;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &TimeoutEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->SetTimer (TimeoutEvent, TimerRelative, TEST_TIMEOUT);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TimeoutEvent);
    return Status;
  }

  EventCount            = 0;
  Events[EventCount++]  = mPointer->WaitForInput;
  KeyEventIndex         = MAX_UINTN;
  if ((mConIn != NULL) && (mConIn->WaitForKey != NULL)) {
    KeyEventIndex        = EventCount;
    Events[EventCount++] = mConIn->WaitForKey;
  }
  TimeoutEventIndex     = EventCount;
  Events[EventCount++]  = TimeoutEvent;

  for (;;) {
    Status = gBS->WaitForEvent (EventCount, Events, &EventIndex);
    if (EFI_ERROR (Status) || (EventIndex == TimeoutEventIndex)) {
      break;
    }
    if (EventIndex == KeyEventIndex) {
      mConIn->ReadKeyStroke (mConIn, &Key);
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
      gBS->CloseEvent (TimeoutEvent);
      return EFI_SUCCESS;
    }
  }

  if (HaveHighlight) {
    DrawCell (HighlightedColumn, HighlightedRow, &mGreen);
  }
  if (mVisitedCount != GRID_CELL_COUNT) {
    Fill (&mRed, GRID_MARGIN, 42,
          mGop->Mode->Info->HorizontalResolution - (2 * GRID_MARGIN), 14);
    gBS->Stall (500000);
  }
  gBS->CloseEvent (TimeoutEvent);
  return EFI_SUCCESS;
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
                  &gB2LTouchDiagnosticProtocolGuid,
                  NULL,
                  (VOID **)&mDiagnostic
                  );
  if (EFI_ERROR (Status)) {
    mDiagnostic = NULL;
  }

  Status = gBS->LocateProtocol (
                  &gEfiAbsolutePointerProtocolGuid,
                  NULL,
                  (VOID **)&mPointer
                  );
  if (EFI_ERROR (Status)) {
    mPointer = NULL;
  }

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&mGop
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ShowHsI2cInventory ();
  ShowTouchDiagnostic (L"TEST ONCESI");

  if (mPointer != NULL) {
    Status = RunTouchTest ();
  } else {
    Status = EFI_NOT_FOUND;
  }

  ShowTouchDiagnostic (L"TEST SONRASI");
  return Status;
}
