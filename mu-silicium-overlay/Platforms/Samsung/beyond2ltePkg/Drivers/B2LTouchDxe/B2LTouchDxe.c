/** @file
  Samsung sec_ts touchscreen driver for Galaxy S10+ (beyond2lte).

  The stock device tree places sec_ts at address 0x48 on HSI2C10 and reports
  12-bit coordinates.  This DXE driver exposes the first supported contact as
  EFI_ABSOLUTE_POINTER_PROTOCOL.  It neither updates firmware nor power-cycles
  the touchscreen regulators.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Guid/EventGroup.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/B2LTouchDiagnostic.h>
#include <Protocol/EFIPmicRegulator.h>

#include "B2LHsI2c.h"
#include "B2LSecTsParser.h"

#define SEC_TS_CMD_SENSE_ON          0x10
#define SEC_TS_READ_FIRMWARE_INTEGRITY 0x21
#define SEC_TS_READ_DEVICE_ID        0x22
#define SEC_TS_CMD_SET_TOUCHFUNCTION 0x30
#define SEC_TS_READ_BOOT_STATUS      0x55
#define SEC_TS_READ_ONE_EVENT        0x60
#define SEC_TS_READ_ALL_EVENT        0x61
#define SEC_TS_CMD_CLEAR_EVENT_STACK 0x62

#define SEC_TS_STATUS_APP_MODE       0x20
#define SEC_TS_MAX_EVENT_COUNT       32
#define B2L_POINTER_QUEUE_SIZE       32
#define B2L_TOUCH_POLL_INTERVAL      200000ULL // 20 ms in 100 ns units
#define B2L_TOUCH_MAX_CONSECUTIVE_ERRORS 3

// Samsung sec_ts initialization enables mutual touch, palm and wet-mode
// reporting before SENSE_ON.  The 16-bit command payload is little-endian.
#define SEC_TS_DEFAULT_TOUCH_FUNCTIONS 0x0061

typedef struct {
  EFI_ABSOLUTE_POINTER_PROTOCOL  Protocol;
  EFI_ABSOLUTE_POINTER_MODE      Mode;
  B2L_SEC_TS_POINTER             Pointer;
  EFI_ABSOLUTE_POINTER_STATE     Queue[B2L_POINTER_QUEUE_SIZE];
  UINTN                          QueueHead;
  UINTN                          QueueTail;
  UINTN                          QueueCount;
  BOOLEAN                        Polling;
  BOOLEAN                        Disabled;
  BOOLEAN                        ExitingBootServices;
  UINTN                          ErrorCount;
  EFI_EVENT                      PollTimer;
  EFI_EVENT                      ExitBootServicesEvent;
  EFI_STATUS                     InitStatus;
  EFI_STATUS                     PowerProtocolStatus;
  EFI_STATUS                     PowerDvddStatus;
  EFI_STATUS                     PowerAvddStatus;
  EFI_STATUS                     ProbeStatus;
  EFI_STATUS                     ResetStatus;
  EFI_STATUS                     TouchFunctionStatus;
  EFI_STATUS                     PointerStatus;
  EFI_STATUS                     LastPollStatus;
  UINT64                         PollCount;
  UINT64                         RawEventCount;
  UINT64                         ParsedEventCount;
  UINT64                         PollFailureCount;
  UINT64                         EventTypeCount[4];
  UINT8                          LastEvent[B2L_SEC_TS_EVENT_SIZE];
  UINT8                          DeviceId[5];
  UINT8                          FirmwareIntegrity;
  UINT8                          BootStatus;
} B2L_TOUCH_CONTEXT;

STATIC B2L_TOUCH_CONTEXT  mTouch;

#define B2L_TOUCH_DVDD_LDO        19
#define B2L_TOUCH_AVDD_LDO        27
#define B2L_TOUCH_DVDD_SETTLE_US  1000
#define B2L_TOUCH_POWER_SETTLE_US 70000

STATIC
EFI_STATUS
EFIAPI
TouchGetDiagnostic (
  IN  B2L_TOUCH_DIAGNOSTIC_PROTOCOL  *This,
  OUT B2L_TOUCH_DIAGNOSTIC_DATA      *Data
  );

STATIC B2L_TOUCH_DIAGNOSTIC_PROTOCOL  mDiagnostic = {
  B2L_TOUCH_DIAGNOSTIC_REVISION,
  TouchGetDiagnostic
};

STATIC
EFI_STATUS
EnableTouchPower (
  IN OUT B2L_TOUCH_CONTEXT  *Context
  )
{
  EFI_PMIC_REGULATOR_PROTOCOL  *Pmic;

  Context->PowerProtocolStatus = gBS->LocateProtocol (
                                        &gEfiPmicRegulatorProtocolGuid,
                                        NULL,
                                        (VOID **)&Pmic
                                        );
  if (EFI_ERROR (Context->PowerProtocolStatus)) {
    return Context->PowerProtocolStatus;
  }

  // Stock DT maps tsp_io to S2MPS19 LDO19M and marks it boot-on.  The PMIC
  // protocol performs a read/modify/write, preserving the configured 1.8 V.
  Context->PowerDvddStatus = Pmic->SetLdo (
                                      ID_S2MPS19,
                                      B2L_TOUCH_DVDD_LDO,
                                      S2MPS_MODE_NORMAL,
                                      TRUE
                                      );
  if (EFI_ERROR (Context->PowerDvddStatus)) {
    return Context->PowerDvddStatus;
  }

  gBS->Stall (B2L_TOUCH_DVDD_SETTLE_US);

  // Stock DT maps tsp_avdd to LDO27M.  Enable only; never power-cycle it and
  // never disable either rail when leaving UEFI.
  Context->PowerAvddStatus = Pmic->SetLdo (
                                      ID_S2MPS19,
                                      B2L_TOUCH_AVDD_LDO,
                                      S2MPS_MODE_NORMAL,
                                      TRUE
                                      );
  if (EFI_ERROR (Context->PowerAvddStatus)) {
    return Context->PowerAvddStatus;
  }

  gBS->Stall (B2L_TOUCH_POWER_SETTLE_US);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadCommand (
  IN  UINT8  Command,
  OUT UINT8 *Data,
  IN  UINTN  DataLength
  )
{
  return B2LHsI2cWriteRead (&Command, 1, Data, DataLength);
}

STATIC
EFI_STATUS
WriteCommand (
  IN UINT8  Command
  )
{
  return B2LHsI2cWriteRead (&Command, 1, NULL, 0);
}

STATIC
EFI_STATUS
SetTouchFunctions (
  VOID
  )
{
  UINT8  Command[3];

  Command[0] = SEC_TS_CMD_SET_TOUCHFUNCTION;
  Command[1] = (UINT8)(SEC_TS_DEFAULT_TOUCH_FUNCTIONS & 0xFF);
  Command[2] = (UINT8)(SEC_TS_DEFAULT_TOUCH_FUNCTIONS >> 8);
  return B2LHsI2cWriteRead (Command, sizeof (Command), NULL, 0);
}

STATIC
BOOLEAN
QueueIsEmpty (
  IN CONST B2L_TOUCH_CONTEXT  *Context
  )
{
  return Context->QueueCount == 0;
}

STATIC
VOID
QueuePointerState (
  IN OUT B2L_TOUCH_CONTEXT  *Context
  )
{
  EFI_ABSOLUTE_POINTER_STATE  *State;

  if (Context->QueueCount == B2L_POINTER_QUEUE_SIZE) {
    Context->QueueHead = (Context->QueueHead + 1) % B2L_POINTER_QUEUE_SIZE;
    Context->QueueCount--;
  }

  State                = &Context->Queue[Context->QueueTail];
  State->CurrentX      = Context->Pointer.X;
  State->CurrentY      = Context->Pointer.Y;
  State->CurrentZ      = Context->Pointer.Pressure;
  State->ActiveButtons = Context->Pointer.Active ? EFI_ABSP_TouchActive : 0;

  Context->QueueTail = (Context->QueueTail + 1) % B2L_POINTER_QUEUE_SIZE;
  Context->QueueCount++;
}

STATIC
VOID
ConsumeEvent (
  IN OUT B2L_TOUCH_CONTEXT  *Context,
  IN     CONST UINT8         Event[B2L_SEC_TS_EVENT_SIZE]
  )
{
  B2L_SEC_TS_PARSE_RESULT  Result;
  UINT8                    EventId;

  CopyMem (Context->LastEvent, Event, sizeof (Context->LastEvent));
  EventId = Event[0] & 0x03;
  Context->EventTypeCount[EventId]++;
  Result = B2LSecTsParseEvent (Event, &Context->Pointer);
  Context->RawEventCount++;
  if (Result == B2LSecTsUpdated) {
    Context->ParsedEventCount++;
    QueuePointerState (Context);
  } else if (Result == B2LSecTsInvalid) {
    DEBUG ((DEBUG_WARN, "B2LTouch: malformed sec_ts coordinate event\n"));
  }
}

STATIC
EFI_STATUS
PollEvents (
  IN OUT B2L_TOUCH_CONTEXT  *Context
  )
{
  UINT8       Events[SEC_TS_MAX_EVENT_COUNT][B2L_SEC_TS_EVENT_SIZE];
  UINT8       Remaining;
  UINTN       Index;
  EFI_STATUS  Status;

  if (Context->Polling || Context->Disabled || Context->ExitingBootServices) {
    return EFI_NOT_READY;
  }

  Context->Polling = TRUE;
  Context->PollCount++;
  Status = ReadCommand (
             SEC_TS_READ_ONE_EVENT,
             Events[0],
             B2L_SEC_TS_EVENT_SIZE
             );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  // Samsung sec_ts returns an all-zero first byte when its event FIFO is empty.
  if (Events[0][0] == 0) {
    Status = EFI_SUCCESS;
    goto Exit;
  }

  Remaining = B2LSecTsRemainingEvents (Events[0]);
  if (Remaining >= SEC_TS_MAX_EVENT_COUNT) {
    DEBUG ((
      DEBUG_ERROR,
      "B2LTouch: invalid event count %u; clearing controller queue\n",
      Remaining
      ));
    WriteCommand (SEC_TS_CMD_CLEAR_EVENT_STACK);
    Status = EFI_COMPROMISED_DATA;
    goto Exit;
  }

  if (Remaining != 0) {
    Status = ReadCommand (
               SEC_TS_READ_ALL_EVENT,
               &Events[1][0],
               (UINTN)Remaining * B2L_SEC_TS_EVENT_SIZE
               );
    if (EFI_ERROR (Status)) {
      goto Exit;
    }
  }

  for (Index = 0; Index <= Remaining; Index++) {
    ConsumeEvent (Context, Events[Index]);
  }

Exit:
  Context->Polling = FALSE;
  Context->LastPollStatus = Status;
  if (EFI_ERROR (Status)) {
    Context->ErrorCount++;
    Context->PollFailureCount++;
    if ((Context->ErrorCount == 1) || ((Context->ErrorCount & 0x3F) == 0)) {
      DEBUG ((
        DEBUG_ERROR,
        "B2LTouch: event poll failed (%u): %r\n",
        (UINT32)Context->ErrorCount,
        Status
        ));
    }
  } else {
    Context->ErrorCount = 0;
  }

  return Status;
}

STATIC
EFI_STATUS
ProbeController (
  IN OUT B2L_TOUCH_CONTEXT  *Context
  )
{
  UINTN       Index;
  BOOLEAN     AllZero;
  BOOLEAN     AllOnes;
  EFI_STATUS  Status;

  Status = ReadCommand (
             SEC_TS_READ_DEVICE_ID,
             Context->DeviceId,
             sizeof (Context->DeviceId)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  AllZero = TRUE;
  AllOnes = TRUE;
  for (Index = 0; Index < sizeof (Context->DeviceId); Index++) {
    AllZero = (BOOLEAN)(AllZero && (Context->DeviceId[Index] == 0x00));
    AllOnes = (BOOLEAN)(AllOnes && (Context->DeviceId[Index] == 0xFF));
  }

  if (AllZero || AllOnes) {
    return EFI_NOT_FOUND;
  }

  Status = ReadCommand (
             SEC_TS_READ_FIRMWARE_INTEGRITY,
             &Context->FirmwareIntegrity,
             sizeof (Context->FirmwareIntegrity)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadCommand (
             SEC_TS_READ_BOOT_STATUS,
             &Context->BootStatus,
             sizeof (Context->BootStatus)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "B2LTouch: sec_ts id=%02x%02x%02x%02x%02x integrity=%02x boot=%02x\n",
    Context->DeviceId[0],
    Context->DeviceId[1],
    Context->DeviceId[2],
    Context->DeviceId[3],
    Context->DeviceId[4],
    Context->FirmwareIntegrity,
    Context->BootStatus
    ));

  if ((Context->FirmwareIntegrity & 0x80) == 0) {
    return EFI_COMPROMISED_DATA;
  }

  return (Context->BootStatus == SEC_TS_STATUS_APP_MODE) ?
         EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
TouchReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  EFI_STATUS  Status;

  if (This != &mTouch.Protocol) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&mTouch.Pointer, sizeof (mTouch.Pointer));
  mTouch.QueueHead        = 0;
  mTouch.QueueTail        = 0;
  mTouch.QueueCount       = 0;
  mTouch.ErrorCount       = 0;

  if (mTouch.Disabled || mTouch.ExitingBootServices) {
    return EFI_DEVICE_ERROR;
  }

  Status = WriteCommand (SEC_TS_CMD_CLEAR_EVENT_STACK);
  if (!EFI_ERROR (Status)) {
    Status = SetTouchFunctions ();
    mTouch.TouchFunctionStatus = Status;
  }

  if (!EFI_ERROR (Status)) {
    Status = WriteCommand (SEC_TS_CMD_SENSE_ON);
  }

  if (!EFI_ERROR (Status) && ExtendedVerification) {
    Status = ProbeController (&mTouch);
  }

  mTouch.ResetStatus = Status;
  return EFI_ERROR (Status) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TouchGetDiagnostic (
  IN  B2L_TOUCH_DIAGNOSTIC_PROTOCOL  *This,
  OUT B2L_TOUCH_DIAGNOSTIC_DATA      *Data
  )
{
  EFI_TPL  OldTpl;

  if ((This != &mDiagnostic) || (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  ZeroMem (Data, sizeof (*Data));
  Data->Revision            = B2L_TOUCH_DIAGNOSTIC_REVISION;
  Data->InitStatus          = mTouch.InitStatus;
  Data->PowerProtocolStatus = mTouch.PowerProtocolStatus;
  Data->PowerDvddStatus     = mTouch.PowerDvddStatus;
  Data->PowerAvddStatus     = mTouch.PowerAvddStatus;
  Data->ProbeStatus         = mTouch.ProbeStatus;
  Data->ResetStatus         = mTouch.ResetStatus;
  Data->TouchFunctionStatus = mTouch.TouchFunctionStatus;
  Data->PointerStatus       = mTouch.PointerStatus;
  Data->LastPollStatus      = mTouch.LastPollStatus;
  Data->PollCount           = mTouch.PollCount;
  Data->RawEventCount       = mTouch.RawEventCount;
  Data->ParsedEventCount    = mTouch.ParsedEventCount;
  Data->PollFailureCount    = mTouch.PollFailureCount;
  CopyMem (Data->EventTypeCount, mTouch.EventTypeCount,
           sizeof (Data->EventTypeCount));
  CopyMem (Data->LastEvent, mTouch.LastEvent, sizeof (Data->LastEvent));
  Data->PollErrorCount      = (UINT32)mTouch.ErrorCount;
  Data->Disabled            = mTouch.Disabled;
  Data->ExitingBootServices = mTouch.ExitingBootServices;
  CopyMem (Data->DeviceId, mTouch.DeviceId, sizeof (Data->DeviceId));
  Data->FirmwareIntegrity = mTouch.FirmwareIntegrity;
  Data->BootStatus         = mTouch.BootStatus;
  Data->TouchFunctions     = SEC_TS_DEFAULT_TOUCH_FUNCTIONS;
  B2LHsI2cPopulateDiagnostic (Data);
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TouchGetState (
  IN  EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  OUT EFI_ABSOLUTE_POINTER_STATE     *State
  )
{
  if ((This != &mTouch.Protocol) || (State == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (QueueIsEmpty (&mTouch)) {
    return EFI_NOT_READY;
  }

  CopyMem (State, &mTouch.Queue[mTouch.QueueHead], sizeof (*State));
  mTouch.QueueHead = (mTouch.QueueHead + 1) % B2L_POINTER_QUEUE_SIZE;
  mTouch.QueueCount--;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
TouchPollTimer (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  B2L_TOUCH_CONTEXT  *Touch;
  EFI_STATUS          Status;

  (VOID)Event;
  Touch = Context;
  if (Touch->Disabled || Touch->ExitingBootServices) {
    return;
  }

  Status = PollEvents (Touch);
  if (EFI_ERROR (Status)) {
    // Linux sec_ts handlers leave the device active after a failed event
    // read.  Permit isolated timeouts to recover on the next controller
    // reset, but retain a small consecutive-error bound so optional input
    // can never hold up boot indefinitely.
    if (Touch->ErrorCount >= B2L_TOUCH_MAX_CONSECUTIVE_ERRORS) {
      Touch->Disabled = TRUE;
      B2LHsI2cQuiesce ();
      gBS->SetTimer (Touch->PollTimer, TimerCancel, 0);
    }
  } else if (!QueueIsEmpty (Touch)) {
    gBS->SignalEvent (Touch->Protocol.WaitForInput);
  }
}

STATIC
VOID
EFIAPI
TouchExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  B2L_TOUCH_CONTEXT  *Touch;

  (VOID)Event;
  Touch = Context;
  Touch->ExitingBootServices = TRUE;
  Touch->Disabled            = TRUE;
  B2LHsI2cQuiesce ();
}

STATIC
VOID
EFIAPI
TouchWaitForInput (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  B2L_TOUCH_CONTEXT  *Touch;

  Touch = Context;
  if (!QueueIsEmpty (Touch)) {
    gBS->SignalEvent (Event);
  }
}

EFI_STATUS
EFIAPI
B2LTouchEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS  Status;

  ZeroMem (&mTouch, sizeof (mTouch));
  mTouch.InitStatus          = EFI_NOT_READY;
  mTouch.PowerProtocolStatus = EFI_NOT_READY;
  mTouch.PowerDvddStatus     = EFI_NOT_READY;
  mTouch.PowerAvddStatus     = EFI_NOT_READY;
  mTouch.ProbeStatus         = EFI_NOT_READY;
  mTouch.ResetStatus         = EFI_NOT_READY;
  mTouch.TouchFunctionStatus = EFI_NOT_READY;
  mTouch.PointerStatus       = EFI_NOT_READY;
  mTouch.LastPollStatus      = EFI_NOT_READY;

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gB2LTouchDiagnosticProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mDiagnostic
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mTouch.InitStatus = EnableTouchPower (&mTouch);
  if (EFI_ERROR (mTouch.InitStatus)) {
    DEBUG ((
      DEBUG_ERROR,
      "B2LTouch: touchscreen power enable failed: %r\n",
      mTouch.InitStatus
      ));
    return EFI_SUCCESS;
  }

  mTouch.InitStatus = B2LHsI2cInitialize ();
  Status = mTouch.InitStatus;
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "B2LTouch: HSI2C10 init failed: %r\n", Status));
    return EFI_SUCCESS;
  }

  mTouch.ProbeStatus = ProbeController (&mTouch);
  Status = mTouch.ProbeStatus;
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "B2LTouch: sec_ts probe failed: %r\n", Status));
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  mTouch.Mode.AbsoluteMinX = 0;
  mTouch.Mode.AbsoluteMinY = 0;
  mTouch.Mode.AbsoluteMinZ = 0;
  mTouch.Mode.AbsoluteMaxX = B2L_SEC_TS_MAX_COORD;
  mTouch.Mode.AbsoluteMaxY = B2L_SEC_TS_MAX_COORD;
  mTouch.Mode.AbsoluteMaxZ = 0x3F;
  mTouch.Mode.Attributes   = EFI_ABSP_SupportsPressureAsZ;

  mTouch.Protocol.Reset    = TouchReset;
  mTouch.Protocol.GetState = TouchGetState;
  mTouch.Protocol.Mode     = &mTouch.Mode;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  TouchWaitForInput,
                  &mTouch,
                  &mTouch.Protocol.WaitForInput
                  );
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  TouchPollTimer,
                  &mTouch,
                  &mTouch.PollTimer
                  );
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    gBS->CloseEvent (mTouch.Protocol.WaitForInput);
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  TouchExitBootServices,
                  &mTouch,
                  &gEfiEventExitBootServicesGuid,
                  &mTouch.ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    gBS->CloseEvent (mTouch.PollTimer);
    gBS->CloseEvent (mTouch.Protocol.WaitForInput);
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  Status = TouchReset (&mTouch.Protocol, FALSE);
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    gBS->CloseEvent (mTouch.ExitBootServicesEvent);
    gBS->CloseEvent (mTouch.PollTimer);
    gBS->CloseEvent (mTouch.Protocol.WaitForInput);
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gEfiAbsolutePointerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mTouch.Protocol
                  );
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    gBS->CloseEvent (mTouch.ExitBootServicesEvent);
    gBS->CloseEvent (mTouch.PollTimer);
    gBS->CloseEvent (mTouch.Protocol.WaitForInput);
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  Status = gBS->SetTimer (
                  mTouch.PollTimer,
                  TimerPeriodic,
                  B2L_TOUCH_POLL_INTERVAL
                  );
  if (EFI_ERROR (Status)) {
    mTouch.PointerStatus = Status;
    gBS->UninstallProtocolInterface (
           ImageHandle,
           &gEfiAbsolutePointerProtocolGuid,
           &mTouch.Protocol
           );
    gBS->CloseEvent (mTouch.ExitBootServicesEvent);
    gBS->CloseEvent (mTouch.PollTimer);
    gBS->CloseEvent (mTouch.Protocol.WaitForInput);
    B2LHsI2cQuiesce ();
    return EFI_SUCCESS;
  }

  mTouch.PointerStatus = EFI_SUCCESS;
  DEBUG ((DEBUG_INFO, "B2LTouch: EFI Absolute Pointer installed\n"));
  return EFI_SUCCESS;
}
