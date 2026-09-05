#include "b2lmax77705.h"

// Samsung beyond2lte: max77705_sysmsg_irq and SYSERROR_APCMD_FAIL.
// The actual G975FXXSGHWC1 boot kernel IKCONFIG, and beyond configs at
// universal9820 55f042627bb7, enable AFC and MAX77705_GRL_ENABLE.
// The latter makes 0x70 a GRL command, NOT a generic firmware queue clear.
static NTSTATUS
B2lMaxDisableAfcProtocolLocked(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status, OffStatus, LockStatus;
    ++Context->AfcClockAttempts;
    Status = B2lMaxWriteByte(Context, B2L_MAX_TOP_ADDRESS, 0xFEU, 0xC5U);
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxWriteByte(Context, B2L_MAX_DEBUG_ADDRESS, 0x0FU, 0x04U);
        // An errored write may have reached the chip: still attempt clock off.
        OffStatus = B2lMaxWriteByte(Context, B2L_MAX_DEBUG_ADDRESS, 0x0FU, 0x00U);
        if (NT_SUCCESS(Status)) Status = OffStatus;
    }
    // Always attempt relock, including an uncertain unlock/write failure.
    LockStatus = B2lMaxWriteByte(Context, B2L_MAX_TOP_ADDRESS, 0xFEU, 0x00U);
    Context->AfcRelockStatus = LockStatus;
    if (NT_SUCCESS(Status)) Status = LockStatus;
    Context->AfcClockStatus = Status;
    return Status;
}

VOID
B2lMaxHandleSysmsgLocked(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;
    BOOLEAN HadRequest;

    ++Context->SysmsgEvents;
    Context->SysmsgValid = FALSE;
    Context->SysmsgValue = 0xFFU;
    Context->SysmsgSampleCount = 0U;
    RtlZeroMemory(Context->SysmsgSamples, sizeof(Context->SysmsgSamples));
    // This is the stock bounded three-read validation of an actual SYSMSG
    // IRQ, not a polling loop. Keep all received bytes even on disagreement.
    for (Index = 0UL; Index < 3UL; ++Index) {
        Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x07U,
            &Context->SysmsgSamples[Index]);
        if (!NT_SUCCESS(Status)) {
            B2lMaxCancelOpcodesLocked(Context, Status);
            goto Done;
        }
        ++Context->SysmsgSampleCount;
    }
    if (Context->SysmsgSamples[0] != Context->SysmsgSamples[1] ||
        Context->SysmsgSamples[0] != Context->SysmsgSamples[2]) {
        ++Context->SysmsgMismatches;
        // Linux uses 0x6d only as a software diagnostic sentinel here. Do
        // not publish that sentinel as a byte received from the controller.
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Done;
    }
    Context->SysmsgValue = Context->SysmsgSamples[0];
    Context->SysmsgValid = TRUE;
    switch (Context->SysmsgValue) {
    case 0x33U: // SYSERROR_APCMD_FAIL
        HadRequest = Context->OpcodeHead != NULL;
        Context->SysmsgRetryStatus = B2lMaxRetryOpcodeLocked(Context);
        // Original AFC workaround also runs when no opcode is outstanding.
        Status = B2lMaxDisableAfcProtocolLocked(Context);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(Context->MfdFault))
            B2lMaxCancelOpcodesLocked(Context, Status);
        if (HadRequest && !NT_SUCCESS(Context->SysmsgRetryStatus)) {
            if (Context->SysmsgRetryStatus == STATUS_NOT_SUPPORTED)
                ++Context->SysmsgUnhandled;
            Status = Context->SysmsgRetryStatus;
        }
        break;
    case 0x00U: case 0x04U:
    case 0x10U: case 0x11U: case 0x12U: case 0x13U: case 0x14U: case 0x15U:
    case 0x20U: case 0x30U: case 0x31U: case 0x32U:
    case 0x68U: case 0x69U:
        // Explicit no-action branches in max77705_usbc_check_sysmsg.
        break;
    default:
        // POR/WDT, GRL and short/battery/VDM consumers still require their
        // actual reset/recovery/notification paths. Never report them handled.
        ++Context->SysmsgUnhandled;
        Status = STATUS_NOT_SUPPORTED;
        break;
    }
Done:
    Context->SysmsgStatus = Status;
    Context->DiagnosticStage = B2L_MAX_STAGE_SYSMSG;
    Context->DiagnosticStatus = Status;
    Context->DiagnosticDirty = TRUE;
}
