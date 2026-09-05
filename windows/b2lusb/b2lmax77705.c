#include "b2lmax77705.h"

#define MAX77705_TOP_PMIC_ID1      0x00U
#define MAX77705_TOP_PMIC_REV      0x01U
#define MAX77705_TOP_INTSRC        0x22U
#define MAX77705_TOP_INTSRC_MASK   0x23U

#define MAX77705_USBC_HW_REV       0x00U
#define MAX77705_USBC_FW_REV       0x01U
#define MAX77705_USBC_STATUS1      0x06U
#define MAX77705_USBC_BC_STATUS    0x08U
#define MAX77705_USBC_FW_MINOR     0x09U
#define MAX77705_USBC_CC_STATUS0   0x0AU
#define MAX77705_USBC_CC_STATUS1   0x0BU
#define MAX77705_USBC_PD_STATUS1   0x0DU
#define MAX77705_PD_INT_DATAROLE   (1U << 5)
#define MAX77705_CC_INT_WATER      (1U << 4)
#define MAX77705_CC_STATUS_WATER   (1U << 1)
#define MAX77705_USBC_UIC_INT      0x02U
#define MAX77705_USBC_CC_INT       0x03U
#define MAX77705_USBC_PD_INT       0x04U
#define MAX77705_USBC_VDM_INT      0x05U
#define MAX77705_USBC_UIC_INT_MASK 0x0EU
#define MAX77705_USBC_CC_INT_MASK  0x0FU
#define MAX77705_USBC_PD_INT_MASK  0x10U
#define MAX77705_USBC_VDM_INT_MASK 0x11U
#define MAX77705_USBC_OPCODE_WRITE 0x21U
#define MAX77705_USBC_OPCODE_END   0x41U
#define MAX77705_USBC_OPCODE_READ  0x51U

#define MAX77705_USBC_INT_APCMDRES       (1U << 7)
#define MAX77705_USBC_INT_SYSMSG         (1U << 6)
#define MAX77705_CC_INT_ATTACH_ERROR    (1U << 5)
#define MAX77705_CC_INT_STATE            (1U << 0)
#define MAX77705_CC_INT_CURRENT          (1U << 2)
#define MAX77705_CC_INT_PIN_STATE        (1U << 3)
#define MAX77705_CC_INT_ATTACH_MASK      \
    (MAX77705_CC_INT_STATE | MAX77705_CC_INT_CURRENT | \
     MAX77705_CC_INT_PIN_STATE)
#define MAX77705_OPCODE_FACTORY_TEST     0x54U
#define MAX77705_FACTORY_AUTO_VBUS_OFF   0x00U
#define MAX77705_OPCODE_FW_AUTOIBUS      0x57U
#define MAX77705_AUTOIBUS_AT_OFF         0x01U
#define MAX77705_OPCODE_CCCTRL1_READ     0x0BU
#define MAX77705_OPCODE_CCCTRL1_WRITE    0x0CU
#define MAX77705_OPCODE_CONTROL1_READ    0x05U
#define MAX77705_OPCODE_CONTROL1_WRITE   0x06U
#define MAX77705_OPCODE_CONTROL3_READ    0x1CU
#define MAX77705_OPCODE_CONTROL3_WRITE   0x1DU
#define MAX77705_MUIC_COM_USB_AP         0x09U
#define MAX77705_MUIC_COM_OPEN           0x3FU
#define MAX77705_CONTROL3_LOCK_ERROR_EN  (1U << 1)
#define MAX77705_CONTROL3_LOCK_ERROR_EN_CLEAR_MASK 0xFDU
#define MAX77705_CCCTRL1_SOURCE_SINK     (1U << 4)
#define MAX77705_CCCTRL1_SINK_SOURCE     (1U << 3)
#define MAX77705_CCCTRL1_DEBUG_ENABLE    (1U << 2)
#define MAX77705_CCCTRL1_AUDIO_ENABLE    (1U << 1)
#define MAX77705_CCCTRL1_DETECT_ENABLE   (1U << 0)
#define MAX77705_CCCTRL1_BEYOND2LTE      \
    (MAX77705_CCCTRL1_SOURCE_SINK | MAX77705_CCCTRL1_SINK_SOURCE | \
     MAX77705_CCCTRL1_DEBUG_ENABLE | MAX77705_CCCTRL1_AUDIO_ENABLE | \
     MAX77705_CCCTRL1_DETECT_ENABLE)
#define MAX77705_UIC_INT_MASK_LINUX      0x00U
#define MAX77705_CC_INT_MASK_LINUX       0x20U
#define MAX77705_PD_INT_MASK_PROBE       0x1CU
#define MAX77705_VDM_INT_MASK_PROBE      0xFFU
#define MAX77705_PD_INT_MASK_RUNTIME     0x00U
#define MAX77705_VDM_INT_MASK_RUNTIME    0xF0U
#define MAX77705_OPCODE_RESPONSE_LENGTH  2UL

#define MAX77705_CHG_INT_MASK      0xB1U
#define MAX77705_CHG_INT_OK        0xB2U
#define MAX77705_CHG_DETAILS_00    0xB3U
#define MAX77705_CHG_DETAILS_01    0xB4U
#define MAX77705_CHG_DETAILS_02    0xB5U
#define MAX77705_CHG_CNFG_00       0xB7U
#define MAX77705_CHG_CNFG_06       0xBDU

#define MAX77705_CHG_MODE_MASK     0x0FU
#define MAX77705_CHG_OTG_INT_MASK  0x51U
#define MAX77705_CHG_OTG_INT_VALUE 0x50U
#define MAX77705_CHG_PROTECT_MASK  0x0CU
#define MAX77705_CHG_PROTECT_CLEAR_MASK 0xF3U
#define MAX77705_CHG_PROTECT_OPEN  0x0CU
#define MAX77705_CHG_UNLOCK_RETRIES 10UL
#define MAX77705_CHG_UNLOCK_WAIT_US 20000UL

#define MAX77705_MODE_ALL_OFF      0x00U
#define MAX77705_MODE_BUCK         0x04U
#define MAX77705_MODE_BUCK_CHG     0x05U
#define MAX77705_MODE_BOOST_UNO    0x08U
#define MAX77705_MODE_BOOST_OTG    0x0AU
#define MAX77705_MODE_BUCK_UNO     0x0CU
#define MAX77705_MODE_BUCK_CHG_UNO 0x0DU
#define MAX77705_MODE_BUCK_OTG     0x0EU
#define MAX77705_MODE_BUCK_CHG_OTG 0x0FU

static VOID
B2lMaxDelayMicroseconds(ULONG Microseconds)
{
    LARGE_INTEGER Interval;

    // Every caller runs at PASSIVE_LEVEL through the device execution-level
    // contract.  Samsung sleeps for both waits; do not busy-spin a Windows
    // CPU for the 50 ms OTG-off settling interval.
    Interval.QuadPart = -((LONGLONG)Microseconds * 10LL);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static VOID
B2lMaxSetDiagnosticLocked(PB2L_MAX_CONTEXT Context, ULONG Stage, NTSTATUS Status)
{
    Context->DiagnosticStage = Stage;
    Context->DiagnosticStatus = Status;
    Context->DiagnosticDirty = TRUE;
}

static VOID
B2lMaxPublishDiagnostic(PB2L_MAX_CONTEXT Context, ULONG Stage, NTSTATUS Status)
{
    static const WCHAR ServicePathBuffer[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\b2lmax77705";
    static const WCHAR *Names[] = {
        L"B2LMaxStage", L"B2LMaxStatus", L"B2LMaxPmicId",
        L"B2LMaxPmicRev", L"B2LMaxUsbcHwRev", L"B2LMaxUsbcFwRev",
        L"B2LMaxUsbcFwMinor", L"B2LMaxCcStatus0", L"B2LMaxCcStatus1",
        L"B2LMaxBcStatus", L"B2LMaxChgMask", L"B2LMaxChgMode",
        L"B2LMaxChgIntOk", L"B2LMaxChgDetails0", L"B2LMaxChgDetails1",
        L"B2LMaxChgDetails2", L"B2LMaxOtgEnabled",
        L"B2LMaxAutoVbusDisabled", L"B2LMaxOpcodeResponse",
        L"B2LMaxAutoIbusConfigured", L"B2LMaxAutoIbusMode",
        L"B2LMaxHsI2cControl", L"B2LMaxHsI2cClockControl",
        L"B2LMaxHsI2cIntStatus",
        L"B2LMaxHsI2cErrorStatus", L"B2LMaxHsI2cTransferStatus",
        L"B2LMaxHsI2cFifoStatus", L"B2LMaxHsI2cTimingFs1",
        L"B2LMaxHsI2cTimingFs2", L"B2LMaxHsI2cTimingFs3",
        L"B2LMaxHsI2cConfiguration", L"B2LMaxHsI2cAutoConfiguration",
        L"B2LMaxHsI2cAddress", L"B2LMaxHsI2cUsiControl",
        L"B2LMaxHsI2cAttemptedAddress", L"B2LMaxHsI2cRetryCount",
        L"B2LMaxHsI2cFailurePhase", L"B2LMaxChgConfig06",
        L"B2LMaxChargerUnlocked", L"B2LMaxChargerUnlockAttempts",
        L"B2LMaxUicInterrupt", L"B2LMaxCcInterrupt",
        L"B2LMaxPdInterrupt", L"B2LMaxVdmInterrupt",
        L"B2LMaxPdInterruptMask", L"B2LMaxVdmInterruptMask",
        L"B2LMaxUsbcMasksConfigured", L"B2LMaxCcControlConfigured",
        L"B2LMaxCcControlResponse", L"B2LMaxOtgHardwareActive",
        L"B2LMaxMuicPathRequested", L"B2LMaxMuicControl1",
        L"B2LMaxMuicPathVerified", L"B2LMaxMuicUsbPath",
        L"B2LMaxPlatformPrepared", L"B2LMaxCmgpMuxUsi3",
        L"B2LMaxCmgpDivUsi3", L"B2LMaxCmgpQchUsi3",
        L"B2LMaxGpm12Con", L"B2LMaxGpm13Con",
        L"B2LMaxUicInterruptMask", L"B2LMaxCcInterruptMask",
        L"B2LMaxUsbcRuntimeMasksConfigured", L"B2LMaxCcControlReadBack",
        L"B2LMaxTopInterruptSource", L"B2LMaxTopInterruptSourceSeen",
        L"B2LMaxTopInterruptSourceMask", L"B2LMaxUicInterruptSeen",
        L"B2LMaxCcInterruptSeen", L"B2LMaxPdInterruptSeen",
        L"B2LMaxVdmInterruptSeen", L"B2LMaxCcState",
        L"B2LMaxPreviousCcState", L"B2LMaxCcStatusValid",
        L"B2LMaxCcStateChanged", L"B2LMaxCcSampleCount",
        L"B2LMaxCcTransitionCount", L"B2LMaxCcInterruptCount",
        L"B2LMaxCcSourceEventCount", L"B2LMaxCcLastSampleTimeLow",
        L"B2LMaxCcLastSampleTimeHigh", L"B2LMaxCcLastTransitionTimeLow",
        L"B2LMaxCcLastTransitionTimeHigh", L"B2LMaxCcOpenNormalized",
        L"B2LMaxControl3Before", L"B2LMaxControl3After",
        L"B2LMaxHsI2cReadFailures", L"B2LMaxHsI2cLastReadFailureStatus",
        L"B2LMaxHsI2cLastReadFailureLength", L"B2LMaxHsI2cLastReadFailureTransferred",
        L"B2LMaxHsI2cRawIrq", L"B2LMaxHsI2cTranslatedIrq",
        L"B2LMaxHsI2cInterruptCount", L"B2LMaxHsI2cCompletionCount",
        L"B2LMaxHsI2cLastInterruptStatus",
        L"B2LMaxMaxRawIrq", L"B2LMaxMaxTranslatedIrq", L"B2LMaxMaxInterruptCount", L"B2LMaxMaxInterruptFailures", L"B2LMaxMaxLastInterruptStatus", L"B2LMaxOpcodeCompletions", L"B2LMaxOpcodeUnexpected", L"B2LMaxIrqFrameOverflows", L"B2LMaxMfdFault",
        L"B2LMaxPowerRole", L"B2LMaxDataRole", L"B2LMaxPdStatus1",
        L"B2LMaxDataRoleStatus", L"B2LMaxDataRoleEvents", L"B2LMaxDataRoleChanges",
        L"B2LMaxWaterDetected", L"B2LMaxOvercurrentEvents", L"B2LMaxDetachRechecks",
        L"B2LMaxMuicStatus", L"B2LMaxMuicOutstanding"
    };
    ULONG Values[RTL_NUMBER_OF(Names)];
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ServicePath;
    UNICODE_STRING ValueName;
    HANDLE Key = NULL;
    ULONG Index;

    // BusLock serializes publication; MfdLock snapshots the IRQ-owned state.
    // Never hold the passive interrupt lock across registry I/O.
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    if (!NT_SUCCESS(Context->MfdFault)) {
        B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_MFD_IRQ, Context->MfdFault);
    } else if (Stage != 0UL) {
        B2lMaxSetDiagnosticLocked(Context, Stage, Status);
    }
    Stage = Context->DiagnosticStage;
    Status = Context->DiagnosticStatus;
    Values[0] = Stage;
    Values[1] = (ULONG)Status;
    Values[2] = Context->PmicId;
    Values[3] = Context->PmicRev;
    Values[4] = Context->UsbcHwRev;
    Values[5] = Context->UsbcFwRev;
    Values[6] = Context->UsbcFwMinor;
    Values[7] = Context->CcStatus0;
    Values[8] = Context->CcStatus1;
    Values[9] = Context->BcStatus;
    Values[10] = Context->ChargerIntMask;
    Values[11] = Context->ChargerMode;
    Values[12] = Context->ChargerIntOk;
    Values[13] = Context->ChargerDetails[0];
    Values[14] = Context->ChargerDetails[1];
    Values[15] = Context->ChargerDetails[2];
    Values[16] = Context->OtgEnabled ? 1UL : 0UL;
    Values[17] = Context->AutoVbusDisabled ? 1UL : 0UL;
    Values[18] = Context->OpcodeResponse;
    Values[19] = Context->AutoIbusConfigured ? 1UL : 0UL;
    Values[20] = Context->AutoIbusMode;
    Values[21] = Context->HsI2cControl;
    Values[22] = Context->HsI2cClockControl;
    Values[23] = Context->HsI2cIntStatus;
    Values[24] = Context->HsI2cErrorStatus;
    Values[25] = Context->HsI2cTransferStatus;
    Values[26] = Context->HsI2cFifoStatus;
    Values[27] = Context->HsI2cTimingFs[0];
    Values[28] = Context->HsI2cTimingFs[1];
    Values[29] = Context->HsI2cTimingFs[2];
    Values[30] = Context->HsI2cConfiguration;
    Values[31] = Context->HsI2cAutoConfiguration;
    Values[32] = Context->HsI2cAddress;
    Values[33] = Context->HsI2cUsiControl;
    Values[34] = Context->HsI2cAttemptedAddress;
    Values[35] = Context->HsI2cRetryCount;
    Values[36] = Context->HsI2cFailurePhase;
    Values[37] = Context->ChargerConfig06;
    Values[38] = Context->ChargerUnlocked ? 1UL : 0UL;
    Values[39] = Context->ChargerUnlockAttempts;
    Values[40] = Context->UicInterrupt;
    Values[41] = Context->CcInterrupt;
    Values[42] = Context->PdInterrupt;
    Values[43] = Context->VdmInterrupt;
    Values[44] = Context->PdInterruptMask;
    Values[45] = Context->VdmInterruptMask;
    Values[46] = Context->UsbcMasksConfigured ? 1UL : 0UL;
    Values[47] = Context->CcControlConfigured ? 1UL : 0UL;
    Values[48] = Context->CcControlResponse;
    Values[49] = Context->OtgHardwareActive ? 1UL : 0UL;
    Values[50] = Context->MuicPathRequested;
    Values[51] = Context->MuicControl1;
    Values[52] = Context->MuicPathVerified ? 1UL : 0UL;
    Values[53] = (Context->MuicPathVerified &&
                  (Context->MuicControl1 == MAX77705_MUIC_COM_USB_AP)) ?
        1UL : 0UL;
    Values[54] = Context->PlatformPrepared ? 1UL : 0UL;
    Values[55] = Context->CmgpMuxUsi3;
    Values[56] = Context->CmgpDivUsi3;
    Values[57] = Context->CmgpQchUsi3;
    Values[58] = Context->Gpm12Con;
    Values[59] = Context->Gpm13Con;
    Values[60] = Context->UicInterruptMask;
    Values[61] = Context->CcInterruptMask;
    Values[62] = Context->UsbcRuntimeMasksConfigured ? 1UL : 0UL;
    Values[63] = Context->CcControlReadBack;
    Values[64] = Context->TopInterruptSource;
    Values[65] = Context->TopInterruptSourceSeen;
    Values[66] = Context->TopInterruptSourceMask;
    Values[67] = Context->UicInterruptSeen;
    Values[68] = Context->CcInterruptSeen;
    Values[69] = Context->PdInterruptSeen;
    Values[70] = Context->VdmInterruptSeen;
    Values[71] = Context->CcState;
    Values[72] = Context->PreviousCcState;
    Values[73] = Context->CcStatusValid ? 1UL : 0UL;
    Values[74] = Context->CcStateChanged ? 1UL : 0UL;
    Values[75] = Context->CcSampleCount;
    Values[76] = Context->CcTransitionCount;
    Values[77] = Context->CcInterruptCount;
    Values[78] = Context->CcSourceEventCount;
    Values[79] = (ULONG)(Context->CcLastSampleTime & 0xFFFFFFFFULL);
    Values[80] = (ULONG)(Context->CcLastSampleTime >> 32);
    Values[81] = (ULONG)(Context->CcLastTransitionTime & 0xFFFFFFFFULL);
    Values[82] = (ULONG)(Context->CcLastTransitionTime >> 32);
    Values[83] = Context->CcOpenNormalized ? 1UL : 0UL;
    Values[84] = Context->Control3Before;
    Values[85] = Context->Control3After;
    Values[86] = Context->HsI2cReadFailures;
    Values[87] = Context->HsI2cLastReadFailureStatus;
    Values[88] = Context->HsI2cLastReadFailureLength;
    Values[89] = Context->HsI2cLastReadFailureTransferred;
    Values[90] = Context->HsI2cRawIrq;
    Values[91] = Context->HsI2cTranslatedIrq;
    Values[92] = Context->HsI2cInterruptCount;
    Values[93] = Context->HsI2cCompletionCount;
    Values[94] = Context->HsI2cLastInterruptStatus;
    Values[95] = (ULONG)Context->MaxRawIrq;
    Values[96] = (ULONG)Context->MaxTranslatedIrq;
    Values[97] = (ULONG)Context->MaxInterruptCount;
    Values[98] = (ULONG)Context->MaxInterruptFailures;
    Values[99] = (ULONG)Context->MaxLastInterruptStatus;
    Values[100] = (ULONG)Context->OpcodeCompletions;
    Values[101] = (ULONG)Context->OpcodeUnexpected;
    Values[102] = (ULONG)Context->IrqFrameOverflows;
    Values[103] = (ULONG)Context->MfdFault;
    Values[104] = Context->PowerRoleValid ? Context->PowerRole : 0xFFUL;
    Values[105] = Context->DataRoleValid ? Context->DataRole : 0xFFUL;
    Values[106] = Context->PdStatus1;
    Values[107] = (ULONG)Context->DataRoleStatus;
    Values[108] = Context->DataRoleEvents;
    Values[109] = Context->DataRoleChanges;
    Values[110] = Context->WaterDetected ? 1UL : 0UL;
    Values[111] = Context->OvercurrentEvents;
    Values[112] = Context->DetachRechecks;
    Values[113] = (ULONG)Context->MuicStatus;
    Values[114] = Context->MuicOutstanding;
    Context->DiagnosticDirty = FALSE;
    WdfWaitLockRelease(Context->MfdLock);

    RtlInitUnicodeString(&ServicePath, ServicePathBuffer);
    InitializeObjectAttributes(
        &Attributes,
        &ServicePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_SET_VALUE, &Attributes))) {
        return;
    }

    for (Index = 0UL; Index < RTL_NUMBER_OF(Names); ++Index) {
        RtlInitUnicodeString(&ValueName, Names[Index]);
        (VOID)ZwSetValueKey(
            Key,
            &ValueName,
            0UL,
            REG_DWORD,
            &Values[Index],
            (ULONG)sizeof(Values[Index])
            );
    }
    // A single value keeps the event header and records in one reader snapshot.
    // Capture never writes the registry inside an opcode wait loop. Retry a
    // failed publication at the next normal diagnostic publish.
    if (Context->EventLogDirty) {
        RtlInitUnicodeString(&ValueName, B2L_MAX_EVENT_VALUE);
        if (NT_SUCCESS(ZwSetValueKey(
                Key, &ValueName, 0UL, REG_BINARY,
                &Context->EventLog, (ULONG)sizeof(Context->EventLog)))) {
            Context->EventLogDirty = FALSE;
        }
    }
    ZwClose(Key);
    Context->DiagnosticPollCount = 0UL;
}

NTSTATUS
B2lMaxReadByte(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR Register,
    UCHAR *Value
    )
{
    return B2lMaxHsI2cWriteRead(
        Context,
        SlaveAddress,
        &Register,
        1UL,
        Value,
        1UL
        );
}

NTSTATUS
B2lMaxWriteByte(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR Register,
    UCHAR Value
    )
{
    UCHAR Buffer[2];

    Buffer[0] = Register;
    Buffer[1] = Value;
    return B2lMaxHsI2cWriteRead(
        Context,
        SlaveAddress,
        Buffer,
        RTL_NUMBER_OF(Buffer),
        NULL,
        0UL
        );
}

NTSTATUS
B2lMaxWriteBlock(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR Register,
    const UCHAR *Values,
    ULONG ValueCount
    )
{
    UCHAR Buffer[1UL + MAX77705_OPCODE_RESPONSE_LENGTH];

    if ((Values == NULL) || (ValueCount == 0UL) ||
        (ValueCount > MAX77705_OPCODE_RESPONSE_LENGTH)) {
        return STATUS_INVALID_PARAMETER;
    }
    Buffer[0] = Register;
    RtlCopyMemory(&Buffer[1], Values, ValueCount);
    return B2lMaxHsI2cWriteRead(
        Context,
        SlaveAddress,
        Buffer,
        ValueCount + 1UL,
        NULL,
        0UL
        );
}

NTSTATUS
B2lMaxReadBlock(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR Register,
    UCHAR *Values,
    ULONG ValueCount
    )
{
    if ((Values == NULL) || (ValueCount == 0UL)) {
        return STATUS_INVALID_PARAMETER;
    }
    return B2lMaxHsI2cWriteRead(
        Context,
        SlaveAddress,
        &Register,
        1UL,
        Values,
        ValueCount
        );
}

static VOID
B2lMaxCaptureEvent(
    PB2L_MAX_CONTEXT Context,
    ULONG Origin,
    ULONG Opcode,
    ULONG Interrupts,
    NTSTATUS TriggerStatus
    )
{
    B2L_MAX_EVENT_LOG *Log = &Context->EventLog;
    B2L_MAX_EVENT *Event;
    ULONG Index;
    NTSTATUS Status;

    // BusLock serializes all callers at PASSIVE_LEVEL. Retain the first 16
    // records permanently for this D0 session, then rotate the latest 48.
    if (Log->Count < B2L_MAX_EVENT_FIRST) {
        Index = Log->Count;
    } else {
        Index = B2L_MAX_EVENT_FIRST + Log->NextTail;
        Log->NextTail = (Log->NextTail + 1UL) % B2L_MAX_EVENT_TAIL;
    }
    if (Log->Count < B2L_MAX_EVENT_CAPACITY) {
        ++Log->Count;
    } else {
        ++Log->Overwritten;
    }
    Event = &Log->Events[Index];
    RtlZeroMemory(Event, sizeof(*Event));
    Event->Time = KeQueryInterruptTime();
    Event->Sequence = ++Log->Total;
    Event->Origin = Origin;
    Event->Opcode = Opcode;
    Event->Interrupts = Interrupts;
    Event->PolicyCc = (Origin == B2L_MAX_EVENT_POLL) ?
        ((ULONG)Context->CcStatus0 | ((ULONG)Context->CcStatus1 << 8)) :
        0xFFFFFFFFUL;
    if ((Interrupts & MAX77705_USBC_INT_SYSMSG) != 0UL) {
        ++Log->SysmsgEvents;
    }
    if ((Interrupts & (MAX77705_CC_INT_ATTACH_ERROR << 8)) != 0UL) {
        ++Log->AttachErrorEvents;
    }

    // Observational read only: no extra interrupt drain, opcode, reset or
    // role update. These later bytes are not an atomic latch with Interrupts
    // or PolicyCc. Failed reads remain explicit records and never turn an
    // otherwise successful opcode/policy operation into a failure.
    // If the policy CC read already failed, preserve that failure and its
    // transport diagnostics. Do not start another I2C transfer on this path.
    Status = TriggerStatus;
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxReadBlock(Context, B2L_MAX_USBC_ADDRESS,
            MAX77705_USBC_STATUS1, Event->Status,
            (ULONG)sizeof(Event->Status));
    }
    Event->ReadStatus = (ULONG)Status;
    if (NT_SUCCESS(Status)) {
        Event->Valid = 1UL;
    } else {
        RtlZeroMemory(Event->Status, sizeof(Event->Status));
        ++Log->SnapshotFailures;
    }
    Context->EventLogDirty = TRUE;
    Context->DiagnosticDirty = TRUE;
}

static NTSTATUS
B2lMaxExchangeOpcode(PB2L_MAX_CONTEXT Context, UCHAR Opcode,
    const UCHAR *WriteData, ULONG WriteDataLength, ULONG ResponseDataLength,
    UCHAR *ResponseData)
{
    B2L_MAX_OPCODE_REQUEST Request;
    NTSTATUS Status;

    if ((WriteDataLength > 1UL) || (ResponseDataLength > 1UL) ||
        ((WriteDataLength != 0UL) && (WriteData == NULL)) ||
        ((ResponseDataLength != 0UL) && (ResponseData == NULL))) {
        return STATUS_INVALID_PARAMETER;
    }
    B2lMaxInitOpcodeRequest(&Request, Opcode, (UCHAR)WriteDataLength,
        WriteDataLength != 0UL ? *WriteData : 0U, (UCHAR)ResponseDataLength);
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Status = B2lMaxQueueOpcodeLocked(Context, &Request);
    WdfWaitLockRelease(Context->MfdLock);
    if (NT_SUCCESS(Status)) {
        // BusLock serializes policy clients, but neither MfdLock nor the
        // transport lock is held while APCMDRES is delivered by GSIV 51.
        Status = B2lMaxWaitOpcode(Context, &Request);
    }
    if (NT_SUCCESS(Status) && (ResponseDataLength != 0UL)) {
        *ResponseData = Request.ResponseData;
    }
    return Status;
}

static NTSTATUS
B2lMaxSetMuicPath(PB2L_MAX_CONTEXT Context, UCHAR Path)
{
    NTSTATUS Status;

    WdfWaitLockAcquire(Context->MfdLock, NULL);
    ++Context->MuicGeneration;
    Context->MuicPathRequested = Path;
    Context->MuicPathVerified = FALSE;
    WdfWaitLockRelease(Context->MfdLock);

    // Samsung max77705_switch_path() sends CONTROL1_WRITE with one payload
    // byte.  COM_USB (0x09) connects both D+ and D- to the AP USB PHY;
    // COM_OPEN (0x3f) disconnects both lines.  Read CONTROL1 back so a
    // successful mailbox exchange cannot be mistaken for a switched path.
    Status = B2lMaxExchangeOpcode(
        Context,
        MAX77705_OPCODE_CONTROL1_WRITE,
        &Path,
        1UL,
        0UL,
        NULL
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxExchangeOpcode(
        Context,
        MAX77705_OPCODE_CONTROL1_READ,
        NULL,
        0UL,
        1UL,
        &Context->MuicControl1
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if (Context->MuicControl1 != Path) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    Context->MuicPathVerified = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxUpdateByte(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR Register,
    UCHAR Value,
    UCHAR Mask,
    UCHAR *ReadBack
    )
{
    UCHAR Current;
    UCHAR Wanted;
    NTSTATUS Status;

    Status = B2lMaxReadByte(Context, SlaveAddress, Register, &Current);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Wanted = (UCHAR)((Current & (UCHAR)~Mask) | (Value & Mask));
    if (Wanted != Current) {
        Status = B2lMaxWriteByte(Context, SlaveAddress, Register, Wanted);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    Status = B2lMaxReadByte(Context, SlaveAddress, Register, ReadBack);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    return (((*ReadBack) & Mask) == (Wanted & Mask)) ?
        STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

static NTSTATUS
B2lMaxUnlockCharger(PB2L_MAX_CONTEXT Context)
{
    UCHAR Current;
    UCHAR Wanted;
    ULONG Attempt;
    NTSTATUS Status;

    Context->ChargerUnlocked = FALSE;
    Context->ChargerUnlockAttempts = 0UL;
    for (Attempt = 0UL; Attempt < MAX77705_CHG_UNLOCK_RETRIES; ++Attempt) {
        Status = B2lMaxReadByte(
            Context,
            B2L_MAX_CHARGER_ADDRESS,
            MAX77705_CHG_CNFG_06,
            &Current
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        Context->ChargerConfig06 = Current;
        if ((Current & MAX77705_CHG_PROTECT_MASK) ==
            MAX77705_CHG_PROTECT_OPEN) {
            Context->ChargerUnlocked = TRUE;
            return STATUS_SUCCESS;
        }

        Wanted = (UCHAR)((Current & MAX77705_CHG_PROTECT_CLEAR_MASK) |
                         MAX77705_CHG_PROTECT_OPEN);
        Status = B2lMaxWriteByte(
            Context,
            B2L_MAX_CHARGER_ADDRESS,
            MAX77705_CHG_CNFG_06,
            Wanted
            );
        Context->ChargerUnlockAttempts = Attempt + 1UL;
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        // Samsung waits 20 ms before testing CHGPROT again.  Do not accept
        // the write itself as proof that protected charger writes are open.
        B2lMaxDelayMicroseconds(MAX77705_CHG_UNLOCK_WAIT_US);
    }

    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_06,
        &Context->ChargerConfig06
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((Context->ChargerConfig06 & MAX77705_CHG_PROTECT_MASK) !=
        MAX77705_CHG_PROTECT_OPEN) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    Context->ChargerUnlocked = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxReadCcStatus(PB2L_MAX_CONTEXT Context)
{
    UCHAR StatusBytes[2];
    UCHAR NewState;
    ULONGLONG SampleTime;
    NTSTATUS Status;

    // Linux's CCStat nested handler reads live CC_STATUS0. The MFD preserves
    // BYP/APCMD priority; a deferred diagnostic worker does not delay this
    // role read or replace it with its older status snapshot.
    Status = B2lMaxReadBlock(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_CC_STATUS0,
        StatusBytes,
        RTL_NUMBER_OF(StatusBytes)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    SampleTime = KeQueryInterruptTime();
    Context->CcStatus0 = StatusBytes[0];
    Context->CcStatus1 = StatusBytes[1];
    Context->CcLastSampleTime = SampleTime;
    ++Context->CcSampleCount;
    Context->CcStateChanged = FALSE;
    NewState = (UCHAR)(Context->CcStatus0 & B2L_MAX_CC_STATUS_MASK);
    if (!Context->CcStatusValid) {
        Context->PreviousCcState = NewState;
        Context->CcState = NewState;
        Context->CcStatusValid = TRUE;
        Context->DiagnosticDirty = TRUE;
    } else if (NewState != Context->CcState) {
        Context->PreviousCcState = Context->CcState;
        Context->CcState = NewState;
        Context->CcStateChanged = TRUE;
        Context->CcLastTransitionTime = SampleTime;
        ++Context->CcTransitionCount;
        Context->DiagnosticDirty = TRUE;
    } else if ((Context->CcInterrupt & MAX77705_CC_INT_ATTACH_MASK) != 0U) {
        // Preserve an interrupt whose status already settled back to the same
        // state.  Sticky interrupt bytes and the counter make it diagnosable.
        Context->DiagnosticDirty = TRUE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxWriteUsbcMasks(
    PB2L_MAX_CONTEXT Context,
    UCHAR UicMask,
    UCHAR CcMask,
    UCHAR PdMask,
    UCHAR VdmMask
    )
{
    NTSTATUS Status;

    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_UIC_INT_MASK,
        UicMask,
        0xFFU,
        &Context->UicInterruptMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_CC_INT_MASK,
        CcMask,
        0xFFU,
        &Context->CcInterruptMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_PD_INT_MASK,
        PdMask,
        0xFFU,
        &Context->PdInterruptMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    return B2lMaxUpdateByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_VDM_INT_MASK,
        VdmMask,
        0xFFU,
        &Context->VdmInterruptMask
        );
}

static NTSTATUS
B2lMaxNormalizeCcOpen(PB2L_MAX_CONTEXT Context)
{
    UCHAR Wanted;
    NTSTATUS Status;

    Context->CcOpenNormalized = FALSE;
    Context->Control3Before = 0xFFU;
    Context->Control3After = 0xFFU;

    // Samsung finishes max77705_usbc_probe() by issuing the manual CC-open
    // release request.  Its off path reads CONTROL3 and clears only
    // LockErrorEn; it does not force VBUS or synthesize a SOURCE role.
    Status = B2lMaxExchangeOpcode(
        Context,
        MAX77705_OPCODE_CONTROL3_READ,
        NULL,
        0UL,
        1UL,
        &Context->Control3Before
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    // Use an explicit 8-bit mask: MSVC /W4 diagnoses casting the promoted
    // bitwise complement (~0x02 == 0xfffffffd) back to UCHAR as C4310.
    Wanted = (UCHAR)(Context->Control3Before &
                     MAX77705_CONTROL3_LOCK_ERROR_EN_CLEAR_MASK);
    if (Wanted != Context->Control3Before) {
        Status = B2lMaxExchangeOpcode(
            Context,
            MAX77705_OPCODE_CONTROL3_WRITE,
            &Wanted,
            1UL,
            0UL,
            NULL
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    Status = B2lMaxExchangeOpcode(
        Context,
        MAX77705_OPCODE_CONTROL3_READ,
        NULL,
        0UL,
        1UL,
        &Context->Control3After
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((Context->Control3After & MAX77705_CONTROL3_LOCK_ERROR_EN) != 0U) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Context->CcOpenNormalized = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxProbeIdentity(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->IdentityReady = FALSE;
    Context->BaselineValid = FALSE;

    // Same ordering as max77705_i2c_probe(): prove TOP first, then create
    // the USBC and charger logical clients on the shared HSI2C45 bus.
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_TOP_ADDRESS,
        MAX77705_TOP_PMIC_ID1,
        &Context->PmicId
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_TOP_ADDRESS,
        MAX77705_TOP_PMIC_REV,
        &Context->PmicRev
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if (!(((Context->PmicId == 0x05U) &&
           (((Context->PmicRev & 0x07U) == 0x03U) ||
            ((Context->PmicRev & 0x07U) == 0x04U))) ||
          ((Context->PmicId == 0x15U) &&
           ((Context->PmicRev & 0x07U) == 0x02U)))) {
        return STATUS_REVISION_MISMATCH;
    }

    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_HW_REV,
        &Context->UsbcHwRev
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_FW_REV,
        &Context->UsbcFwRev
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_FW_MINOR,
        &Context->UsbcFwMinor
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_INT_MASK,
        &Context->ChargerIntMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        &Context->ChargerMode
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_06,
        &Context->ChargerConfig06
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Context->ChargerIntMaskBaseline = Context->ChargerIntMask;
    Context->ChargerModeBaseline = Context->ChargerMode;
    Context->OtgHardwareActive =
        (((Context->ChargerMode & MAX77705_CHG_MODE_MASK) == MAX77705_MODE_BOOST_OTG) ||
         ((Context->ChargerMode & MAX77705_CHG_MODE_MASK) == MAX77705_MODE_BUCK_OTG) ||
         ((Context->ChargerMode & MAX77705_CHG_MODE_MASK) == MAX77705_MODE_BUCK_CHG_OTG));
    Context->BaselineValid = TRUE;
    Context->IdentityReady = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxEnableOtg(PB2L_MAX_CONTEXT Context)
{
    UCHAR Current;
    UCHAR Mode;
    UCHAR NewMode;
    UCHAR ReadBack;
    NTSTATUS Status;

    // CHG_CNFG_00 and INT_MASK are protected settings.  Linux verifies
    // CNFG_06.CHGPROT before mutable charger work; Windows must do the same
    // because it cannot assume the Android charger probe already ran.
    Status = B2lMaxUnlockCharger(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        &Current
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Mode = (UCHAR)(Current & MAX77705_CHG_MODE_MASK);
    switch (Mode) {
    case MAX77705_MODE_ALL_OFF:
        NewMode = MAX77705_MODE_BOOST_OTG;
        break;
    case MAX77705_MODE_BUCK:
        NewMode = MAX77705_MODE_BUCK_OTG;
        break;
    case MAX77705_MODE_BUCK_CHG:
        NewMode = MAX77705_MODE_BUCK_CHG_OTG;
        break;
    case MAX77705_MODE_BOOST_UNO:
        NewMode = MAX77705_MODE_BOOST_OTG;
        break;
    case MAX77705_MODE_BUCK_UNO:
    case MAX77705_MODE_BUCK_CHG_UNO:
        NewMode = MAX77705_MODE_BUCK_OTG;
        break;
    case MAX77705_MODE_BOOST_OTG:
    case MAX77705_MODE_BUCK_OTG:
    case MAX77705_MODE_BUCK_CHG_OTG:
        NewMode = Mode;
        break;
    default:
        return STATUS_INVALID_DEVICE_STATE;
    }

    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_INT_MASK,
        MAX77705_CHG_OTG_INT_VALUE,
        MAX77705_CHG_OTG_INT_MASK,
        &Context->ChargerIntMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    // Samsung updates CHG/CHGIN/BYP masking even when CNFG_00 already
    // contains an OTG mode.  This matters when Windows inherits a mode that
    // firmware or an earlier operating system selected.
    if (NewMode == Mode) {
        Context->ChargerMode = Current;
        Context->OtgEnabled = TRUE;
        Context->OtgHardwareActive = TRUE;
        return STATUS_SUCCESS;
    }

    // Samsung's UNO -> OTG transition first forces mode 4 and waits 1-2 ms.
    if ((Mode == MAX77705_MODE_BOOST_UNO) ||
        (Mode == MAX77705_MODE_BUCK_UNO) ||
        (Mode == MAX77705_MODE_BUCK_CHG_UNO)) {
        Status = B2lMaxUpdateByte(
            Context,
            B2L_MAX_CHARGER_ADDRESS,
            MAX77705_CHG_CNFG_00,
            MAX77705_MODE_BUCK,
            MAX77705_CHG_MODE_MASK,
            &ReadBack
            );
        if (!NT_SUCCESS(Status)) {
            goto RestoreMask;
        }
        B2lMaxDelayMicroseconds(1500U);
    }

    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        NewMode,
        MAX77705_CHG_MODE_MASK,
        &Context->ChargerMode
        );
    if (!NT_SUCCESS(Status)) {
        goto RestoreModeAndMask;
    }
    Context->OtgEnabled = TRUE;
    Context->OtgHardwareActive = TRUE;
    return STATUS_SUCCESS;

RestoreModeAndMask:
    (VOID)B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        Context->ChargerModeBaseline,
        MAX77705_CHG_MODE_MASK,
        &ReadBack
        );
RestoreMask:
    (VOID)B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_INT_MASK,
        Context->ChargerIntMaskBaseline,
        0xFFU,
        &ReadBack
        );
    return Status;
}

static NTSTATUS
B2lMaxDisableOtg(PB2L_MAX_CONTEXT Context)
{
    UCHAR Current;
    UCHAR Mode;
    UCHAR SafeMode;
    NTSTATUS Status;
    NTSTATUS MaskStatus;

    if (!Context->OtgEnabled && !Context->OtgHardwareActive) {
        return STATUS_SUCCESS;
    }
    Status = B2lMaxUnlockCharger(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        &Current
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Mode = (UCHAR)(Current & MAX77705_CHG_MODE_MASK);
    switch (Mode) {
    case MAX77705_MODE_BOOST_OTG:
        SafeMode = MAX77705_MODE_ALL_OFF;
        break;
    case MAX77705_MODE_BUCK_OTG:
        SafeMode = MAX77705_MODE_BUCK;
        break;
    case MAX77705_MODE_BUCK_CHG_OTG:
        SafeMode = MAX77705_MODE_BUCK_CHG;
        break;
    default:
        SafeMode = (UCHAR)(Context->ChargerModeBaseline &
                           MAX77705_CHG_MODE_MASK);
        break;
    }
    Status = B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_CNFG_00,
        SafeMode,
        MAX77705_CHG_MODE_MASK,
        &Context->ChargerMode
        );
    if (NT_SUCCESS(Status)) {
        B2lMaxDelayMicroseconds(50000U);
    }
    MaskStatus = B2lMaxUpdateByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_INT_MASK,
        Context->ChargerIntMaskBaseline,
        0xFFU,
        &Context->ChargerIntMask
        );
    if (NT_SUCCESS(Status)) {
        Status = MaskStatus;
    }
    if (NT_SUCCESS(Status)) {
        Context->OtgEnabled = FALSE;
        Context->OtgHardwareActive = FALSE;
    }
    return Status;
}

static NTSTATUS
B2lMaxEvaluateConnection(PB2L_MAX_CONTEXT Context, BOOLEAN InitialDetect)
{
    UCHAR CcState;
    NTSTATUS Status;

    Status = B2lMaxReadCcStatus(Context);
    if (!NT_SUCCESS(Status)) {
        B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_CC, Status);
        return Status;
    }
    CcState = Context->CcState;
    // Samsung CCIC_IRQ_INIT_DETECT processes SINK only. In particular, a
    // probe status read must never substitute for the source CCSTAT IRQ.
    if (InitialDetect && (CcState != B2L_MAX_CC_SINK)) return STATUS_SUCCESS;
    if (CcState == B2L_MAX_CC_SOURCE) {
        ++Context->CcSourceEventCount; // actual CCSTAT dispatch, never probe/pin-only
        // Samsung max77705_ccstat_irq_handler turns on VBUS in cc_SOURCE.
        // Only a power-role transition starts source power. BYP overcurrent
        // can turn OTG off while current_pr remains SRC in the Linux handler.
        if (!Context->PowerRoleValid || (Context->PowerRole != B2L_MAX_CC_SOURCE)) {
            Status = B2lMaxEnableOtg(Context);
            B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_OTG_ON, Status);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }
        Context->PowerRole = B2L_MAX_CC_SOURCE;
        Context->PowerRoleValid = TRUE;
        // DFP/UFP comes from PD_STATUS1, never from this power-role value.
        return STATUS_SUCCESS;
    }
    // CC source power is permitted in exactly one state. This also shuts down
    // OTG inherited from boot firmware when the current cable is detached,
    // sink, audio/debug accessory, disabled or erroneous.
    if ((CcState != B2L_MAX_CC_SOURCE) &&
        (Context->OtgEnabled || Context->OtgHardwareActive)) {
        Status = B2lMaxDisableOtg(Context);
        B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_OTG_OFF, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    if (CcState == B2L_MAX_CC_SINK) {
        Context->PowerRole = B2L_MAX_CC_SINK;
        Context->PowerRoleValid = TRUE;
        // A PR swap does not imply a DR swap or disconnect the data path.
        return STATUS_SUCCESS;
    }
    Context->PowerRoleValid = Context->DataRoleValid = FALSE;
    Context->PowerRole = Context->DataRole = 0xFFU;
    Context->DataRoleStatus = STATUS_DEVICE_NOT_READY;
    // Mirror Samsung detach/logical-detach: remove VBUS first, then isolate
    // D+/D-.  Also establish a verified OPEN baseline on the first no-cable
    // sample so boot-firmware switch state is never assumed.
    if (!Context->MuicPathVerified ||
        (Context->MuicControl1 != MAX77705_MUIC_COM_OPEN)) {
        Status = B2lMaxRequestMuicLocked(Context, MAX77705_MUIC_COM_OPEN);
        B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_MUIC_OPEN, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    return Status;
}

static NTSTATUS
B2lMaxReadWaterState(PB2L_MAX_CONTEXT Context)
{
    UCHAR Value;
    NTSTATUS Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_CC_STATUS1, &Value);
    if (NT_SUCCESS(Status)) {
        Context->CcStatus1 = Value;
        Context->WaterDetected = (BOOLEAN)((Value & MAX77705_CC_STATUS_WATER) != 0U);
    }
    return Status;
}

static NTSTATUS
B2lMaxHandleDataRole(PB2L_MAX_CONTEXT Context, BOOLEAN InitialDetect)
{
    UCHAR Value, Role;
    NTSTATUS Status;

    // Samsung PD DATAROLE: read the real PD_STATUS1, then require a CC power
    // role. Initial-detect only processes SINK. No source -> DFP inference.
    Status = B2lMaxReadByte(Context, B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_PD_STATUS1, &Value);
    if (!NT_SUCCESS(Status)) {
        Context->DataRoleStatus = Status;
        return Status;
    }
    Context->PdStatus1 = Value;
    if (!Context->PowerRoleValid ||
        (InitialDetect && (Context->PowerRole != B2L_MAX_CC_SINK))) {
        return STATUS_SUCCESS;
    }
    if (!InitialDetect) ++Context->DataRoleEvents;
    Role = (UCHAR)((Value >> 7) & 1U);
    if (!Context->DataRoleValid || (Context->DataRole != Role)) {
        ++Context->DataRoleChanges;
    }
    Context->DataRole = Role;
    Context->DataRoleValid = TRUE;
    // notify_dr_status blocks MUIC/USB attach while current_connstat=WATER.
    // This is read from CC_STATUS1 by CC init and the VCONNSC handler.
    if (Context->WaterDetected) {
        Context->DataRoleStatus = STATUS_DEVICE_NOT_READY;
        return STATUS_DEVICE_NOT_READY;
    }
    if (Role == B2L_MAX_DATA_DFP) {
        // DFP -> MUIC RpRd=1 -> OTG -> COM_USB_AP in the stock notification
        // chain. DWC3/xHCI notification is a separate consumer still to port.
        Status = (Context->MuicPathVerified &&
            (Context->MuicControl1 == MAX77705_MUIC_COM_USB_AP)) ? STATUS_SUCCESS :
            B2lMaxRequestMuicLocked(Context, MAX77705_MUIC_COM_USB_AP);
    } else {
        // UFP needs the MUIC charger classification and Windows function
        // stack. Retire any old host path; do not present UFP as DFP success.
        Status = B2lMaxRequestMuicLocked(Context, MAX77705_MUIC_COM_OPEN);
        if (Status == STATUS_SUCCESS) Status = STATUS_NOT_SUPPORTED;
    }
    Context->DataRoleStatus = Status;
    B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_PD, Status);
    return Status;
}

static NTSTATUS
B2lMaxHandleBypass(PB2L_MAX_CONTEXT Context)
{
    UCHAR Details;
    NTSTATUS Status = B2lMaxUnlockCharger(Context);
    if (!NT_SUCCESS(Status)) return Status;
    Status = B2lMaxReadByte(Context, B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_DETAILS_02, &Details);
    if (!NT_SUCCESS(Status)) return Status;
    Context->ChargerDetails[2] = Details;
    // max77705_charger.h: BYP_DTLS mask=0x0f, shift=0; bit0 is overcurrent.
    if (((Details & 0x01U) != 0U) && (Context->OtgEnabled || Context->OtgHardwareActive)) {
        ++Context->OvercurrentEvents;
        Status = B2lMaxDisableOtg(Context);
        B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_BYP, Status);
    }
    return Status;
}

NTSTATUS
B2lMaxDispatchBypassLocked(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status = B2lMaxHandleBypass(Context);
    B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_BYP, Status);
    return Status;
}

VOID
B2lMaxObserveIrqLocked(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame)
{
    Context->TopInterruptSource = Frame->Top;
    Context->TopInterruptSourceSeen |= Frame->Top;
    Context->UicInterrupt = Frame->Active[0];
    Context->CcInterrupt = Frame->Active[1];
    Context->PdInterrupt = Frame->Active[2];
    Context->VdmInterrupt = Frame->Active[3];
    Context->UicInterruptSeen |= Frame->Raw[0];
    Context->CcInterruptSeen |= Frame->Raw[1];
    Context->PdInterruptSeen |= Frame->Raw[2];
    Context->VdmInterruptSeen |= Frame->Raw[3];
    if (Frame->Active[1] != 0U) ++Context->CcInterruptCount;
}

VOID
B2lMaxDispatchCcPdLocked(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Stage = B2L_MAX_STAGE_CC;
    BOOLEAN Handled = FALSE;

    if ((Frame->Active[1] & MAX77705_CC_INT_WATER) != 0U) {
        Status = B2lMaxReadWaterState(Context);
        Handled = TRUE;
    }
    if (NT_SUCCESS(Status) && (((Frame->Active[1] & MAX77705_CC_INT_STATE) != 0U) ||
        Frame->DetachRecheck)) {
        if (Frame->DetachRecheck) ++Context->DetachRechecks;
        // Re-read live CC just as the stock nested handler does. Do not act
        // on the earlier diagnostic snapshot after waiting in a work queue.
        Status = B2lMaxEvaluateConnection(Context, FALSE);
        Handled = TRUE;
    }
    if (NT_SUCCESS(Status) && ((Frame->Active[2] & MAX77705_PD_INT_DATAROLE) != 0U)) {
        Status = B2lMaxHandleDataRole(Context, FALSE);
        Stage = B2L_MAX_STAGE_PD;
        Handled = TRUE;
    }
    if (Handled) B2lMaxSetDiagnosticLocked(Context, Stage, Status);
}

static NTSTATUS
B2lMaxInitializeUsbc(PB2L_MAX_CONTEXT Context)
{
    B2L_MAX_OPCODE_REQUEST Requests[4];
    UCHAR ReadBack;
    ULONG Index;
    NTSTATUS Status;

    B2lMaxInitOpcodeRequest(&Requests[0], MAX77705_OPCODE_FW_AUTOIBUS,
        1U, MAX77705_AUTOIBUS_AT_OFF, 0U);
    B2lMaxInitOpcodeRequest(&Requests[1], MAX77705_OPCODE_FACTORY_TEST,
        1U, MAX77705_FACTORY_AUTO_VBUS_OFF, 1U);
    B2lMaxInitOpcodeRequest(&Requests[2], MAX77705_OPCODE_CCCTRL1_WRITE,
        1U, MAX77705_CCCTRL1_BEYOND2LTE, 1U);
    B2lMaxInitOpcodeRequest(&Requests[3], MAX77705_OPCODE_CCCTRL1_READ,
        0U, 0U, 1U);

    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Status = Context->MfdFault;
    if (!NT_SUCCESS(Status) || !Context->MaxIrqEnabled) {
        if (NT_SUCCESS(Status)) Status = STATUS_DEVICE_NOT_READY;
        goto Failed;
    }
    B2lMaxGpioMask(Context);
    // MFD starts with all nested sources masked, then installs its clients.
    Status = B2lMaxUpdateByte(Context, B2L_MAX_TOP_ADDRESS, 0x23U,
        0x0FU, 0x0FU, &Context->TopInterruptSourceMask);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = B2lMaxWriteByte(Context, B2L_MAX_TOP_ADDRESS, 0x26U, 0xFFU);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = B2lMaxWriteByte(Context, B2L_MAX_CHARGER_ADDRESS, 0xB1U, 0xFFU);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->ChargerIntMask = Context->ChargerIntMaskBaseline = 0xFFU;
    Status = B2lMaxWriteUsbcMasks(Context, 0xFFU, 0xFFU, 0xFFU, 0xFFU);
    if (!NT_SUCCESS(Status)) goto Failed;
    // Original MFD enables CHG top source while USBC remains masked.
    Status = B2lMaxUpdateByte(Context, B2L_MAX_TOP_ADDRESS, 0x23U,
        0U, 0x01U, &Context->TopInterruptSourceMask);
    if (!NT_SUCCESS(Status)) goto Failed;
    // Equivalent to USBC/MUIC/CC/PD nested handlers registering in probe.
    Status = B2lMaxWriteUsbcMasks(Context, MAX77705_UIC_INT_MASK_LINUX,
        MAX77705_CC_INT_MASK_LINUX, MAX77705_PD_INT_MASK_RUNTIME,
        MAX77705_VDM_INT_MASK_RUNTIME);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = B2lMaxEvaluateConnection(Context, TRUE);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = B2lMaxReadWaterState(Context);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->MfdReady = TRUE;
    // PD init queues AUTOIBUS before the final probe masks/init opcodes.
    Status = B2lMaxQueueOpcodeLocked(Context, &Requests[0]);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = B2lMaxHandleDataRole(Context, TRUE);
    // Function-stack support is a separate consumer, not a mailbox failure.
    if (!NT_SUCCESS(Status) && (Status != STATUS_NOT_SUPPORTED) &&
        !((Status == STATUS_DEVICE_NOT_READY) && Context->WaterDetected &&
          Context->DataRoleValid)) goto Failed;
    Status = B2lMaxWriteUsbcMasks(Context, MAX77705_UIC_INT_MASK_LINUX,
        MAX77705_CC_INT_MASK_LINUX, MAX77705_PD_INT_MASK_PROBE,
        MAX77705_VDM_INT_MASK_PROBE);
    if (!NT_SUCCESS(Status)) goto Failed;
    Context->UsbcMasksConfigured = TRUE;
    for (Index = 1UL; Index < RTL_NUMBER_OF(Requests); ++Index) {
        Status = B2lMaxQueueOpcodeLocked(Context, &Requests[Index]);
        if (!NT_SUCCESS(Status)) goto Failed;
    }
    // cc_booting_complete -> unmask top USBC bit; no pre-unmask response wait.
    Status = B2lMaxUpdateByte(Context, B2L_MAX_TOP_ADDRESS, 0x23U,
        0U, 0x08U, &Context->TopInterruptSourceMask);
    if (!NT_SUCCESS(Status)) goto Failed;
    B2lMaxGpioUnmask(Context);
    WdfWaitLockRelease(Context->MfdLock);
    for (Index = 0UL; Index < RTL_NUMBER_OF(Requests); ++Index) {
        Status = B2lMaxWaitOpcode(Context, &Requests[Index]);
        if (!NT_SUCCESS(Status)) return Status; // cancellation retired all links
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        switch (Index) {
        case 0:
            Context->AutoIbusMode = MAX77705_AUTOIBUS_AT_OFF;
            Context->AutoIbusConfigured = TRUE;
            break;
        case 1:
            Context->OpcodeResponse = Requests[Index].ResponseData;
            Context->AutoVbusDisabled = TRUE;
            break;
        case 2:
            Context->CcControlResponse = Requests[Index].ResponseData;
            break;
        case 3:
            ReadBack = Requests[Index].ResponseData;
            Context->CcControlReadBack = ReadBack;
            if (ReadBack != MAX77705_CCCTRL1_BEYOND2LTE) {
                WdfWaitLockRelease(Context->MfdLock);
                return STATUS_DEVICE_DATA_ERROR;
            }
            Context->CcControlConfigured = TRUE;
            break;
        }
        WdfWaitLockRelease(Context->MfdLock);
    }
    // Do not replace normal probe masks by reset-recovery mask constants.
    // Alternate-mode/PD mask transitions need their actual Linux handlers.
    Status = B2lMaxNormalizeCcOpen(Context);
    if (NT_SUCCESS(Status)) Context->UsbcInitialized = TRUE;
    return Status;

Failed:
    B2lMaxCancelOpcodesLocked(Context, Status);
    Context->MfdReady = FALSE;
    B2lMaxGpioMask(Context);
    WdfWaitLockRelease(Context->MfdLock);
    return Status;
}

static NTSTATUS
B2lMaxServiceConnection(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    if (!Context->HardwareReady) return STATUS_DEVICE_NOT_READY;
    if (!Context->IdentityReady) {
        Status = B2lMaxProbeIdentity(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_IDENTITY, Status);
        if (!NT_SUCCESS(Status)) return Status;
        B2lMaxCaptureEvent(Context, B2L_MAX_EVENT_BASELINE, 0xFFUL, 0UL, STATUS_SUCCESS);
    }
    Status = B2lMaxUnlockCharger(Context);
    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_CHARGER_UNLOCK, Status);
    if (!NT_SUCCESS(Status)) return Status;
    Status = B2lMaxInitializeUsbc(Context);
    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_MFD_IRQ, Status);
    if (!NT_SUCCESS(Status)) return Status;
    return STATUS_SUCCESS;
}

static VOID
B2lMaxUnmapResources(PB2L_MAX_CONTEXT Context)
{
    Context->PolicyEnabled = FALSE;
    if (Context->HsI2cRegisters != NULL) {
        MmUnmapIoSpace(Context->HsI2cRegisters, Context->HsI2cLength);
        Context->HsI2cRegisters = NULL;
    }
    if (Context->UsiRegisters != NULL) {
        MmUnmapIoSpace(Context->UsiRegisters, Context->UsiLength);
        Context->UsiRegisters = NULL;
    }
    if (Context->CmgpCmuRegisters != NULL) {
        MmUnmapIoSpace(Context->CmgpCmuRegisters, Context->CmgpCmuLength);
        Context->CmgpCmuRegisters = NULL;
    }
    if (Context->CmgpPinctrlRegisters != NULL) {
        MmUnmapIoSpace(
            Context->CmgpPinctrlRegisters,
            Context->CmgpPinctrlLength
            );
        Context->CmgpPinctrlRegisters = NULL;
    }
    if (Context->AliveRegisters != NULL) {
        MmUnmapIoSpace(Context->AliveRegisters, Context->AliveLength);
        Context->AliveRegisters = NULL;
    }
    Context->AliveLength = 0UL;
    Context->HsI2cLength = 0UL;
    Context->UsiLength = 0UL;
    Context->CmgpCmuLength = 0UL;
    Context->CmgpPinctrlLength = 0UL;
    Context->HardwareReady = FALSE;
    Context->PlatformPrepared = FALSE;
    Context->OtgHardwareActive = FALSE;
    Context->ChargerUnlocked = FALSE;
    Context->AutoVbusDisabled = FALSE;
    Context->AutoIbusConfigured = FALSE;
    Context->UsbcMasksConfigured = FALSE;
    Context->UsbcRuntimeMasksConfigured = FALSE;
    Context->CcControlConfigured = FALSE;
    Context->CcOpenNormalized = FALSE;
    Context->Control3Before = 0xFFU;
    Context->Control3After = 0xFFU;
    Context->MuicPathVerified = FALSE;
    Context->MuicPathRequested = 0xFFU;
    Context->MuicControl1 = 0xFFU;
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;

    WDF_DRIVER_CONFIG_INIT(&Config, B2lMaxEvtDeviceAdd);
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &Attributes,
        &Config,
        WDF_NO_HANDLE
        );
}

NTSTATUS
B2lMaxEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS PowerCallbacks;
    WDF_WORKITEM_CONFIG WorkConfig;
    WDF_TIMER_CONFIG TimerConfig;
    WDF_INTERRUPT_CONFIG InterruptConfig;
    WDFDEVICE Device;
    PB2L_MAX_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PowerCallbacks);
    PowerCallbacks.EvtDevicePrepareHardware = B2lMaxEvtPrepareHardware;
    PowerCallbacks.EvtDeviceReleaseHardware = B2lMaxEvtReleaseHardware;
    PowerCallbacks.EvtDeviceD0Entry = B2lMaxEvtD0Entry;
    PowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = B2lMaxEvtD0EntryPostInterruptsEnabled;
    PowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = B2lMaxEvtD0ExitPreInterruptsDisabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, B2L_MAX_CONTEXT);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Context = B2lMaxGetContext(Device);
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Device = Device;
    KeInitializeEvent(&Context->HsI2cCompletion, SynchronizationEvent, FALSE);
    WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig,
        B2lMaxHsI2cEvtInterruptIsr, B2lMaxHsI2cEvtInterruptDpc);
    InterruptConfig.AutomaticSerialization = FALSE;
    InterruptConfig.ShareVector = WdfFalse;
    InterruptConfig.ReportInactiveOnPowerDown = WdfFalse;
    InterruptConfig.EvtInterruptEnable = B2lMaxHsI2cEvtInterruptEnable;
    InterruptConfig.EvtInterruptDisable = B2lMaxHsI2cEvtInterruptDisable;
    Status = WdfInterruptCreate(Device, &InterruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &Context->HsI2cInterrupt);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Context->CcStatus0 = 0xFFU;
    Context->OpcodeResponse = 0xFFU;
    Context->AutoIbusMode = 0xFFU;
    Context->CcControlResponse = 0xFFU;
    Context->CcControlReadBack = 0xFFU;
    Context->Control3Before = 0xFFU;
    Context->Control3After = 0xFFU;
    Context->UicInterruptMask = 0xFFU;
    Context->CcInterruptMask = 0xFFU;
    Context->PdInterruptMask = 0xFFU;
    Context->VdmInterruptMask = 0xFFU;
    Context->MuicPathRequested = 0xFFU;
    Context->MuicControl1 = 0xFFU;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Status = WdfWaitLockCreate(&Attributes, &Context->BusLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = WdfWaitLockCreate(&Attributes, &Context->TransportLock);
    if (!NT_SUCCESS(Status)) return Status;
    Status = WdfWaitLockCreate(&Attributes, &Context->MfdLock);
    if (!NT_SUCCESS(Status)) return Status;
    // Second ACPI interrupt: real MAX GPA1-6 EINT. OS masks its GIC parent
    // while the passive ISR services the I2C-connected interrupt source.
    WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, B2lMaxEvtMaxInterruptIsr, NULL);
    InterruptConfig.PassiveHandling = TRUE;
    InterruptConfig.AutomaticSerialization = FALSE;
    InterruptConfig.WaitLock = Context->MfdLock;
    InterruptConfig.ShareVector = WdfFalse;
    InterruptConfig.ReportInactiveOnPowerDown = WdfFalse;
    InterruptConfig.EvtInterruptEnable = B2lMaxEvtMaxInterruptEnable;
    InterruptConfig.EvtInterruptDisable = B2lMaxEvtMaxInterruptDisable;
    Status = WdfInterruptCreate(Device, &InterruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &Context->MaxInterrupt);
    if (!NT_SUCCESS(Status)) return Status;
    WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lMaxEvtOpcodeDeadline);
    TimerConfig.AutomaticSerialization = FALSE;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfTimerCreate(&TimerConfig, &Attributes, &Context->OpcodeDeadline);
    if (!NT_SUCCESS(Status)) return Status;
    WDF_WORKITEM_CONFIG_INIT(&WorkConfig, B2lMaxEvtPolicyWork);
    WorkConfig.AutomaticSerialization = FALSE;
    return WdfWorkItemCreate(&WorkConfig, &Attributes, &Context->PolicyWork);
}

NTSTATUS
B2lMaxEvtPrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw;
    ULONG Count;
    ULONG Index;
    ULONG MemoryCount = 0UL;
    ULONG InterruptCount = 0UL;
    ULONGLONG Start;

    B2lMaxUnmapResources(Context);
    Context->HsI2cRawIrq = 0UL;
    Context->HsI2cTranslatedIrq = 0UL;
    Context->MaxRawIrq = Context->MaxTranslatedIrq = 0UL;
    Count = WdfCmResourceListGetCount(ResourcesTranslated);
    for (Index = 0UL; Index < Count; ++Index) {
        Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        if (Resource == NULL) {
            B2lMaxUnmapResources(Context);
            B2lMaxPublishDiagnostic(
                Context,
                B2L_MAX_STAGE_RESOURCES,
                STATUS_DEVICE_CONFIGURATION_ERROR
                );
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if (Resource->Type == CmResourceTypeInterrupt) {
            Raw = WdfCmResourceListGetDescriptor(ResourcesRaw, Index);
            if ((Raw == NULL) || (Raw->Type != CmResourceTypeInterrupt) ||
                (InterruptCount > 1UL) ||
                (Raw->u.Interrupt.Vector != (InterruptCount == 0UL ?
                    B2L_MAX_HSI2C_GSIV : B2L_MAX_GPIO_GSIV)) ||
                ((Resource->Flags & (CM_RESOURCE_INTERRUPT_LATCHED |
                                    CM_RESOURCE_INTERRUPT_MESSAGE)) != 0)) {
                B2lMaxUnmapResources(Context);
                B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_RESOURCES,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            if (InterruptCount == 0UL) {
                Context->HsI2cRawIrq = Raw->u.Interrupt.Vector;
                Context->HsI2cTranslatedIrq = Resource->u.Interrupt.Vector;
            } else {
                Context->MaxRawIrq = Raw->u.Interrupt.Vector;
                Context->MaxTranslatedIrq = Resource->u.Interrupt.Vector;
            }
            ++InterruptCount;
            continue;
        }
        if (Resource->Type != CmResourceTypeMemory) {
            continue;
        }
        Start = (ULONGLONG)Resource->u.Memory.Start.QuadPart;
        if ((Start == B2L_MAX_HSI2C_PHYSICAL_BASE) &&
            (Resource->u.Memory.Length == B2L_MAX_DEVICE_PAGE_LENGTH) &&
            (Context->HsI2cRegisters == NULL)) {
            Context->HsI2cRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->HsI2cLength = Resource->u.Memory.Length;
        } else if ((Start == B2L_MAX_USI_PHYSICAL_BASE) &&
                   (Resource->u.Memory.Length ==
                    B2L_MAX_DEVICE_PAGE_LENGTH) &&
                   (Context->UsiRegisters == NULL)) {
            Context->UsiRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->UsiLength = Resource->u.Memory.Length;
        } else if ((Start == B2L_MAX_CMGP_CMU_PHYSICAL_BASE) &&
                   (Resource->u.Memory.Length == B2L_MAX_CMGP_CMU_LENGTH) &&
                   (Context->CmgpCmuRegisters == NULL)) {
            Context->CmgpCmuRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->CmgpCmuLength = Resource->u.Memory.Length;
        } else if ((Start == B2L_MAX_CMGP_PINCTRL_PHYSICAL_BASE) &&
                   (Resource->u.Memory.Length ==
                    B2L_MAX_DEVICE_PAGE_LENGTH) &&
                   (Context->CmgpPinctrlRegisters == NULL)) {
            Context->CmgpPinctrlRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->CmgpPinctrlLength = Resource->u.Memory.Length;
        } else if ((Start == B2L_MAX_ALIVE_PHYSICAL_BASE) &&
                   (Resource->u.Memory.Length == B2L_MAX_DEVICE_PAGE_LENGTH) &&
                   (Context->AliveRegisters == NULL)) {
            Context->AliveRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start, Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE);
            Context->AliveLength = Resource->u.Memory.Length;
        } else {
            B2lMaxUnmapResources(Context);
            B2lMaxPublishDiagnostic(
                Context,
                B2L_MAX_STAGE_RESOURCES,
                STATUS_DEVICE_CONFIGURATION_ERROR
                );
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        ++MemoryCount;
    }
    if ((MemoryCount != 5UL) || (InterruptCount != 2UL) || (Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL) ||
        (Context->CmgpCmuRegisters == NULL) ||
        (Context->CmgpPinctrlRegisters == NULL) || (Context->AliveRegisters == NULL)) {
        B2lMaxUnmapResources(Context);
        B2lMaxPublishDiagnostic(
            Context,
            B2L_MAX_STAGE_RESOURCES,
            STATUS_INSUFFICIENT_RESOURCES
            );
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->HardwareReady = TRUE;
    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_RESOURCES, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtReleaseHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Context->PolicyEnabled = FALSE;
    WdfWaitLockRelease(Context->MfdLock);
    WdfWaitLockRelease(Context->BusLock);
    WdfWorkItemFlush(Context->PolicyWork);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    // Hardware is no longer accessible here; all mask/quiesce operations
    // were completed in PreInterruptsDisabled while resources were powered.
    B2lMaxUnmapResources(Context);
    Context->IdentityReady = FALSE;
    Context->BaselineValid = FALSE;
    Context->OtgHardwareActive = FALSE;
    Context->ChargerUnlocked = FALSE;
    Context->AutoVbusDisabled = FALSE;
    Context->AutoIbusConfigured = FALSE;
    Context->UsbcMasksConfigured = FALSE;
    Context->UsbcRuntimeMasksConfigured = FALSE;
    Context->CcControlConfigured = FALSE;
    Context->CcOpenNormalized = FALSE;
    Context->Control3Before = 0xFFU;
    Context->Control3After = 0xFFU;
    Context->MuicPathVerified = FALSE;
    Context->MuicPathRequested = 0xFFU;
    Context->MuicControl1 = 0xFFU;
    WdfWaitLockRelease(Context->BusLock);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    RtlZeroMemory(Context->MuicPairs, sizeof(Context->MuicPairs));
    Context->MuicOutstanding = 0UL;
    Context->MuicGeneration = 0ULL;
    Context->MuicStatus = STATUS_DEVICE_NOT_READY;
    Context->PowerRoleValid = Context->DataRoleValid = Context->WaterDetected = FALSE;
    Context->PowerRole = Context->DataRole = 0xFFU;
    Context->PdStatus1 = 0xFFU;
    Context->DataRoleEvents = Context->DataRoleChanges = Context->OvercurrentEvents = 0UL;
    Context->DetachRechecks = 0UL;
    Context->DataRoleStatus = STATUS_DEVICE_NOT_READY;
    Context->MaxInterruptCount = Context->MaxInterruptFailures = 0UL;
    Context->MaxLastInterruptStatus = Context->OpcodeCompletions = 0UL;
    Context->OpcodeUnexpected = Context->IrqFrameOverflows = 0UL;
    Context->IrqFrameRead = Context->IrqFrameCount = 0UL;
    Context->MfdReady = Context->UsbcInitialized = FALSE;
    Context->HsI2cInterruptCount = 0UL;
    Context->HsI2cCompletionCount = 0UL;
    Context->HsI2cLastInterruptStatus = 0UL;
    Context->CcStatusValid = FALSE;
    RtlZeroMemory(&Context->EventLog, sizeof(Context->EventLog));
    Context->EventLog.Version = B2L_MAX_EVENT_VERSION;
    Context->EventLog.Size = (ULONG)sizeof(Context->EventLog);
    Context->EventLog.StartTime = KeQueryInterruptTime();
    Context->EventLogDirty = TRUE;
    Context->EventCcValid = FALSE;
    Context->EventLastCc = 0U;
    Context->HsI2cReadFailures = 0UL;
    Context->HsI2cLastReadFailureStatus = 0UL;
    Context->HsI2cLastReadFailureLength = 0UL;
    Context->HsI2cLastReadFailureTransferred = 0UL;
    Context->CcStateChanged = FALSE;
    Context->DiagnosticDirty = TRUE;
    Context->UicInterrupt = 0U;
    Context->CcInterrupt = 0U;
    Context->PdInterrupt = 0U;
    Context->VdmInterrupt = 0U;
    Context->UicInterruptSeen = 0U;
    Context->CcInterruptSeen = 0U;
    Context->PdInterruptSeen = 0U;
    Context->VdmInterruptSeen = 0U;
    Context->TopInterruptSource = 0U;
    Context->TopInterruptSourceSeen = 0U;
    Context->CcState = B2L_MAX_CC_NO_CONNECTION;
    Context->PreviousCcState = B2L_MAX_CC_NO_CONNECTION;
    Context->CcSampleCount = 0UL;
    Context->CcTransitionCount = 0UL;
    Context->CcInterruptCount = 0UL;
    Context->CcSourceEventCount = 0UL;
    Context->DiagnosticPollCount = 0UL;
    Context->CcLastSampleTime = 0ULL;
    Context->CcLastTransitionTime = 0ULL;
    Context->PolicyEnabled = FALSE;
    // Prepare clocks/pins and mask stale controller status BEFORE Windows
    // enables the hardware IRQ. No MAX transaction is made here.
    Status = B2lMaxHsI2cInitialize(Context);
    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_BUS, Status);
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxPrepareGpio(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_GPIO_IRQ, Status);
    }
    WdfWaitLockRelease(Context->BusLock);
    return Status;
}

NTSTATUS
B2lMaxEvtD0EntryPostInterruptsEnabled(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Context->PolicyEnabled = TRUE;
    WdfWaitLockRelease(Context->MfdLock);
    WdfWaitLockRelease(Context->BusLock);
    WdfWorkItemEnqueue(Context->PolicyWork);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtD0ExitPreInterruptsDisabled(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS MuicStatus;

    UNREFERENCED_PARAMETER(TargetState);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Context->PolicyEnabled = FALSE;
    WdfWaitLockRelease(Context->MfdLock);
    WdfWaitLockRelease(Context->BusLock);
    WdfWorkItemFlush(Context->PolicyWork);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (Context->HardwareReady && Context->PlatformPrepared) {
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        Status = B2lMaxDisableOtg(Context);
        WdfWaitLockRelease(Context->MfdLock);
        if (!NT_SUCCESS(Status)) {
            B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_OTG_OFF, Status);
        }
        if (Context->IdentityReady) {
            MuicStatus = B2lMaxSetMuicPath(Context, MAX77705_MUIC_COM_OPEN);
            if (NT_SUCCESS(Status)) {
                Status = MuicStatus;
                if (!NT_SUCCESS(Status)) {
                    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_MUIC_OPEN, Status);
                }
            }
        }
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        B2lMaxGpioMask(Context);
        Context->MfdReady = FALSE;
        // Do not overwrite a previous mailbox failure with a shutdown code.
        if (Context->OpcodeHead != NULL) {
            B2lMaxCancelOpcodesLocked(Context, STATUS_DEVICE_NOT_READY);
        }
        WdfWaitLockAcquire(Context->TransportLock, NULL);
        B2lMaxHsI2cQuiesce(Context);
        WdfWaitLockRelease(Context->TransportLock);
        WdfWaitLockRelease(Context->MfdLock);
    }
    (VOID)WdfTimerStop(Context->OpcodeDeadline, TRUE);
    Context->UsbcInitialized = FALSE;
    Context->IdentityReady = FALSE;
    Context->BaselineValid = FALSE;
    Context->ChargerUnlocked = FALSE;
    Context->AutoVbusDisabled = FALSE;
    Context->AutoIbusConfigured = FALSE;
    Context->UsbcMasksConfigured = FALSE;
    Context->UsbcRuntimeMasksConfigured = FALSE;
    Context->CcControlConfigured = FALSE;
    Context->CcOpenNormalized = FALSE;
    Context->Control3Before = 0xFFU;
    Context->Control3After = 0xFFU;
    Context->MuicPathVerified = FALSE;
    Context->MuicPathRequested = 0xFFU;
    Context->MuicControl1 = 0xFFU;
    WdfWaitLockRelease(Context->BusLock);
    return Status;
}

// Copy the IRQ's actual status snapshot into the photo log, without another
// read-to-clear access or making failed bytes look like register contents.
static VOID
B2lMaxRecordIrqFrame(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame)
{
    B2L_MAX_EVENT_LOG *Log = &Context->EventLog;
    B2L_MAX_EVENT *Event;
    ULONG Index;

    if (Log->Count < B2L_MAX_EVENT_FIRST) {
        Index = Log->Count;
    } else {
        Index = B2L_MAX_EVENT_FIRST + Log->NextTail;
        Log->NextTail = (Log->NextTail + 1UL) % B2L_MAX_EVENT_TAIL;
    }
    if (Log->Count < B2L_MAX_EVENT_CAPACITY) ++Log->Count;
    else ++Log->Overwritten;
    Event = &Log->Events[Index];
    RtlZeroMemory(Event, sizeof(*Event));
    Event->Time = Frame->Time;
    Event->Sequence = ++Log->Total;
    Event->Origin = B2L_MAX_EVENT_IRQ;
    Event->Opcode = 0xFFUL;
    Event->PolicyCc = 0xFFFFFFFFUL;
    Event->Interrupts = (ULONG)Frame->Raw[0] | ((ULONG)Frame->Raw[1] << 8) |
        ((ULONG)Frame->Raw[2] << 16) | ((ULONG)Frame->Raw[3] << 24);
    Event->ReadStatus = (ULONG)Frame->SnapshotStatus;
    Event->Valid = NT_SUCCESS(Frame->SnapshotStatus);
    if (Event->Valid) RtlCopyMemory(Event->Status, Frame->Status, sizeof(Event->Status));
    else if (Frame->SnapshotAttempted) ++Log->SnapshotFailures;
    Log->OpcodeUicSeen |= Frame->Raw[0];
    if ((Frame->Raw[0] & MAX77705_USBC_INT_SYSMSG) != 0U) ++Log->SysmsgEvents;
    if ((Frame->Raw[1] & MAX77705_CC_INT_ATTACH_ERROR) != 0U) ++Log->AttachErrorEvents;
    Context->EventLogDirty = Context->DiagnosticDirty = TRUE;
}

VOID
B2lMaxEvtPolicyWork(WDFWORKITEM WorkItem)
{
    WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    B2L_MAX_IRQ_FRAME Frame;
    ULONG Budget = B2L_MAX_IRQ_FRAMES;
    NTSTATUS Status;

    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (!Context->PolicyEnabled || !Context->HardwareReady) goto Exit;
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Status = Context->MfdFault;
    WdfWaitLockRelease(Context->MfdLock);
    if (!Context->UsbcInitialized && NT_SUCCESS(Status)) {
        Status = B2lMaxServiceConnection(Context);
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        if (!NT_SUCCESS(Status) && !Context->UsbcInitialized) {
            B2lMaxCancelOpcodesLocked(Context, Status);
            B2lMaxGpioMask(Context);
            B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_MFD_IRQ, Status);
        } else if (NT_SUCCESS(Context->MfdFault)) {
            B2lMaxSetDiagnosticLocked(Context, B2L_MAX_STAGE_READY, Status);
        }
        WdfWaitLockRelease(Context->MfdLock);
    }
    while (Budget-- != 0UL) {
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        if (Context->IrqFrameCount == 0UL) {
            WdfWaitLockRelease(Context->MfdLock);
            break;
        }
        Frame = Context->IrqFrames[Context->IrqFrameRead];
        Context->IrqFrameRead = (Context->IrqFrameRead + 1UL) % B2L_MAX_IRQ_FRAMES;
        --Context->IrqFrameCount;
        WdfWaitLockRelease(Context->MfdLock);
        B2lMaxRecordIrqFrame(Context, &Frame);
    }
    // A bounded drain must arrange another run if the ISR refilled the ring.
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    if (Context->IrqFrameCount != 0UL) WdfWorkItemEnqueue(Context->PolicyWork);
    WdfWaitLockRelease(Context->MfdLock);
    B2lMaxPublishDiagnostic(Context, 0UL, STATUS_SUCCESS);
Exit:
    WdfWaitLockRelease(Context->BusLock);
}
