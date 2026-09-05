#include "b2lmax77705.h"

// Samsung max77705_pdmsg_irq / max77705_psrdy_irq. These methods run under
// the passive MAX ISR's MfdLock; APCMD owns all mailbox response consumption.
// PDMSG names are taken from max77705.h at d2s d1d4fb4aa21d.
#define MAX_PD_CURRENT_SRCCAP 0x30U

VOID
B2lMaxCancelPdRecoveryLocked(PB2L_MAX_CONTEXT Context, NTSTATUS Status)
{
    if (Context->PdRecoveryPending) {
        Context->PdRecoveryPending = FALSE;
        Context->PdRecoveryStatus = Status;
        Context->DiagnosticDirty = TRUE;
    }
    (VOID)WdfTimerStop(Context->PdRecoveryTimer, FALSE);
}

static VOID
B2lMaxInvalidatePdCapsLocked(PB2L_MAX_CONTEXT Context)
{
    ++Context->PdGeneration;
    Context->PdCapsValid = FALSE;
    Context->PsRdyReceived = FALSE;
    Context->PdPdoCount = Context->PdCurrentPdo = Context->PdCapsHeader = 0U;
    RtlZeroMemory(Context->PdPdos, sizeof(Context->PdPdos));
    Context->PdCapsStatus = STATUS_DEVICE_NOT_READY;
    Context->DiagnosticDirty = TRUE;
}

VOID
B2lMaxResetPdConnectionLocked(PB2L_MAX_CONTEXT Context)
{
    B2lMaxInvalidatePdCapsLocked(Context);
    Context->PdSupported = FALSE;
    Context->PdRequestRetry = FALSE;
    Context->PdPrSwap = 0xFFU;
    Context->PsRdyRaw = 0U;
    B2lMaxCancelPdRecoveryLocked(Context, STATUS_CANCELLED);
    // In-flight opcode requests still own their slots. Their old generation
    // cannot publish a contract for a later cable; APCMD/cancel retires them.
}

static VOID
B2lMaxSchedulePdRecoveryLocked(PB2L_MAX_CONTEXT Context, ULONG DelayMs)
{
    // schedule_delayed_work does not postpone an already pending recovery.
    if (!Context->PdRecoveryPending) {
        Context->PdRecoveryPending = TRUE;
        Context->PdRecoveryStatus = STATUS_PENDING;
        Context->PdRecoveryDeadline = KeQueryInterruptTime() + (ULONGLONG)DelayMs * 10000ULL;
        (VOID)WdfTimerStart(Context->PdRecoveryTimer, -(LONGLONG)DelayMs * 10000LL);
    }
}

VOID
B2lMaxEvtPdRecoveryTimer(WDFTIMER Timer)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext((WDFDEVICE)WdfTimerGetParentObject(Timer));
    ULONGLONG Now;
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Now = KeQueryInterruptTime();
    if (Context->PdRecoveryPending) {
        if (Now < Context->PdRecoveryDeadline) {
            (VOID)WdfTimerStart(Timer, -(LONGLONG)(Context->PdRecoveryDeadline - Now));
        } else {
            Context->PdRecoveryPending = FALSE;
            // Stock vbus_control_hard_reset rechecks current_pr == SRC.
            // Windows also forbids access during PnP shutdown/IRQ failure.
            Context->PdRecoveryStatus = STATUS_DEVICE_NOT_READY;
            if (Context->PolicyEnabled && Context->MfdReady && NT_SUCCESS(Context->MfdFault) &&
                Context->PowerRoleValid && Context->PowerRole == B2L_MAX_CC_SOURCE &&
                !Context->WaterDetected) {
                ++Context->PdRecoveryAttempts;
                Context->PdRecoveryStatus = B2lMaxEnableOtg(Context);
            }
            Context->DiagnosticStage = B2L_MAX_STAGE_PD_MESSAGE;
            Context->DiagnosticStatus = Context->PdRecoveryStatus;
            Context->DiagnosticDirty = TRUE;
            if (Context->PolicyEnabled) WdfWorkItemEnqueue(Context->PolicyWork);
        }
    }
    WdfWaitLockRelease(Context->MfdLock);
}

NTSTATUS
B2lMaxHandlePdMessageLocked(PB2L_MAX_CONTEXT Context)
{
    UCHAR Message;
    NTSTATUS Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x0CU, &Message);
    if (!NT_SUCCESS(Status)) {
        Context->PdMessageStatus = Status;
        return Status;
    }
    Context->PdMessage = Message;
    ++Context->PdMessageEvents;
    switch (Message) {
    case 0x06U: // Source_PD_SenderResponseTimer_Timeout
        Status = B2lMaxDisableOtg(Context);
        if (NT_SUCCESS(Status)) B2lMaxSchedulePdRecoveryLocked(Context, 800UL);
        else B2lMaxCancelPdRecoveryLocked(Context, Status);
        break;
    case 0x16U: // HARDRESET_RECEIVED
    case 0x19U: // HARDRESET_SENT
        if (Context->PowerRoleValid && Context->PowerRole == B2L_MAX_CC_SOURCE) {
            Status = B2lMaxDisableOtg(Context);
            if (NT_SUCCESS(Status)) B2lMaxSchedulePdRecoveryLocked(Context, 760UL);
            else B2lMaxCancelPdRecoveryLocked(Context, Status);
        }
        break;
    case 0x18U: // Get_Vbus_turn_off
    case 0x1AU: // PRSWAP_SRCTOSWAP
    case 0x1BU: // PRSWAP_SWAPTOSNK
        B2lMaxCancelPdRecoveryLocked(Context, STATUS_CANCELLED);
        Status = B2lMaxDisableOtg(Context);
        break;
    case 0x1DU: // PRSWAP_SWAPTOSRC: firmware's real power sequencing request
        B2lMaxCancelPdRecoveryLocked(Context, STATUS_CANCELLED);
        Status = Context->PowerRoleValid && !Context->WaterDetected ?
            B2lMaxEnableOtg(Context) : STATUS_DEVICE_NOT_READY;
        // Power/data role is still updated only by its own CC/DATAROLE IRQ.
        break;
    case 0x14U: Context->PdPrSwap = B2L_MAX_CC_SOURCE; break;
    case 0x15U: Context->PdPrSwap = B2L_MAX_CC_SINK; break;
    case 0x1CU: // PRSWAP_SNKTOSWAP resets the sink PDO/battery state.
        B2lMaxInvalidatePdCapsLocked(Context);
        // Samsung's CHGINSEL property calls change_charge_path(cable_type),
        // not an unconditional clear of CNFG12 bit5. The battery/wireless
        // cable-type consumer is not yet ported, so do not guess that write.
        ++Context->PdUnhandledEvents;
        Status = STATUS_NOT_SUPPORTED;
        break;
    case 0x00U: case 0x01U: case 0x02U: case 0x03U: case 0x05U:
    case 0x07U: case 0x08U: case 0x09U: case 0x0AU: case 0x10U:
    case 0x11U: case 0x17U: case 0x20U: case 0x21U:
        // Explicit no-action branches in the pinned Samsung handler.
        // In particular Get_Vbus_turn_on (0x17) does NOT enable VBUS there.
        break;
    default:
        // Includes MPSM Source_PSRdy, SRC_CAP battery indication, alert,
        // current-cable JIG, and unknown messages. No false handled success.
        ++Context->PdUnhandledEvents;
        Status = STATUS_NOT_SUPPORTED;
        break;
    }
    Context->PdMessageStatus = Status;
    Context->DiagnosticDirty = TRUE;
    return Status;
}

static NTSTATUS
B2lMaxQueueCurrentPdoLocked(PB2L_MAX_CONTEXT Context)
{
    PB2L_MAX_PD_REQUEST Slot = NULL;
    ULONG Index;
    NTSTATUS Status;
    for (Index = 0UL; Index < B2L_MAX_PD_SLOTS; ++Index) {
        if (!Context->PdRequests[Index].InUse) { Slot = &Context->PdRequests[Index]; break; }
    }
    if (Slot == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    B2lMaxInitOpcodeRequest(&Slot->Request, MAX_PD_CURRENT_SRCCAP, 0U, 0U, 31U);
    Slot->Generation = Context->PdGeneration;
    Slot->InUse = TRUE;
    Slot->Request.PdRequest = Slot;
    ++Context->PdReadsPending;
    Status = B2lMaxQueueOpcodeLocked(Context, &Slot->Request);
    if (!NT_SUCCESS(Status)) {
        // A synchronous transport failure may already have retired this slot.
        if (Slot->InUse) { Slot->InUse = FALSE; --Context->PdReadsPending; }
        return Status;
    }
    ++Context->PdReadsQueued;
    return STATUS_PENDING;
}

NTSTATUS
B2lMaxHandlePsRdyLocked(PB2L_MAX_CONTEXT Context)
{
    UCHAR Value;
    NTSTATUS Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x0DU, &Value);
    if (!NT_SUCCESS(Status)) {
        Context->PsRdyStatus = Status;
        return Status;
    }
    Context->PsRdyRaw = Value;
    ++Context->PsRdyEvents;
    if ((Value & 0x10U) && Context->PowerRoleValid) {
        Context->PdSupported = TRUE;
        if (Context->PowerRole == B2L_MAX_CC_SINK) {
            B2lMaxInvalidatePdCapsLocked(Context);
            Context->PsRdyReceived = TRUE;
            Status = B2lMaxQueueCurrentPdoLocked(Context);
            Context->PdCapsStatus = Status;
        }
    }
    Context->PsRdyStatus = Status;
    Context->DiagnosticDirty = TRUE;
    return Status;
}

NTSTATUS
B2lMaxConsumeCurrentPdoLocked(PB2L_MAX_CONTEXT Context,
    const UCHAR *Response, ULONG Length, ULONGLONG Generation)
{
    ULONG Index;
    UCHAR Count = 0U, Current = 0U;
    NTSTATUS Status = STATUS_SUCCESS;
    if (Generation != Context->PdGeneration || !Context->PsRdyReceived ||
        !Context->PowerRoleValid || Context->PowerRole != B2L_MAX_CC_SINK) {
        ++Context->PdStaleResponses;
        return STATUS_CANCELLED;
    }
    Context->PdCapsValid = FALSE;
    Context->PdPdoCount = Context->PdCurrentPdo = Context->PdCapsHeader = 0U;
    RtlZeroMemory(Context->PdPdos, sizeof(Context->PdPdos));
    if (Length < 2UL) Status = STATUS_BUFFER_TOO_SMALL;
    else {
        Context->PdCapsHeader = Response[1];
        Count = (UCHAR)(Response[1] & 7U);
        Current = (UCHAR)((Response[1] >> 3) & 7U);
        if (!Count || !Current || Current > Count) Status = STATUS_DEVICE_DATA_ERROR;
        else if (Length < 2UL + 4UL * Count) Status = STATUS_BUFFER_TOO_SMALL;
    }
    Context->PdCapsStatus = Status;
    if (NT_SUCCESS(Status)) {
        Context->PdCapsValid = TRUE;
        Context->PdPdoCount = Count;
        Context->PdCurrentPdo = Current;
        for (Index = 0UL; Index < Count; ++Index) {
            const UCHAR *Pdo = &Response[2UL + 4UL * Index];
            Context->PdPdos[Index] = (ULONG)Pdo[0] | ((ULONG)Pdo[1] << 8) |
                ((ULONG)Pdo[2] << 16) | ((ULONG)Pdo[3] << 24);
        }
        // This remains a raw capability observation, not an OS contract or
        // RDO. Bytes missing from a mismatched short read are never invented.
    }
    Context->DiagnosticStage = B2L_MAX_STAGE_PD_CAPS;
    Context->DiagnosticStatus = Status;
    Context->DiagnosticDirty = TRUE;
    return Status;
}

VOID
B2lMaxRetirePdRequestLocked(PB2L_MAX_CONTEXT Context, PB2L_MAX_PD_REQUEST Slot,
    NTSTATUS Status)
{
    ++Context->PdReadCompletions;
    // Actual 0x30 is decoded by the response dispatcher before retirement.
    // Cancellation/mismatch still completes this owner with its own error.
    if (!NT_SUCCESS(Status) && Slot->Generation == Context->PdGeneration &&
        Context->PsRdyReceived && Context->PowerRoleValid &&
        Context->PowerRole == B2L_MAX_CC_SINK) {
        Context->PdCapsValid = FALSE;
        Context->PdCapsStatus = Status;
    }
    Slot->InUse = FALSE;
    --Context->PdReadsPending;
    Context->DiagnosticDirty = TRUE;
}
