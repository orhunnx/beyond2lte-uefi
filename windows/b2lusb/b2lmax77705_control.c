#include "b2lmax77705.h"

static VOID
B2lMaxUpdateControlStatusLocked(PB2L_MAX_CONTEXT Context)
{
    if (Context->ControlPending == 0UL && Context->RecoverMask == 0UL &&
        NT_SUCCESS(Context->ControlStatus) && NT_SUCCESS(Context->MfdFault)) {
        Context->ControlStatus = STATUS_SUCCESS;
        Context->UsbcInitialized = TRUE;
    }
    Context->DiagnosticStage = Context->UsbcInitialized ? B2L_MAX_STAGE_READY : B2L_MAX_STAGE_CONTROL;
    Context->DiagnosticStatus = Context->ControlStatus;
    Context->DiagnosticDirty = TRUE;
}

// Durable ownership for Linux's asynchronous probe/init/recover commands.
// Reset may discard these at any point; no queued request points into a
// returning probe callback's stack. Only the physical APCMD retires success.
NTSTATUS
B2lMaxQueueControlLocked(PB2L_MAX_CONTEXT Context, UCHAR Kind)
{
    PB2L_MAX_CONTROL_REQUEST Control = NULL;
    ULONG Index;
    UCHAR Opcode, WriteLength = 0U, ReadLength = 1U, Data = 0U;
    NTSTATUS Status;

    switch (Kind) {
    case B2L_MAX_CONTROL_AUTOIBUS: Opcode = 0x57U; WriteLength = 1U; Data = 1U; ReadLength = 0U; break;
    case B2L_MAX_CONTROL_AUTO_VBUS: Opcode = 0x54U; WriteLength = 1U; break;
    case B2L_MAX_CONTROL_CC_WRITE: Opcode = 0x0CU; WriteLength = 1U; Data = 0x1FU; break;
    case B2L_MAX_CONTROL_CC_READ: Opcode = 0x0BU; break;
    case B2L_MAX_CONTROL_OPEN_READ:
    case B2L_MAX_CONTROL_OPEN_VERIFY: Opcode = 0x1CU; break;
    case B2L_MAX_CONTROL_OPEN_WRITE:
        Opcode = 0x1DU; WriteLength = 1U; ReadLength = 0U;
        Data = (UCHAR)(Context->Control3Before & 0xFDU); break;
    case B2L_MAX_CONTROL_GRL: Opcode = 0x70U; WriteLength = 1U; Data = 1U; ReadLength = 2U; break;
    default: return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0UL; Index < B2L_MAX_CONTROL_SLOTS; ++Index) {
        if (!Context->ControlRequests[Index].InUse) {
            Control = &Context->ControlRequests[Index];
            break;
        }
    }
    if (Control == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    B2lMaxInitOpcodeRequest(&Control->Request, Opcode, WriteLength, Data, ReadLength);
    Control->Request.ControlRequest = Control;
    Control->Kind = Kind;
    Control->InUse = TRUE;
    ++Context->ControlPending;
    if (Context->ControlStatus == STATUS_SUCCESS) Context->ControlStatus = STATUS_PENDING;
    Context->UsbcInitialized = FALSE;
    Status = B2lMaxQueueOpcodeLocked(Context, &Control->Request);
    // A send failure already canceled every linked slot. A pre-link
    // rejection must retire this one as well, without double-decrementing.
    if (!NT_SUCCESS(Status) && Control->InUse)
        B2lMaxRetireControlLocked(Context, Control, Status);
    return Status;
}

VOID
B2lMaxRetireControlLocked(PB2L_MAX_CONTEXT Context,
    PB2L_MAX_CONTROL_REQUEST Control, NTSTATUS Status)
{
    UCHAR Kind = Control->Kind, Data = Control->Request.ResponseData;
    // Release before a continuation allocates a slot. Do not access Control
    // again: QueueControl can immediately reuse it under the same MfdLock.
    Control->InUse = FALSE;
    --Context->ControlPending;
    if (NT_SUCCESS(Status)) {
        switch (Kind) {
        case B2L_MAX_CONTROL_AUTOIBUS:
            Context->AutoIbusMode = 1U;
            Context->AutoIbusConfigured = TRUE;
            break;
        case B2L_MAX_CONTROL_AUTO_VBUS:
            Context->OpcodeResponse = Data;
            Context->AutoVbusDisabled = TRUE;
            break;
        case B2L_MAX_CONTROL_CC_WRITE:
            Context->CcControlResponse = Data;
            break;
        case B2L_MAX_CONTROL_CC_READ:
            Context->CcControlReadBack = Data;
            Context->CcControlConfigured = (BOOLEAN)(Data == 0x1FU);
            if (!Context->CcControlConfigured) Status = STATUS_DEVICE_DATA_ERROR;
            break;
        case B2L_MAX_CONTROL_OPEN_READ:
            Context->Control3Before = Data;
            if (!Context->PolicyEnabled) { Status = STATUS_CANCELLED; break; }
            if ((Data & 0x02U) != 0U)
                Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_OPEN_WRITE);
            if (NT_SUCCESS(Status))
                Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_OPEN_VERIFY);
            break;
        case B2L_MAX_CONTROL_OPEN_VERIFY:
            Context->Control3After = Data;
            Context->CcOpenNormalized = (BOOLEAN)((Data & 0x02U) == 0U);
            if (!Context->CcOpenNormalized) Status = STATUS_DEVICE_DATA_ERROR;
            break;
        case B2L_MAX_CONTROL_GRL:
            if (!Context->PolicyEnabled) { Status = STATUS_CANCELLED; break; }
            // CONFIG_MAX77705_GRL_ENABLE: max77705_set_forcetrimi.
            // Neither the SYSMSG nor the command send permits this write.
            Status = B2lMaxWriteByte(Context, B2L_MAX_DEBUG_ADDRESS, 0x1FU, 0x04U);
            B2lMaxDelayMicroseconds(100000UL);
            break;
        default: break;
        }
    }
    if (Kind == B2L_MAX_CONTROL_GRL) Context->GrlStatus = Status;
    if (!NT_SUCCESS(Status) && NT_SUCCESS(Context->ControlStatus))
        Context->ControlStatus = Status;
    if (!NT_SUCCESS(Status) && Status != STATUS_CANCELLED && NT_SUCCESS(Context->MfdFault))
        Context->MfdFault = Status; // caller drains links before permitting another send
    B2lMaxUpdateControlStatusLocked(Context);
}

NTSTATUS
B2lMaxRecoverControlsLocked(PB2L_MAX_CONTEXT Context)
{
    ULONG Mask = Context->RecoverMask;
    NTSTATUS Status = STATUS_SUCCESS;
    // Stock recover list walks opcodes in ascending order after cmd_run.
    // Bits represent unsent write nodes discarded by clear_queue, not the
    // in-flight response node. GRL builds send no firmware clear opcode.
    Context->RecoverMask = 0UL;
    if (!Context->PolicyEnabled) {
        if (Mask != 0UL) Context->ControlStatus = STATUS_CANCELLED;
        return STATUS_SUCCESS; // shutdown drains replies, not new policy work
    }
    if ((Mask & 1UL) != 0UL)
        Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_CC_WRITE);
    if (NT_SUCCESS(Status) && (Mask & 2UL) != 0UL)
        Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_AUTO_VBUS);
    if ((Mask & 4UL) != 0UL) {
        // A recover-list bit does not supply set_altmode or consumer READY.
        // Retain any real START request from reset; do not infer one here.
        Context->AltModeStatus = STATUS_DEVICE_NOT_READY;
    }
    if (Mask != 0UL) B2lMaxUpdateControlStatusLocked(Context);
    return Status;
}

NTSTATUS
B2lMaxResetUsbcLocked(PB2L_MAX_CONTEXT Context, BOOLEAN Watchdog)
{
    NTSTATUS Status;
    if (Watchdog) ++Context->WatchdogResets;
    else ++Context->PorResets;
    Context->ResetStatus = STATUS_PENDING;
    Context->ResetPriorFault = Context->MfdFault;
    Context->ResetUicRaw = 0xFFU;
    Context->ResetStage = 1U;
    // WDT only: stock turns VBUS off for the actual SOURCE power role.
    if (Watchdog && Context->PowerRoleValid && Context->PowerRole == B2L_MAX_CC_SOURCE) {
        Status = B2lMaxDisableOtg(Context);
        if (!NT_SUCCESS(Status)) goto Failed;
    }
    Context->ResetStage = 2U;
    Status = B2lMaxUpdateByte(Context, B2L_MAX_TOP_ADDRESS, 0x23U,
        0x08U, 0x08U, &Context->TopInterruptSourceMask);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->ResetStage = 3U;
    // An uncertain reset write also invalidates the old configuration proof.
    Context->UsbcInitialized = Context->AutoVbusDisabled = FALSE;
    Context->AutoIbusConfigured = Context->CcControlConfigured = FALSE;
    Context->CcOpenNormalized = Context->UsbcMasksConfigured = FALSE;
    Context->UsbcRuntimeMasksConfigured = FALSE;
    Context->MuicPathVerified = FALSE;
    Context->MuicStatus = STATUS_DEVICE_NOT_READY;
    Status = B2lMaxWriteByte(Context, B2L_MAX_USBC_ADDRESS, 0x80U, 0x0FU);
    if (!NT_SUCCESS(Status)) goto Failed;
    B2lMaxDelayMicroseconds(100000UL);
    Context->ResetStage = 4U;
    Status = B2lMaxWriteUsbcMasks(Context, 0x04U, 0x20U, 0x00U, 0xF0U);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->UsbcMasksConfigured = Context->UsbcRuntimeMasksConfigured = TRUE;
    Context->ResetStage = 5U;
    Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x02U, &Context->ResetUicRaw);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->SavedUic = (UCHAR)(Context->ResetUicRaw & 0xBFU);
    // beyond PASS2+: replay these captured bits only in a subsequent real
    // USBC IRQ frame; clear SYSMSG to prevent the source's reset loop.
    B2lMaxClearOpcodesLocked(Context);
    // This real POR/WDT reset and successful post-reset register accesses
    // establish a new mailbox epoch. Preserve the old failure separately;
    // merely receiving SYSMSG, GRL, or a timer cannot clear a bus fault.
    Context->MfdFault = STATUS_SUCCESS;
    Context->ControlStatus = STATUS_PENDING;
    Context->AltModeStartRequested = TRUE;
    Context->AltModeStatus = STATUS_DEVICE_NOT_READY;
    Context->ResetStage = 6U;
    Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_AUTO_VBUS);
    if (NT_SUCCESS(Status))
        Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_CC_WRITE);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->ResetStage = 7U;
    Status = B2lMaxUpdateByte(Context, B2L_MAX_TOP_ADDRESS, 0x23U,
        0U, 0x08U, &Context->TopInterruptSourceMask);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->ResetStage = 8U;
    // This status covers the reset/masks/enqueue/unmask sequence. The
    // separate ControlStatus remains pending until actual APCMD replies.
    Context->ResetStatus = STATUS_SUCCESS;
    return STATUS_SUCCESS;
Failed:
    Context->ResetStatus = Status;
    B2lMaxCancelOpcodesLocked(Context, Status);
    return Status;
}

NTSTATUS
B2lMaxSetGrlLocked(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;
    ++Context->GrlRequests;
    B2lMaxClearOpcodesLocked(Context);
    Context->ControlStatus = Context->GrlStatus = STATUS_PENDING;
    Status = B2lMaxQueueControlLocked(Context, B2L_MAX_CONTROL_GRL);
    if (!NT_SUCCESS(Status)) {
        Context->GrlStatus = Status;
        B2lMaxCancelOpcodesLocked(Context, Status);
    }
    return Status;
}
