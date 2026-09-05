#include "b2lmax77705.h"

// Samsung exynos9820 ALIVE GPA1 bank / exynos7-wakeup-eint.
// pinctrl-samsung.c sets eint_base=virt_base[0]; GPA1 eint_offset=4.
#define MAX_GPA1_CON  0x020UL
#define MAX_GPA1_DAT  0x024UL
#define MAX_GPA1_PUD  0x028UL
#define MAX_GPA1_DRV  0x02CUL
#define MAX_EINT_CON  0x704UL
#define MAX_EINT_MASK 0x904UL
#define MAX_EINT_PEND 0xA04UL
#define MAX_PIN_BIT   (1UL << 6)
#define MAX_PIN_FIELD (0xFUL << 24)

static ULONG
B2lMaxGpioRead(PB2L_MAX_CONTEXT Context, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Context->AliveRegisters + Offset));
}

static VOID
B2lMaxGpioWrite(PB2L_MAX_CONTEXT Context, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->AliveRegisters + Offset), Value);
}

VOID
B2lMaxGpioMask(PB2L_MAX_CONTEXT Context)
{
    B2lMaxGpioWrite(Context, MAX_EINT_MASK,
        B2lMaxGpioRead(Context, MAX_EINT_MASK) | MAX_PIN_BIT);
}

VOID
B2lMaxGpioUnmask(PB2L_MAX_CONTEXT Context)
{
    // exynos_irq_unmask(): level pending is W1C immediately before unmask.
    // Do not acknowledge other ALIVE pins, or modify the GIC distributor.
    B2lMaxGpioWrite(Context, MAX_EINT_PEND, MAX_PIN_BIT);
    B2lMaxGpioWrite(Context, MAX_EINT_MASK,
        B2lMaxGpioRead(Context, MAX_EINT_MASK) & ~MAX_PIN_BIT);
}

NTSTATUS
B2lMaxPrepareGpio(PB2L_MAX_CONTEXT Context)
{
    if (Context->AliveRegisters == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    B2lMaxGpioMask(Context);
    // DT gpa1-6: FUNC=0xf, PUD=0, DRV=0. LOW EINT type encodes as 0.
    B2lMaxGpioWrite(Context, MAX_GPA1_CON,
        B2lMaxGpioRead(Context, MAX_GPA1_CON) | MAX_PIN_FIELD);
    B2lMaxGpioWrite(Context, MAX_GPA1_PUD,
        B2lMaxGpioRead(Context, MAX_GPA1_PUD) & ~MAX_PIN_FIELD);
    B2lMaxGpioWrite(Context, MAX_GPA1_DRV,
        B2lMaxGpioRead(Context, MAX_GPA1_DRV) & ~MAX_PIN_FIELD);
    B2lMaxGpioWrite(Context, MAX_EINT_CON,
        B2lMaxGpioRead(Context, MAX_EINT_CON) & ~MAX_PIN_FIELD);
    if (((B2lMaxGpioRead(Context, MAX_GPA1_CON) & MAX_PIN_FIELD) != MAX_PIN_FIELD) ||
        ((B2lMaxGpioRead(Context, MAX_GPA1_PUD) & MAX_PIN_FIELD) != 0UL) ||
        ((B2lMaxGpioRead(Context, MAX_GPA1_DRV) & MAX_PIN_FIELD) != 0UL) ||
        ((B2lMaxGpioRead(Context, MAX_EINT_CON) & MAX_PIN_FIELD) != 0UL)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
B2lMaxInitOpcodeRequest(PB2L_MAX_OPCODE_REQUEST Request, UCHAR Opcode,
    UCHAR WriteLength, UCHAR WriteData, UCHAR ReadLength)
{
    RtlZeroMemory(Request, sizeof(*Request));
    KeInitializeEvent(&Request->Completion, SynchronizationEvent, FALSE);
    Request->Opcode = Opcode;
    Request->WriteLength = WriteLength;
    Request->WriteData = WriteData;
    Request->ReadLength = ReadLength;
    Request->Status = STATUS_PENDING;
}

VOID
B2lMaxCancelOpcodesLocked(PB2L_MAX_CONTEXT Context, NTSTATUS Status)
{
    PB2L_MAX_OPCODE_REQUEST Request;

    // All queue links and request lifetimes are protected by MfdLock, also
    // supplied as the passive interrupt's WaitLock. Never cancel as success.
    NT_ASSERT(!NT_SUCCESS(Status));
    Context->MfdFault = Status;
    while ((Request = Context->OpcodeHead) != NULL) {
        Context->OpcodeHead = Request->Next;
        Request->Next = NULL;
        Request->Status = Status;
        Request->Complete = TRUE;
        (VOID)KeSetEvent(&Request->Completion, IO_NO_INCREMENT, FALSE);
    }
    Context->OpcodeTail = NULL;
}

static NTSTATUS
B2lMaxStartOpcodeLocked(PB2L_MAX_CONTEXT Context)
{
    PB2L_MAX_OPCODE_REQUEST Request = Context->OpcodeHead;
    UCHAR Command[2];
    NTSTATUS Status;

    if (Request == NULL) {
        return STATUS_SUCCESS;
    }
    Command[0] = Request->Opcode;
    Command[1] = Request->WriteData;
    Request->SentAt = KeQueryInterruptTime();
    Status = B2lMaxWriteBlock(Context, B2L_MAX_USBC_ADDRESS,
        0x21U, Command, 1UL + Request->WriteLength);
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxWriteByte(Context, B2L_MAX_USBC_ADDRESS, 0x41U, 0U);
    }
    if (!NT_SUCCESS(Status)) {
        B2lMaxCancelOpcodesLocked(Context, Status);
    }
    return Status;
}

NTSTATUS
B2lMaxQueueOpcodeLocked(PB2L_MAX_CONTEXT Context, PB2L_MAX_OPCODE_REQUEST Request)
{
    if (!NT_SUCCESS(Context->MfdFault) || !Context->MfdReady) {
        return !NT_SUCCESS(Context->MfdFault) ? Context->MfdFault : STATUS_DEVICE_NOT_READY;
    }
    if ((Request->WriteLength > 1U) || (Request->ReadLength > 1U)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Context->OpcodeTail != NULL) {
        Context->OpcodeTail->Next = Request;
    } else {
        Context->OpcodeHead = Request;
    }
    Context->OpcodeTail = Request;
    return Context->OpcodeHead == Request ? B2lMaxStartOpcodeLocked(Context) : STATUS_SUCCESS;
}

NTSTATUS
B2lMaxWaitOpcode(PB2L_MAX_CONTEXT Context, PB2L_MAX_OPCODE_REQUEST Request)
{
    LARGE_INTEGER Remaining;
    NTSTATUS Status;
    NTSTATUS WaitStatus = STATUS_SUCCESS;
    ULONGLONG Now;
    ULONGLONG Deadline = KeQueryInterruptTime() + B2L_MAX_OPCODE_TIMEOUT_100NS;

    for (;;) {
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        if (Request->Complete) {
            Status = Request->Status;
            WdfWaitLockRelease(Context->MfdLock);
            return Status;
        }
        Now = KeQueryInterruptTime();
        if (Request->SentAt != 0ULL) {
            Deadline = Request->SentAt + B2L_MAX_OPCODE_TIMEOUT_100NS;
        }
        if ((Now >= Deadline) || (WaitStatus != STATUS_SUCCESS)) {
            Status = ((WaitStatus == STATUS_SUCCESS) || (WaitStatus == STATUS_TIMEOUT)) ?
                STATUS_IO_TIMEOUT : WaitStatus;
            // A mailbox without a verified response cannot be reused as if
            // the command succeeded. Cancel every linked caller before return.
            // SYSMSG/firmware-queue recovery is a separate, not yet ported path.
            B2lMaxCancelOpcodesLocked(Context, Status);
            WdfWaitLockRelease(Context->MfdLock);
            return Status;
        }
        Remaining.QuadPart = -(LONGLONG)(Deadline - Now);
        WdfWaitLockRelease(Context->MfdLock);
        WaitStatus = KeWaitForSingleObject(&Request->Completion,
            Executive, KernelMode, FALSE, &Remaining);
    }
}

static NTSTATUS
B2lMaxCompleteOpcodeLocked(PB2L_MAX_CONTEXT Context)
{
    PB2L_MAX_OPCODE_REQUEST Request = Context->OpcodeHead;
    UCHAR Response[2];
    NTSTATUS Status;

    if (Request == NULL) {
        ++Context->OpcodeUnexpected;
        return STATUS_SUCCESS; // no request is completed by an unsolicited IRQ
    }
    Status = B2lMaxReadBlock(Context, B2L_MAX_USBC_ADDRESS,
        0x51U, Response, 1UL + Request->ReadLength);
    if (NT_SUCCESS(Status) && (Response[0] != Request->Opcode)) {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (!NT_SUCCESS(Status)) {
        B2lMaxCancelOpcodesLocked(Context, Status);
        return Status;
    }
    Context->OpcodeHead = Request->Next;
    if (Context->OpcodeHead == NULL) {
        Context->OpcodeTail = NULL;
    }
    Request->Next = NULL;
    if (Request->ReadLength != 0U) {
        Request->ResponseData = Response[1];
    }
    Request->Status = STATUS_SUCCESS;
    Request->Complete = TRUE;
    ++Context->OpcodeCompletions;
    (VOID)KeSetEvent(&Request->Completion, IO_NO_INCREMENT, FALSE);
    // Samsung APCMD handler advances the queue; a timer never consumes 0x51.
    return B2lMaxStartOpcodeLocked(Context);
}

NTSTATUS
B2lMaxEvtMaxInterruptEnable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    WDF_INTERRUPT_INFO Info;

    WDF_INTERRUPT_INFO_INIT(&Info);
    WdfInterruptGetInfo(Interrupt, &Info);
    if (Info.Vector != Context->MaxTranslatedIrq) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Context->MaxIrqEnabled = TRUE;
    // MFD probe prepares the mailbox/masks before unmasking GPA1-6.
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtMaxInterruptDisable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    UNREFERENCED_PARAMETER(Interrupt);
    if (Context->AliveRegisters != NULL) {
        B2lMaxGpioMask(Context);
    }
    Context->MaxIrqEnabled = FALSE;
    Context->MfdReady = FALSE;
    if (Context->OpcodeHead != NULL) {
        B2lMaxCancelOpcodesLocked(Context, STATUS_DEVICE_NOT_READY);
    }
    return STATUS_SUCCESS;
}

BOOLEAN
B2lMaxEvtMaxInterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(WdfInterruptGetDevice(Interrupt));
    B2L_MAX_IRQ_FRAME Frame;
    ULONG Index;
    UCHAR Masks[4];
    UCHAR Sysmsg;
    UCHAR BcStatus, CcStatus;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(MessageId);
    // PassiveHandling=TRUE, AutomaticSerialization=FALSE, WaitLock=MfdLock.
    // This ISR may wait for HSI2C's independent DIRQL ISR/DPC. It never takes
    // BusLock, which the policy caller may hold while waiting for APCMDRES.
    if (!Context->MaxIrqEnabled || !Context->MfdReady ||
        ((B2lMaxGpioRead(Context, MAX_EINT_PEND) & MAX_PIN_BIT) == 0UL) ||
        ((B2lMaxGpioRead(Context, MAX_EINT_MASK) & MAX_PIN_BIT) != 0UL)) {
        return FALSE;
    }
    B2lMaxGpioMask(Context);
    ++Context->MaxInterruptCount;
    RtlZeroMemory(&Frame, sizeof(Frame));
    Frame.Time = KeQueryInterruptTime();
    Frame.SnapshotStatus = STATUS_DEVICE_NOT_READY;
    Status = B2lMaxReadByte(Context, B2L_MAX_TOP_ADDRESS, 0x22U, &Frame.Top);
    if (!NT_SUCCESS(Status)) {
        goto Failed;
    }
    if ((Frame.Top & 0x01U) != 0U) {
        // PASS2+ charger interrupt: BYP is required for OTG overcurrent.
        Status = B2lMaxReadByte(Context, B2L_MAX_CHARGER_ADDRESS, 0xB0U, &Frame.Charger);
        if (!NT_SUCCESS(Status)) {
            goto Failed;
        }
    }
    if ((Frame.Top & 0x08U) != 0U) {
        // The only runtime owner of read-to-clear USBC 0x02..0x05.
        Status = B2lMaxReadBlock(Context, B2L_MAX_USBC_ADDRESS, 0x02U, Frame.Raw, 4UL);
        if (!NT_SUCCESS(Status)) {
            goto Failed;
        }
        // Stock PASS2+ VBUSDet handling: with VBUS low and CC disconnected,
        // re-dispatch CCSTAT so detach is not lost. Retain RAW bytes exactly;
        // a separate flag records this Linux rule, not an invented IRQ bit.
        if ((Frame.Raw[0] & 0x20U) != 0U) {
            Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x08U, &BcStatus);
            if (NT_SUCCESS(Status)) {
                Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x0AU, &CcStatus);
            }
            if (!NT_SUCCESS(Status)) goto Failed;
            Frame.DetachRecheck = (BOOLEAN)(((BcStatus & 0x80U) == 0U) &&
                ((CcStatus & B2L_MAX_CC_STATUS_MASK) == B2L_MAX_CC_NO_CONNECTION));
        }
        // Linux irq_masks_cur tracks registered nested handlers. Direct
        // probe writes of PD=1c/VDM=ff do not change that software vector.
        Masks[0] = 0x00U;
        Masks[1] = 0x20U;
        Masks[2] = 0x00U;
        Masks[3] = 0xF0U;
        for (Index = 0UL; Index < 4UL; ++Index) {
            Frame.Active[Index] = (UCHAR)(Frame.Raw[Index] & (UCHAR)~Masks[Index]);
        }
        Frame.SnapshotAttempted = TRUE;
        Frame.SnapshotStatus = B2lMaxReadBlock(Context, B2L_MAX_USBC_ADDRESS,
            0x06U, Frame.Status, 8UL);
        if (!NT_SUCCESS(Frame.SnapshotStatus)) {
            RtlZeroMemory(Frame.Status, sizeof(Frame.Status));
        }
        // APCMD is first in Samsung's nested-IRQ table, before CC and PD.
        if ((Frame.Active[0] & 0x80U) != 0U) {
            Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS, 0x07U, &Sysmsg);
            if (NT_SUCCESS(Status)) {
                Status = B2lMaxCompleteOpcodeLocked(Context);
            }
            if (!NT_SUCCESS(Status)) {
                goto Failed;
            }
        }

    }
    Status = STATUS_SUCCESS;
    goto Record;

Failed:
    // Preserve the actual error and retire waiters. Do not invent a CC state
    // or use a timer to drain a failed IRQ. The pin remains masked on failure.
    // Record even a partial frame: a later opcode failure must not discard
    // the read-to-clear bytes already consumed from this physical interrupt.
Record:
    if (Context->IrqFrameCount < B2L_MAX_IRQ_FRAMES) {
        Index = (Context->IrqFrameRead + Context->IrqFrameCount) % B2L_MAX_IRQ_FRAMES;
        Context->IrqFrames[Index] = Frame;
        ++Context->IrqFrameCount;
    } else {
        ++Context->IrqFrameOverflows;
        if (NT_SUCCESS(Status)) Status = STATUS_BUFFER_OVERFLOW;
    }
    Context->MaxLastInterruptStatus = (ULONG)Status;
    if (!NT_SUCCESS(Status)) {
        ++Context->MaxInterruptFailures;
        B2lMaxCancelOpcodesLocked(Context, Status);
    }
    if (Context->PolicyEnabled) {
        WdfWorkItemEnqueue(Context->PolicyWork);
    }
    if (NT_SUCCESS(Status)) B2lMaxGpioUnmask(Context);
    return TRUE;
}
