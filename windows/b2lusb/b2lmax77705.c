#include "b2lmax77705.h"

#define MAX77705_TOP_PMIC_ID1      0x00U
#define MAX77705_TOP_PMIC_REV      0x01U

#define MAX77705_USBC_HW_REV       0x00U
#define MAX77705_USBC_FW_REV       0x01U
#define MAX77705_USBC_BC_STATUS    0x08U
#define MAX77705_USBC_FW_MINOR     0x09U
#define MAX77705_USBC_CC_STATUS0   0x0AU
#define MAX77705_USBC_CC_STATUS1   0x0BU

#define MAX77705_CHG_INT_MASK      0xB1U
#define MAX77705_CHG_INT_OK        0xB2U
#define MAX77705_CHG_DETAILS_00    0xB3U
#define MAX77705_CHG_DETAILS_01    0xB4U
#define MAX77705_CHG_DETAILS_02    0xB5U
#define MAX77705_CHG_CNFG_00       0xB7U

#define MAX77705_CHG_MODE_MASK     0x0FU
#define MAX77705_CHG_OTG_INT_MASK  0x51U
#define MAX77705_CHG_OTG_INT_VALUE 0x50U

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
        L"B2LMaxChgDetails2", L"B2LMaxOtgEnabled"
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
B2lMaxProbeIdentity(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

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
    Context->ChargerIntMaskBaseline = Context->ChargerIntMask;
    Context->ChargerModeBaseline = Context->ChargerMode;
    Context->BaselineValid = TRUE;
    Context->IdentityReady = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxReadStatus(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;

    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_CC_STATUS0,
        &Context->CcStatus0
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lMaxReadByte(
        Context,
        B2L_MAX_USBC_ADDRESS,
        MAX77705_USBC_CC_STATUS1,
        &Context->CcStatus1
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
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

    if (!Context->OtgEnabled) {
        return STATUS_SUCCESS;
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
        return Status;
    }
    CcState = (UCHAR)(Context->CcStatus0 & B2L_MAX_CC_STATUS_MASK);
    if (CcState == B2L_MAX_CC_SOURCE) {
        if (!Context->OtgEnabled) {
            Status = B2lMaxEnableOtg(Context);
            B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_OTG_ON, Status);
            return Status;
        }
        return STATUS_SUCCESS;
    }
    if (Context->OtgEnabled &&
        ((CcState == B2L_MAX_CC_NO_CONNECTION) ||
         (CcState == B2L_MAX_CC_SINK))) {
        Status = B2lMaxDisableOtg(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_OTG_OFF, Status);
        return Status;
    }
    return STATUS_SUCCESS;
}

static VOID
B2lMaxUnmapResources(PB2L_MAX_CONTEXT Context)
{
    if (Context->HsI2cRegisters != NULL) {
        MmUnmapIoSpace(Context->HsI2cRegisters, Context->HsI2cLength);
        Context->HsI2cRegisters = NULL;
    }
    if (Context->UsiRegisters != NULL) {
        MmUnmapIoSpace(Context->UsiRegisters, Context->UsiLength);
        Context->UsiRegisters = NULL;
    }
    Context->HsI2cLength = 0UL;
    Context->UsiLength = 0UL;
    Context->HardwareReady = FALSE;
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

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Status = WdfWaitLockCreate(&Attributes, &Context->BusLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &TimerConfig,
        B2lMaxEvtPollTimer,
        B2L_MAX_POLL_MS
        );
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
        if (Resource->u.Memory.Length != B2L_MAX_DEVICE_PAGE_LENGTH) {
            B2lMaxUnmapResources(Context);
            B2lMaxPublishDiagnostic(
                Context,
                B2L_MAX_STAGE_RESOURCES,
                STATUS_DEVICE_CONFIGURATION_ERROR
                );
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        ++MemoryCount;
        Start = (ULONGLONG)Resource->u.Memory.Start.QuadPart;
        if ((Start == B2L_MAX_HSI2C_PHYSICAL_BASE) &&
            (Context->HsI2cRegisters == NULL)) {
            Context->HsI2cRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->HsI2cLength = Resource->u.Memory.Length;
        } else if ((Start == B2L_MAX_USI_PHYSICAL_BASE) &&
                   (Context->UsiRegisters == NULL)) {
            Context->UsiRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                Resource->u.Memory.Length,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->UsiLength = Resource->u.Memory.Length;
        } else {
            B2lMaxUnmapResources(Context);
            B2lMaxPublishDiagnostic(
                Context,
                B2L_MAX_STAGE_RESOURCES,
                STATUS_DEVICE_CONFIGURATION_ERROR
                );
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    if ((MemoryCount != 2UL) || (Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL)) {
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
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (Context->HardwareReady) {
        (VOID)B2lMaxDisableOtg(Context);
        B2lMaxHsI2cQuiesce(Context);
    }
    B2lMaxUnmapResources(Context);
    Context->IdentityReady = FALSE;
    Context->BaselineValid = FALSE;
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
    Status = B2lMaxHsI2cInitialize(Context);
    B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_BUS, Status);
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxProbeIdentity(Context);
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_IDENTITY, Status);
    }
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxEvaluateConnection(Context);
        if (NT_SUCCESS(Status)) {
            B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_READY, Status);
        }
    }
    WdfWaitLockRelease(Context->BusLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    (VOID)WdfTimerStart(
        Context->PollTimer,
        WDF_REL_TIMEOUT_IN_MS(B2L_MAX_POLL_MS)
        );
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxEvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(TargetState);
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (Context->HardwareReady) {
        (VOID)B2lMaxDisableOtg(Context);
        B2lMaxHsI2cQuiesce(Context);
    }
    WdfWaitLockRelease(Context->BusLock);
    return STATUS_SUCCESS;
}

VOID
B2lMaxEvtPollTimer(WDFTIMER Timer)
{
    WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);
    UCHAR PreviousCc;
    BOOLEAN PreviousOtg;
    NTSTATUS Status;

    WdfWaitLockAcquire(Context->BusLock, NULL);
    if (!Context->HardwareReady || !Context->IdentityReady) {
        WdfWaitLockRelease(Context->BusLock);
        return;
    }
    PreviousCc = Context->CcStatus0;
    PreviousOtg = Context->OtgEnabled;
    Status = B2lMaxEvaluateConnection(Context);
    if (!NT_SUCCESS(Status) || (PreviousCc != Context->CcStatus0) ||
        (PreviousOtg != Context->OtgEnabled)) {
        B2lMaxPublishDiagnostic(Context, B2L_MAX_STAGE_CC, Status);
    }
    WdfWaitLockRelease(Context->BusLock);
}
