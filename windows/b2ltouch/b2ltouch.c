#include "b2ltouch.h"

#define SEC_TS_CMD_SENSE_ON            0x10U
#define SEC_TS_READ_FIRMWARE_INTEGRITY 0x21U
#define SEC_TS_READ_DEVICE_ID          0x22U
#define SEC_TS_CMD_SET_TOUCHFUNCTION   0x30U
#define SEC_TS_READ_BOOT_STATUS        0x55U
#define SEC_TS_READ_ONE_EVENT          0x60U
#define SEC_TS_READ_ALL_EVENT          0x61U
#define SEC_TS_CMD_CLEAR_EVENT_STACK   0x62U

#define SEC_TS_STATUS_APP_MODE         0x20U
#define SEC_TS_DEFAULT_TOUCH_FUNCTIONS 0x0061U

#define B2L_TOUCH_REPORT_ID            1U
#define B2L_TOUCH_FEATURE_REPORT_ID    2U

#define B2L_TOUCH_STAGE_DEVICE_CREATED 0x10U
#define B2L_TOUCH_STAGE_TIMER_CREATED  0x20U
#define B2L_TOUCH_STAGE_VHF_CREATED    0x30U
#define B2L_TOUCH_STAGE_VHF_STARTED    0x40U
#define B2L_TOUCH_STAGE_RESOURCES      0x50U
#define B2L_TOUCH_STAGE_I2C_INIT       0x60U
#define B2L_TOUCH_STAGE_PROBE          0x61U
#define B2L_TOUCH_STAGE_CLEAR_EVENTS   0x62U
#define B2L_TOUCH_STAGE_FUNCTIONS      0x63U
#define B2L_TOUCH_STAGE_SENSE_ON       0x64U
#define B2L_TOUCH_STAGE_D0_READY       0x70U
#define B2L_TOUCH_STAGE_ADD_ENTERED    0x01U
#define B2L_TOUCH_STAGE_DEVICE_CREATE  0x02U

#define B2L_TOUCH_CONTACT_DESCRIPTOR                                      \
    0x05, 0x0D,             /* Usage Page (Digitizers) */                 \
    0x09, 0x22,             /* Usage (Finger) */                          \
    0xA1, 0x02,             /* Collection (Logical) */                   \
    0x09, 0x42,             /* Usage (Tip Switch) */                      \
    0x15, 0x00,                                                            \
    0x25, 0x01,                                                            \
    0x75, 0x01,                                                            \
    0x95, 0x01,                                                            \
    0x81, 0x02,                                                            \
    0x75, 0x01,                                                            \
    0x95, 0x07,                                                            \
    0x81, 0x03,             /* Seven padding bits */                     \
    0x75, 0x08,                                                            \
    0x95, 0x01,                                                            \
    0x09, 0x51,             /* Usage (Contact Identifier) */             \
    0x15, 0x00,                                                            \
    0x25, B2L_SEC_TS_MAX_TOUCH_ID,                                         \
    0x81, 0x02,                                                            \
    0x05, 0x01,             /* Usage Page (Generic Desktop) */            \
    0x15, 0x00,                                                            \
    0x26, 0xFF, 0x0F,       /* Logical Maximum (4095) */                  \
    0x35, 0x00,                                                            \
    0x55, 0x0E,             /* Unit Exponent (-2) */                      \
    0x65, 0x11,             /* Unit (centimeter, SI linear) */            \
    0x09, 0x30,             /* Usage (X) */                               \
    0x46, 0xAB, 0x02,       /* Physical Maximum (683 = 6.83 cm) */        \
    0x75, 0x10,                                                            \
    0x95, 0x01,                                                            \
    0x81, 0x02,                                                            \
    0x09, 0x31,             /* Usage (Y) */                               \
    0x46, 0xA3, 0x05,       /* Physical Maximum (1443 = 14.43 cm) */      \
    0x81, 0x02,                                                            \
    0xC0                    /* End Collection (Finger) */

/*
 * Parallel-mode Windows Touchscreen report.  Every embedded Finger logical
 * collection is one contact slot, and each submitted frame carries all live
 * contacts plus a one-frame Tip=0 record for contacts that just departed.
 */
static UCHAR B2lTouchReportDescriptor[] = {
    0x05, 0x0D,             /* Usage Page (Digitizers) */
    0x09, 0x04,             /* Usage (Touch Screen) */
    0xA1, 0x01,             /* Collection (Application) */
    0x85, B2L_TOUCH_REPORT_ID,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    B2L_TOUCH_CONTACT_DESCRIPTOR,
    0x05, 0x0D,
    0x55, 0x00,             /* Unit Exponent (0) */
    0x65, 0x00,             /* Unit (None) */
    0x09, 0x54,             /* Usage (Contact Count) */
    0x15, 0x00,
    0x25, B2L_TOUCH_MAX_CONTACTS,
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x02,
    0x85, B2L_TOUCH_FEATURE_REPORT_ID,
    0x09, 0x55,             /* Usage (Contact Count Maximum) */
    0x25, B2L_TOUCH_MAX_CONTACTS,
    0x75, 0x08,
    0x95, 0x01,
    0xB1, 0x02,             /* Feature (Data, Variable, Absolute) */
    0xC0
};

/*
 * UFS miniportundaki tani yaklasimi gibi, PnP'nin tek Code 31 sonucunu
 * hangi baslatma adiminin dondurdugunu gosteren iki REG_DWORD'a ayirir.
 * Kayit yalniz WinPE'nin gecici device registry anahtarina yazilir; MMIO'ya
 * veya kalici cihaza dokunmaz ve asil hata durumunu degistirmez.
 */
static VOID
B2lTouchPublishServiceDiagnostic(
    ULONG Stage,
    NTSTATUS Status
    )
{
    static const WCHAR ServicePathBuffer[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\b2ltouch";
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ServicePath;
    UNICODE_STRING StageName;
    UNICODE_STRING StatusName;
    HANDLE Key = NULL;

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

    RtlInitUnicodeString(&StageName, L"B2LTouchStage");
    RtlInitUnicodeString(&StatusName, L"B2LTouchStatus");
    (VOID)ZwSetValueKey(
        Key,
        &StageName,
        0UL,
        REG_DWORD,
        &Stage,
        (ULONG)sizeof(Stage)
        );
    (VOID)ZwSetValueKey(
        Key,
        &StatusName,
        0UL,
        REG_DWORD,
        &Status,
        (ULONG)sizeof(Status)
        );
    ZwClose(Key);
}

static VOID
B2lTouchPublishDiagnostic(
    WDFDEVICE Device,
    ULONG Stage,
    NTSTATUS Status
    )
{
    WDFKEY Key = NULL;
    UNICODE_STRING StageName;
    UNICODE_STRING StatusName;

    B2lTouchPublishServiceDiagnostic(Stage, Status);
    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(
            Device,
            PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE,
            WDF_NO_OBJECT_ATTRIBUTES,
            &Key))) {
        return;
    }

    RtlInitUnicodeString(&StageName, L"B2LTouchStage");
    RtlInitUnicodeString(&StatusName, L"B2LTouchStatus");
    (VOID)WdfRegistryAssignULong(Key, &StageName, Stage);
    (VOID)WdfRegistryAssignULong(Key, &StatusName, (ULONG)Status);
    WdfRegistryClose(Key);
}

static VOID
B2lTouchPublishDriverStatus(
    PUNICODE_STRING RegistryPath,
    NTSTATUS Status
    )
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    HANDLE Key = NULL;
    ULONG Value = (ULONG)Status;

    InitializeObjectAttributes(
        &Attributes,
        RegistryPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_SET_VALUE, &Attributes))) {
        return;
    }

    RtlInitUnicodeString(&ValueName, L"B2LDriverStatus");
    (VOID)ZwSetValueKey(
        Key,
        &ValueName,
        0UL,
        REG_DWORD,
        &Value,
        (ULONG)sizeof(Value)
        );
    ZwClose(Key);
}

static NTSTATUS
B2lTouchReadCommand(
    PB2L_TOUCH_CONTEXT Context,
    UCHAR Command,
    UCHAR *Data,
    ULONG DataLength
    )
{
    return B2lHsI2cWriteRead(Context, &Command, 1UL, Data, DataLength);
}

static NTSTATUS
B2lTouchWriteCommand(PB2L_TOUCH_CONTEXT Context, UCHAR Command)
{
    return B2lHsI2cWriteRead(Context, &Command, 1UL, NULL, 0UL);
}

static NTSTATUS
B2lTouchSetFunctions(PB2L_TOUCH_CONTEXT Context)
{
    UCHAR Command[3];

    Command[0] = SEC_TS_CMD_SET_TOUCHFUNCTION;
    Command[1] = (UCHAR)(SEC_TS_DEFAULT_TOUCH_FUNCTIONS & 0xFFU);
    Command[2] = (UCHAR)(SEC_TS_DEFAULT_TOUCH_FUNCTIONS >> 8);
    return B2lHsI2cWriteRead(
        Context,
        Command,
        (ULONG)sizeof(Command),
        NULL,
        0UL
        );
}

static NTSTATUS
B2lTouchProbe(PB2L_TOUCH_CONTEXT Context)
{
    UCHAR BootStatus;
    UCHAR DeviceId[5];
    UCHAR FirmwareIntegrity;
    ULONG Index;
    BOOLEAN AllZero = TRUE;
    BOOLEAN AllOnes = TRUE;
    NTSTATUS Status;

    Status = B2lTouchReadCommand(
        Context,
        SEC_TS_READ_DEVICE_ID,
        DeviceId,
        (ULONG)sizeof(DeviceId)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    for (Index = 0UL; Index < sizeof(DeviceId); ++Index) {
        AllZero = (BOOLEAN)(AllZero && (DeviceId[Index] == 0x00U));
        AllOnes = (BOOLEAN)(AllOnes && (DeviceId[Index] == 0xFFU));
    }
    if (AllZero || AllOnes) {
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = B2lTouchReadCommand(
        Context,
        SEC_TS_READ_FIRMWARE_INTEGRITY,
        &FirmwareIntegrity,
        (ULONG)sizeof(FirmwareIntegrity)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = B2lTouchReadCommand(
        Context,
        SEC_TS_READ_BOOT_STATUS,
        &BootStatus,
        (ULONG)sizeof(BootStatus)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((FirmwareIntegrity & 0x80U) == 0U) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return (BootStatus == SEC_TS_STATUS_APP_MODE) ?
        STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS
B2lTouchStartController(PB2L_TOUCH_CONTEXT Context)
{
    NTSTATUS Status;

    RtlZeroMemory(Context->Contacts, sizeof(Context->Contacts));
    Status = B2lHsI2cInitialize(Context);
    B2lTouchPublishDiagnostic(Context->Device, B2L_TOUCH_STAGE_I2C_INIT, Status);
    if (NT_SUCCESS(Status)) {
        Status = B2lTouchProbe(Context);
        B2lTouchPublishDiagnostic(Context->Device, B2L_TOUCH_STAGE_PROBE, Status);
    }
    if (NT_SUCCESS(Status)) {
        Status = B2lTouchWriteCommand(Context, SEC_TS_CMD_CLEAR_EVENT_STACK);
        B2lTouchPublishDiagnostic(Context->Device, B2L_TOUCH_STAGE_CLEAR_EVENTS, Status);
    }
    if (NT_SUCCESS(Status)) {
        Status = B2lTouchSetFunctions(Context);
        B2lTouchPublishDiagnostic(Context->Device, B2L_TOUCH_STAGE_FUNCTIONS, Status);
    }
    if (NT_SUCCESS(Status)) {
        Status = B2lTouchWriteCommand(Context, SEC_TS_CMD_SENSE_ON);
        B2lTouchPublishDiagnostic(Context->Device, B2L_TOUCH_STAGE_SENSE_ON, Status);
    }

    Context->TouchReady = (BOOLEAN)NT_SUCCESS(Status);
    Context->ConsecutiveErrors = 0UL;
    if (!NT_SUCCESS(Status)) {
        B2lHsI2cQuiesce(Context);
    }
    return Status;
}

static ULONG
B2lTouchActiveContactCount(PB2L_TOUCH_CONTEXT Context)
{
    ULONG Count = 0UL;
    UCHAR TouchId;

    for (TouchId = 1U; TouchId <= B2L_SEC_TS_MAX_TOUCH_ID; ++TouchId) {
        if (Context->Contacts[TouchId].Active) {
            ++Count;
        }
    }
    return Count;
}

static BOOLEAN
B2lTouchApplyUpdate(
    PB2L_TOUCH_CONTEXT Context,
    const B2L_SEC_TS_UPDATE *Update
    )
{
    B2L_TOUCH_CONTACT_STATE *Contact;

    if ((Update == NULL) || (Update->TouchId == 0U) ||
        (Update->TouchId > B2L_SEC_TS_MAX_TOUCH_ID)) {
        return FALSE;
    }

    Contact = &Context->Contacts[Update->TouchId];
    if (Update->Action == B2L_SEC_TS_ACTION_RELEASE) {
        if (!Contact->Active) {
            return FALSE;
        }
        Contact->Active = FALSE;
        Contact->PendingRelease = TRUE;
        return TRUE;
    }

    if (Update->Action == B2L_SEC_TS_ACTION_MOVE) {
        if (!Contact->Active) {
            return FALSE;
        }
    } else if (Update->Action == B2L_SEC_TS_ACTION_PRESS) {
        if (!Contact->Active &&
            (B2lTouchActiveContactCount(Context) >= B2L_TOUCH_MAX_CONTACTS)) {
            return FALSE;
        }
        Contact->Active = TRUE;
        Contact->PendingRelease = FALSE;
    } else {
        return FALSE;
    }

    Contact->X = Update->X;
    Contact->Y = Update->Y;
    return TRUE;
}

static NTSTATUS
B2lTouchSubmitFrame(PB2L_TOUCH_CONTEXT Context)
{
    B2L_TOUCH_CONTACT_REPORT *ReportContact;
    B2L_TOUCH_CONTACT_STATE *Contact;
    NTSTATUS Status;
    UCHAR Count = 0U;
    UCHAR TouchId;

    RtlZeroMemory(&Context->Report, sizeof(Context->Report));
    Context->Report.ReportId = B2L_TOUCH_REPORT_ID;
    for (TouchId = 1U;
         (TouchId <= B2L_SEC_TS_MAX_TOUCH_ID) &&
         (Count < B2L_TOUCH_MAX_CONTACTS);
         ++TouchId) {
        Contact = &Context->Contacts[TouchId];
        if (!Contact->Active && !Contact->PendingRelease) {
            continue;
        }
        ReportContact = &Context->Report.Contacts[Count];
        ReportContact->Tip = Contact->Active ? 1U : 0U;
        ReportContact->ContactId = TouchId;
        ReportContact->X = Contact->X;
        ReportContact->Y = Contact->Y;
        ++Count;
    }
    if (Count == 0U) {
        return STATUS_SUCCESS;
    }

    Context->Report.ContactCount = Count;
    Context->ReportPacket.reportBuffer = (PUCHAR)&Context->Report;
    Context->ReportPacket.reportBufferLen = (ULONG)sizeof(Context->Report);
    Context->ReportPacket.reportId = B2L_TOUCH_REPORT_ID;
    Status = VhfReadReportSubmit(Context->VhfHandle, &Context->ReportPacket);
    if (NT_SUCCESS(Status)) {
        for (TouchId = 1U; TouchId <= B2L_SEC_TS_MAX_TOUCH_ID; ++TouchId) {
            Context->Contacts[TouchId].PendingRelease = FALSE;
        }
    }
    return Status;
}

static NTSTATUS
B2lTouchReleaseAll(PB2L_TOUCH_CONTEXT Context)
{
    BOOLEAN Changed = FALSE;
    UCHAR TouchId;

    for (TouchId = 1U; TouchId <= B2L_SEC_TS_MAX_TOUCH_ID; ++TouchId) {
        if (Context->Contacts[TouchId].Active) {
            Context->Contacts[TouchId].Active = FALSE;
            Context->Contacts[TouchId].PendingRelease = TRUE;
            Changed = TRUE;
        }
    }
    return Changed ? B2lTouchSubmitFrame(Context) : STATUS_SUCCESS;
}

static NTSTATUS
B2lTouchPoll(PB2L_TOUCH_CONTEXT Context)
{
    UCHAR Events[B2L_TOUCH_MAX_EVENTS][B2L_SEC_TS_EVENT_SIZE];
    UCHAR ReadCount;
    UCHAR Remaining;
    B2L_SEC_TS_UPDATE Update;
    ULONG Index;
    NTSTATUS Status;

    Status = B2lTouchReadCommand(
        Context,
        SEC_TS_READ_ONE_EVENT,
        Events[0],
        B2L_SEC_TS_EVENT_SIZE
        );
    if (!NT_SUCCESS(Status) || (Events[0][0] == 0U)) {
        return Status;
    }

    Remaining = B2lSecTsRemainingEvents(Events[0]);
    ReadCount = (Remaining > (B2L_TOUCH_MAX_EVENTS - 1U)) ?
        (B2L_TOUCH_MAX_EVENTS - 1U) : Remaining;
    if (ReadCount != 0U) {
        Status = B2lTouchReadCommand(
            Context,
            SEC_TS_READ_ALL_EVENT,
            &Events[1][0],
            (ULONG)ReadCount * B2L_SEC_TS_EVENT_SIZE
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    for (Index = 0UL; Index <= ReadCount; ++Index) {
        if ((B2lSecTsParseEvent(Events[Index], &Update) == B2lSecTsUpdated) &&
            B2lTouchApplyUpdate(Context, &Update)) {
            Status = B2lTouchSubmitFrame(Context);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }
    }
    return STATUS_SUCCESS;
}

VOID
B2lTouchEvtTimer(WDFTIMER Timer)
{
    WDFDEVICE Device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext(Device);
    NTSTATUS Status;

    if (!Context->HardwareReady || !Context->TouchReady) {
        return;
    }

    Status = B2lTouchPoll(Context);
    if (NT_SUCCESS(Status)) {
        Context->ConsecutiveErrors = 0UL;
    } else {
        ++Context->ConsecutiveErrors;
        if (Context->ConsecutiveErrors >= 3UL) {
            (VOID)B2lTouchReleaseAll(Context);
            Context->TouchReady = FALSE;
            B2lHsI2cQuiesce(Context);
            (VOID)B2lTouchStartController(Context);
        }
    }

    if (Context->HardwareReady && Context->TouchReady) {
        WdfTimerStart(
            Context->PollTimer,
            WDF_REL_TIMEOUT_IN_MS(B2L_TOUCH_POLL_MS)
            );
    }
}

static VOID
B2lTouchUnmapResources(PB2L_TOUCH_CONTEXT Context)
{
    if (Context->HsI2cRegisters != NULL) {
        MmUnmapIoSpace(Context->HsI2cRegisters, B2L_DEVICE_PAGE_LENGTH);
        Context->HsI2cRegisters = NULL;
    }
    if (Context->UsiRegisters != NULL) {
        MmUnmapIoSpace(Context->UsiRegisters, B2L_DEVICE_PAGE_LENGTH);
        Context->UsiRegisters = NULL;
    }
    Context->HsI2cLength = 0UL;
    Context->UsiLength = 0UL;
    Context->HardwareReady = FALSE;
}

NTSTATUS
B2lTouchEvtPrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext(Device);
    ULONG Count;
    ULONG Index;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    ULONGLONG Start;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    B2lTouchUnmapResources(Context);
    Count = WdfCmResourceListGetCount(ResourcesTranslated);
    for (Index = 0UL; Index < Count; ++Index) {
        Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        if ((Resource == NULL) || (Resource->Type != CmResourceTypeMemory)) {
            continue;
        }

        Start = (ULONGLONG)Resource->u.Memory.Start.QuadPart;
        if ((Resource->u.Memory.Length != B2L_DEVICE_PAGE_LENGTH) ||
            ((Start != B2L_HSI2C_PHYSICAL_BASE) &&
             (Start != B2L_USI_PHYSICAL_BASE))) {
            B2lTouchUnmapResources(Context);
            B2lTouchPublishDiagnostic(
                Device,
                B2L_TOUCH_STAGE_RESOURCES,
                STATUS_DEVICE_CONFIGURATION_ERROR
                );
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        if (Start == B2L_HSI2C_PHYSICAL_BASE) {
            if (Context->HsI2cRegisters != NULL) {
                B2lTouchUnmapResources(Context);
                B2lTouchPublishDiagnostic(
                    Device,
                    B2L_TOUCH_STAGE_RESOURCES,
                    STATUS_DEVICE_CONFIGURATION_ERROR
                    );
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Context->HsI2cRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                B2L_DEVICE_PAGE_LENGTH,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->HsI2cLength = B2L_DEVICE_PAGE_LENGTH;
        } else {
            if (Context->UsiRegisters != NULL) {
                B2lTouchUnmapResources(Context);
                B2lTouchPublishDiagnostic(
                    Device,
                    B2L_TOUCH_STAGE_RESOURCES,
                    STATUS_DEVICE_CONFIGURATION_ERROR
                    );
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Context->UsiRegisters = (PUCHAR)MmMapIoSpaceEx(
                Resource->u.Memory.Start,
                B2L_DEVICE_PAGE_LENGTH,
                PAGE_READWRITE | PAGE_NOCACHE
                );
            Context->UsiLength = B2L_DEVICE_PAGE_LENGTH;
        }

        if ((Context->HsI2cRegisters == NULL) &&
            (Context->UsiRegisters == NULL)) {
            B2lTouchUnmapResources(Context);
            B2lTouchPublishDiagnostic(
                Device,
                B2L_TOUCH_STAGE_RESOURCES,
                STATUS_INSUFFICIENT_RESOURCES
                );
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if ((Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL)) {
        B2lTouchUnmapResources(Context);
        B2lTouchPublishDiagnostic(
            Device,
            B2L_TOUCH_STAGE_RESOURCES,
            STATUS_DEVICE_CONFIGURATION_ERROR
            );
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Context->HardwareReady = TRUE;
    B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_RESOURCES, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lTouchEvtReleaseHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    Context->TouchReady = FALSE;
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    B2lHsI2cQuiesce(Context);
    B2lTouchUnmapResources(Context);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lTouchEvtD0Entry(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext(Device);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);
    Status = B2lTouchStartController(Context);
    if (NT_SUCCESS(Status)) {
        WdfTimerStart(Context->PollTimer, WDF_REL_TIMEOUT_IN_MS(B2L_TOUCH_POLL_MS));
        B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_D0_READY, Status);
    }
    return Status;
}

NTSTATUS
B2lTouchEvtD0Exit(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE TargetState
    )
{
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext(Device);

    UNREFERENCED_PARAMETER(TargetState);
    Context->TouchReady = FALSE;
    (VOID)WdfTimerStop(Context->PollTimer, TRUE);
    (VOID)B2lTouchReleaseAll(Context);
    B2lHsI2cQuiesce(Context);
    return STATUS_SUCCESS;
}

VOID
B2lTouchEvtGetFeature(
    PVOID VhfClientContext,
    VHFOPERATIONHANDLE VhfOperationHandle,
    PVOID VhfOperationContext,
    PHID_XFER_PACKET HidTransferPacket
    )
{
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(VhfClientContext);
    UNREFERENCED_PARAMETER(VhfOperationContext);
    if ((HidTransferPacket != NULL) &&
        (HidTransferPacket->reportId == B2L_TOUCH_FEATURE_REPORT_ID) &&
        (HidTransferPacket->reportBuffer != NULL) &&
        (HidTransferPacket->reportBufferLen >= 2UL)) {
        HidTransferPacket->reportBuffer[0] = B2L_TOUCH_FEATURE_REPORT_ID;
        HidTransferPacket->reportBuffer[1] = B2L_TOUCH_MAX_CONTACTS;
        Status = STATUS_SUCCESS;
    }
    (VOID)VhfAsyncOperationComplete(VhfOperationHandle, Status);
}

VOID
B2lTouchEvtCleanup(WDFOBJECT Object)
{
    PB2L_TOUCH_CONTEXT Context = B2lTouchGetContext((WDFDEVICE)Object);

    if (Context->VhfHandle != NULL) {
        VhfDelete(Context->VhfHandle, TRUE);
        Context->VhfHandle = NULL;
    }
}

NTSTATUS
B2lTouchEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;
    WDF_TIMER_CONFIG TimerConfig;
    WDFDEVICE Device;
    PB2L_TOUCH_CONTEXT Context;
    VHF_CONFIG VhfConfig;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    B2lTouchPublishServiceDiagnostic(
        B2L_TOUCH_STAGE_ADD_ENTERED,
        STATUS_SUCCESS
        );
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpCallbacks);
    PnpCallbacks.EvtDevicePrepareHardware = B2lTouchEvtPrepareHardware;
    PnpCallbacks.EvtDeviceReleaseHardware = B2lTouchEvtReleaseHardware;
    PnpCallbacks.EvtDeviceD0Entry = B2lTouchEvtD0Entry;
    PnpCallbacks.EvtDeviceD0Exit = B2lTouchEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, B2L_TOUCH_CONTEXT);
    Attributes.EvtCleanupCallback = B2lTouchEvtCleanup;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    B2lTouchPublishServiceDiagnostic(
        B2L_TOUCH_STAGE_DEVICE_CREATE,
        Status
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Context = B2lTouchGetContext(Device);
    Context->Device = Device;
    B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_DEVICE_CREATED, STATUS_SUCCESS);
    /* Passive-level KMDF timers must be one-shot; the callback rearms it. */
    WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lTouchEvtTimer);
    TimerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfTimerCreate(&TimerConfig, &Attributes, &Context->PollTimer);
    B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_TIMER_CREATED, Status);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    VHF_CONFIG_INIT(
        &VhfConfig,
        WdfDeviceWdmGetDeviceObject(Device),
        (USHORT)sizeof(B2lTouchReportDescriptor),
        B2lTouchReportDescriptor
        );
    VhfConfig.VhfClientContext = Context;
    VhfConfig.VendorID = 0x04E8U;
    VhfConfig.ProductID = 0x9820U;
    VhfConfig.VersionNumber = 0x0001U;
    VhfConfig.EvtVhfAsyncOperationGetFeature = B2lTouchEvtGetFeature;
    Status = VhfCreate(&VhfConfig, &Context->VhfHandle);
    B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_VHF_CREATED, Status);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = VhfStart(Context->VhfHandle);
    B2lTouchPublishDiagnostic(Device, B2L_TOUCH_STAGE_VHF_STARTED, Status);
    return Status;
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, B2lTouchEvtDeviceAdd);
    Status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &Config,
        WDF_NO_HANDLE
        );
    B2lTouchPublishDriverStatus(RegistryPath, Status);
    return Status;
}
