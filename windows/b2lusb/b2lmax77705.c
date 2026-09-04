#include "b2lmax77705.h"

#define MAX77705_TOP_PMIC_ID1      0x00U
#define MAX77705_TOP_PMIC_REV      0x01U
#define MAX77705_TOP_INTSRC        0x22U
#define MAX77705_TOP_INTSRC_MASK   0x23U

#define MAX77705_USBC_HW_REV       0x00U
#define MAX77705_USBC_FW_REV       0x01U
#define MAX77705_USBC_BC_STATUS    0x08U
#define MAX77705_USBC_FW_MINOR     0x09U
#define MAX77705_USBC_CC_STATUS0   0x0AU
#define MAX77705_USBC_CC_STATUS1   0x0BU
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
#define MAX77705_OPCODE_POLL_COUNT       300UL
#define MAX77705_OPCODE_POLL_US          10000UL

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
        L"B2LMaxControl3Before", L"B2LMaxControl3After"
    };
    ULONG Values[RTL_NUMBER_OF(Names)];
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ServicePath;
    UNICODE_STRING ValueName;
    HANDLE Key = NULL;
    ULONG Index;

    Context->DiagnosticStage = Stage;
    Context->DiagnosticStatus = Status;
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
    ZwClose(Key);
    Context->DiagnosticDirty = FALSE;
    Context->DiagnosticPollCount = 0UL;
}

static NTSTATUS
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

static NTSTATUS
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

static NTSTATUS
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

static NTSTATUS
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

static NTSTATUS
B2lMaxExchangeOpcode(
    PB2L_MAX_CONTEXT Context,
    UCHAR Opcode,
    const UCHAR *WriteData,
    ULONG WriteDataLength,
    ULONG ResponseDataLength,
    UCHAR *ResponseData
    )
{
    UCHAR Command[2];
    UCHAR InterruptStatus = 0U;
    UCHAR Response[MAX77705_OPCODE_RESPONSE_LENGTH];
    ULONG ResponseLength;
    ULONG Poll;
    NTSTATUS Status;

    if ((WriteDataLength > 1UL) || (ResponseDataLength > 1UL) ||
        ((WriteDataLength != 0UL) && (WriteData == NULL)) ||
        ((ResponseDataLength != 0UL) && (ResponseData == NULL))) {
        return STATUS_INVALID_PARAMETER;
    }
    ResponseLength = 1UL + ResponseDataLength;
    RtlZeroMemory(Response, sizeof(Response));

    // REG_UIC_INT is read-to-clear.  Drain any stale APCmdRes before sending
    // a Samsung opcode, then wait for this command's APCmdRes response.
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_UIC_INT,
        &InterruptStatus
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Command[0] = Opcode;
    if (WriteDataLength != 0UL) {
        Command[1] = *WriteData;
    }
    Status = B2lMaxWriteBlock(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_OPCODE_WRITE,
        Command,
        1UL + WriteDataLength
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxWriteByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_OPCODE_END,
        0x00U
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    for (Poll = 0UL; Poll < MAX77705_OPCODE_POLL_COUNT; ++Poll) {
        Status = B2lMaxReadByte(
            Context,
            B2L_MAX_USBC_ADDRESS,
            MAX77705_USBC_UIC_INT,
            &InterruptStatus
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        if ((InterruptStatus & MAX77705_USBC_INT_APCMDRES) != 0U) {
            Status = B2lMaxReadBlock(
                Context,
                B2L_MAX_USBC_ADDRESS,
                MAX77705_USBC_OPCODE_READ,
                Response,
                ResponseLength
                );
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
            if (Response[0] != Opcode) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            if (ResponseDataLength != 0UL) {
                *ResponseData = Response[1];
            }
            return STATUS_SUCCESS;
        }
        B2lMaxDelayMicroseconds(MAX77705_OPCODE_POLL_US);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
B2lMaxSendOpcode(
    PB2L_MAX_CONTEXT Context,
    UCHAR Opcode,
    UCHAR Data,
    ULONG ResponseDataLength,
    UCHAR *ResponseData
    )
{
    return B2lMaxExchangeOpcode(
        Context,
        Opcode,
        &Data,
        1UL,
        ResponseDataLength,
        ResponseData
        );
}

static NTSTATUS
B2lMaxSetMuicPath(PB2L_MAX_CONTEXT Context, UCHAR Path)
{
    NTSTATUS Status;

    Context->MuicPathRequested = Path;
    Context->MuicPathVerified = FALSE;

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
B2lMaxDisableAutoVbus(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->AutoVbusDisabled = FALSE;
    Context->OpcodeResponse = 0xFFU;
    Status = B2lMaxSendOpcode(
        Context,
        MAX77705_OPCODE_FACTORY_TEST,
        MAX77705_FACTORY_AUTO_VBUS_OFF,
        1UL,
        &Context->OpcodeResponse
        );
    if (NT_SUCCESS(Status)) {
        Context->AutoVbusDisabled = TRUE;
    }
    return Status;
}

static NTSTATUS
B2lMaxConfigureAutoIbus(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->AutoIbusConfigured = FALSE;
    Context->AutoIbusMode = 0xFFU;
    // Samsung's Exynos PD initialization uses AUTOIBUS_AT_OFF: USBC firmware
    // remains active, while charger input-bus automation is handed to the OS.
    // This is deliberately 0x01, not the Snapdragon UEFI log's stronger
    // FW_AT_OFF (0x03), because Windows still relies on MAX77705 CC firmware.
    Status = B2lMaxSendOpcode(
        Context,
        MAX77705_OPCODE_FW_AUTOIBUS,
        MAX77705_AUTOIBUS_AT_OFF,
        0UL,
        NULL
        );
    if (NT_SUCCESS(Status)) {
        Context->AutoIbusMode = MAX77705_AUTOIBUS_AT_OFF;
        Context->AutoIbusConfigured = TRUE;
    }
    return Status;
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
B2lMaxReadAndClearUsbcInterrupts(PB2L_MAX_CONTEXT Context)
{
    UCHAR Interrupts[4];
    NTSTATUS Status;

    // Match the Samsung parent IRQ thread ordering: sample TOP INTSRC first,
    // then fetch the four contiguous, read-to-clear USBC interrupt bytes in
    // one transaction.  ACPI currently has no verified MAX77705 GPIO IRQ, so
    // the passive timer is the delivery mechanism; sticky copies preserve
    // short events after the live register sample has been cleared.
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_TOP_ADDRESS,
        MAX77705_TOP_INTSRC,
        &Context->TopInterruptSource
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Context->TopInterruptSourceSeen |= Context->TopInterruptSource;
    Status = B2lMaxReadBlock(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_UIC_INT,
        Interrupts,
        RTL_NUMBER_OF(Interrupts)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Context->UicInterrupt = Interrupts[0];
    Context->CcInterrupt = Interrupts[1];
    Context->PdInterrupt = Interrupts[2];
    Context->VdmInterrupt = Interrupts[3];
    Context->UicInterruptSeen |= Context->UicInterrupt;
    Context->CcInterruptSeen |= Context->CcInterrupt;
    Context->PdInterruptSeen |= Context->PdInterrupt;
    Context->VdmInterruptSeen |= Context->VdmInterrupt;
    if (Context->CcInterrupt != 0U) {
        ++Context->CcInterruptCount;
    }
    if ((Context->TopInterruptSource != 0U) ||
        (Context->UicInterrupt != 0U) ||
        (Context->CcInterrupt != 0U) ||
        (Context->PdInterrupt != 0U) ||
        (Context->VdmInterrupt != 0U)) {
        Context->DiagnosticDirty = TRUE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxReadCcStatus(PB2L_MAX_CONTEXT Context)
{
    UCHAR StatusBytes[2];
    UCHAR NewState;
    ULONGLONG SampleTime;
    NTSTATUS Status;

    // Linux's CCStat nested handler reads CC_STATUS0 immediately after the
    // parent has drained CC_INT.  Keep that ordering so a transient role is
    // not delayed behind unrelated charger-register traffic.
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
        if (NewState == B2L_MAX_CC_SOURCE) {
            ++Context->CcSourceEventCount;
        }
    } else if (NewState != Context->CcState) {
        Context->PreviousCcState = Context->CcState;
        Context->CcState = NewState;
        Context->CcStateChanged = TRUE;
        Context->CcLastTransitionTime = SampleTime;
        ++Context->CcTransitionCount;
        Context->DiagnosticDirty = TRUE;
        if (NewState == B2L_MAX_CC_SOURCE) {
            ++Context->CcSourceEventCount;
        }
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
B2lMaxConfigureUsbcMasks(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->UsbcMasksConfigured = FALSE;
    Context->UsbcRuntimeMasksConfigured = FALSE;
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_TOP_ADDRESS,
        MAX77705_TOP_INTSRC_MASK,
        &Context->TopInterruptSourceMask
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadAndClearUsbcInterrupts(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxWriteUsbcMasks(
        Context,
        MAX77705_UIC_INT_MASK_LINUX,
        MAX77705_CC_INT_MASK_LINUX,
        MAX77705_PD_INT_MASK_PROBE,
        MAX77705_VDM_INT_MASK_PROBE
        );
    if (NT_SUCCESS(Status)) {
        Context->UsbcMasksConfigured = TRUE;
    }
    return Status;
}

static NTSTATUS
B2lMaxConfigureRuntimeUsbcMasks(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->UsbcRuntimeMasksConfigured = FALSE;
    Status = B2lMaxWriteUsbcMasks(
        Context,
        MAX77705_UIC_INT_MASK_LINUX,
        MAX77705_CC_INT_MASK_LINUX,
        MAX77705_PD_INT_MASK_RUNTIME,
        MAX77705_VDM_INT_MASK_RUNTIME
        );
    if (NT_SUCCESS(Status)) {
        Context->UsbcRuntimeMasksConfigured = TRUE;
    }
    return Status;
}

static NTSTATUS
B2lMaxConfigureCcControl(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Context->CcControlConfigured = FALSE;
    Context->CcControlResponse = 0xFFU;
    Context->CcControlReadBack = 0xFFU;
    Status = B2lMaxSendOpcode(
        Context,
        MAX77705_OPCODE_CCCTRL1_WRITE,
        MAX77705_CCCTRL1_BEYOND2LTE,
        1UL,
        &Context->CcControlResponse
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxExchangeOpcode(
        Context,
        MAX77705_OPCODE_CCCTRL1_READ,
        NULL,
        0UL,
        1UL,
        &Context->CcControlReadBack
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if (Context->CcControlReadBack != MAX77705_CCCTRL1_BEYOND2LTE) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    Context->CcControlConfigured = TRUE;
    return STATUS_SUCCESS;
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
    Context->BaselineValid = TRUE;
    Context->IdentityReady = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxReadStatus(PB2L_MAX_CONTEXT Context)
{
    BOOLEAN NeedDetails;
    NTSTATUS Status;

    NeedDetails = (BOOLEAN)!Context->CcStatusValid;
    Status = B2lMaxReadAndClearUsbcInterrupts(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadCcStatus(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    NeedDetails = (BOOLEAN)(NeedDetails || Context->CcStateChanged ||
        (Context->TopInterruptSource != 0U) ||
        (Context->UicInterrupt != 0U) ||
        (Context->CcInterrupt != 0U) ||
        (Context->PdInterrupt != 0U) ||
        (Context->VdmInterrupt != 0U) ||
        (Context->CcState != B2L_MAX_CC_NO_CONNECTION) ||
        (Context->DiagnosticPollCount >=
         (B2L_MAX_IDLE_DIAGNOSTIC_POLLS - 1UL)));
    if (!NeedDetails) {
        return STATUS_SUCCESS;
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
    Context->OtgHardwareActive =
        (((Context->ChargerMode & MAX77705_CHG_MODE_MASK) ==
          MAX77705_MODE_BOOST_OTG) ||
         ((Context->ChargerMode & MAX77705_CHG_MODE_MASK) ==
          MAX77705_MODE_BUCK_OTG) ||
         ((Context->ChargerMode & MAX77705_CHG_MODE_MASK) ==
          MAX77705_MODE_BUCK_CHG_OTG));
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_BC_STATUS,
        &Context->BcStatus
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_INT_OK,
        &Context->ChargerIntOk
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_DETAILS_00,
        &Context->ChargerDetails[0]
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_DETAILS_01,
        &Context->ChargerDetails[1]
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    return B2lMaxReadByte(
        Context,
        B2L_MAX_CHARGER_ADDRESS,
        MAX77705_CHG_DETAILS_02,
        &Context->ChargerDetails[2]
        );
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
B2lMaxEvaluateConnection(PB2L_MAX_CONTEXT Context)
{
    UCHAR CcState;
    NTSTATUS Status;

    Status = B2lMaxReadStatus(Context);
    if (!NT_SUCCESS(Status)) {
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_CC, Status);
        return Status;
    }
    CcState = Context->CcState;
    if (CcState == B2L_MAX_CC_SOURCE) {
        // Samsung's OTG/RpRd attach path routes D+/D- to the AP with
        // CONTROL1_WRITE(COM_USB) before the USB host path is useful.  VBUS
        // remains gated until CONTROL1_READ proves that physical route.
        if (!Context->MuicPathVerified ||
            (Context->MuicControl1 != MAX77705_MUIC_COM_USB_AP)) {
            Status = B2lMaxSetMuicPath(
                Context,
                MAX77705_MUIC_COM_USB_AP
                );
            B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_MUIC_USB, Status);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }
        if (!Context->OtgEnabled) {
            Status = B2lMaxEnableOtg(Context);
            B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_OTG_ON, Status);
            return Status;
        }
        return STATUS_SUCCESS;
    }
    // Fixed HOST permits VBUS in exactly one CC state.  This also shuts down
    // OTG inherited from boot firmware when the current cable is detached,
    // sink, audio/debug accessory, disabled or erroneous.
    if ((CcState != B2L_MAX_CC_SOURCE) &&
        (Context->OtgEnabled || Context->OtgHardwareActive)) {
        Status = B2lMaxDisableOtg(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_OTG_OFF, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    // Mirror Samsung detach/logical-detach: remove VBUS first, then isolate
    // D+/D-.  Also establish a verified OPEN baseline on the first no-cable
    // sample so boot-firmware switch state is never assumed.
    if (!Context->MuicPathVerified ||
        (Context->MuicControl1 != MAX77705_MUIC_COM_OPEN)) {
        Status = B2lMaxSetMuicPath(Context, MAX77705_MUIC_COM_OPEN);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_MUIC_OPEN, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxServiceConnection(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    if (!Context->HardwareReady) {
        return STATUS_DEVICE_NOT_READY;
    }

    // Samsung brings MAX77705 up as an MFD and performs CC/charger work
    // asynchronously.  Keep the same recovery boundary here: a transient
    // bus/identity failure is retried by the passive timer instead of
    // permanently failing the ACPI parent and all of its _DEP children.
    if (!Context->IdentityReady) {
        Status = B2lMaxHsI2cInitialize(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_BUS, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        Status = B2lMaxProbeIdentity(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_IDENTITY, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->ChargerUnlocked) {
        Status = B2lMaxUnlockCharger(Context);
        B2lMaxPublishDiagnostic(
            Context,
            B2L_MAX_STAGE_CHARGER_UNLOCK,
            Status
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    // Preserve Samsung's probe order: PD initializes AUTOIBUS, the probe
    // masks cover opcode setup, then the runtime mask vector is installed.
    if (!Context->AutoIbusConfigured) {
        Status = B2lMaxConfigureAutoIbus(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_AUTO_IBUS, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->UsbcMasksConfigured) {
        Status = B2lMaxConfigureUsbcMasks(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_USBC_MASKS, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->AutoVbusDisabled) {
        Status = B2lMaxDisableAutoVbus(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_AUTO_VBUS, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->CcControlConfigured) {
        Status = B2lMaxConfigureCcControl(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_CC_CONTROL, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->UsbcRuntimeMasksConfigured) {
        Status = B2lMaxConfigureRuntimeUsbcMasks(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_RUNTIME_MASKS, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    if (!Context->CcOpenNormalized) {
        Status = B2lMaxNormalizeCcOpen(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_CC_OPEN, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    Status = B2lMaxEvaluateConnection(Context);
    if (NT_SUCCESS(Status)) {
        ++Context->DiagnosticPollCount;
    }
    if (NT_SUCCESS(Status) &&
        (Context->DiagnosticDirty ||
         (Context->DiagnosticStage != B2L_MAX_STAGE_READY) ||
         (Context->CcState != B2L_MAX_CC_NO_CONNECTION) ||
         (Context->DiagnosticPollCount >= B2L_MAX_IDLE_DIAGNOSTIC_POLLS))) {
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_READY, Status);
    }
    return Status;
}

static VOID
B2lMaxUnmapResources(PB2L_MAX_CONTEXT Context)
{
    Context->PollingEnabled = FALSE;
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
    WDF_TIMER_CONFIG TimerConfig;
    WDFDEVICE Device;
    PB2L_MAX_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PowerCallbacks);
    PowerCallbacks.EvtDevicePrepareHardware = B2lMaxEvtPrepareHardware;
    PowerCallbacks.EvtDeviceReleaseHardware = B2lMaxEvtReleaseHardware;
    PowerCallbacks.EvtDeviceD0Entry = B2lMaxEvtD0Entry;
    PowerCallbacks.EvtDeviceD0Exit = B2lMaxEvtD0Exit;
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

    // Passive-level KMDF timers must be one-shot (Period == 0).  The timer
    // callback rearms itself after each serialized MAX77705 status sample.
    WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lMaxEvtPollTimer);
    TimerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfTimerCreate(
        &TimerConfig,
        &Attributes,
        &Context->PollTimer
        );
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
    ULONG Count;
    ULONG Index;
    ULONG MemoryCount = 0UL;
    ULONGLONG Start;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    B2lMaxUnmapResources(Context);
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
    if ((MemoryCount != 4UL) || (Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL) ||
        (Context->CmgpCmuRegisters == NULL) ||
        (Context->CmgpPinctrlRegisters == NULL)) {
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
    Context->PollingEnabled = FALSE;
    WdfWaitLockRelease(Context->BusLock);
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (Context->HardwareReady) {
        (VOID)B2lMaxDisableOtg(Context);
        if (Context->IdentityReady) {
            (VOID)B2lMaxSetMuicPath(Context, MAX77705_MUIC_COM_OPEN);
        }
        B2lMaxHsI2cQuiesce(Context);
    }
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

    UNREFERENCED_PARAMETER(PreviousState);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    Context->CcStatusValid = FALSE;
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
    Context->PollingEnabled = TRUE;
    WdfWaitLockRelease(Context->BusLock);
    // Samsung defers CC/charger work after probe.  Do the same on Windows:
    // never hold the single active CPU or the PnP D0 path while a failed
    // HSI2C transaction reaches its bounded timeout.
    (VOID)WdfTimerStart(
        Context->PollTimer,
        WDF_REL_TIMEOUT_IN_MS(B2L_MAX_INITIAL_POLL_MS)
        );
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(TargetState);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    Context->PollingEnabled = FALSE;
    WdfWaitLockRelease(Context->BusLock);
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (Context->HardwareReady) {
        (VOID)B2lMaxDisableOtg(Context);
        if (Context->IdentityReady) {
            (VOID)B2lMaxSetMuicPath(Context, MAX77705_MUIC_COM_OPEN);
        }
        B2lMaxHsI2cQuiesce(Context);
    }
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

VOID
B2lMaxEvtPollTimer(WDFTIMER Timer)
{
    WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    ULONG NextPollMs;
    NTSTATUS Status;

    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (!Context->PollingEnabled || !Context->HardwareReady) {
        WdfWaitLockRelease(Context->BusLock);
        return;
    }
    // ServiceConnection publishes the precise failed stage (BUS, IDENTITY,
    // CC or OTG).  Do not overwrite a retry failure with a generic CC stage.
    Status = B2lMaxServiceConnection(Context);
    if (!NT_SUCCESS(Status)) {
        NextPollMs = B2L_MAX_RETRY_POLL_MS;
    } else if (Context->CcStatusValid &&
               (Context->CcState == B2L_MAX_CC_NO_CONNECTION) &&
               (Context->CcSampleCount <= B2L_MAX_ATTACH_FAST_POLLS)) {
        // With no verified GPA1_6 ACPI interrupt resource, approximate the
        // Linux CCStat wakeup only for a bounded post-init attach window.
        // A stable role or an expired window returns to the 200 ms health
        // cadence; late physical attach remains observable without a
        // permanent 50 Hz polling load.
        NextPollMs = B2L_MAX_ATTACH_POLL_MS;
    } else {
        NextPollMs = B2L_MAX_POLL_MS;
    }
    if (Context->PollingEnabled && Context->HardwareReady) {
        (VOID)WdfTimerStart(
            Timer,
            WDF_REL_TIMEOUT_IN_MS(NextPollMs)
            );
    }
    WdfWaitLockRelease(Context->BusLock);
}
