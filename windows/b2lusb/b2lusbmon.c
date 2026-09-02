#include <ntddk.h>
#include <wdf.h>

// Passive lower filter for the URS host child.  It never touches controller
// registers and never fails PnP; it only persists the resources and PnP stages
// that actually reached the USBXHCI stack.  This remains useful after a failed
// start, when pnputil may no longer expose the released resource assignment.

#define B2L_USB_MON_DMA_BYTES 0x00010000UL

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lUsbMonEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lUsbMonEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lUsbMonEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lUsbMonEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT B2lUsbMonEvtD0Exit;

static
VOID
B2lUsbMonWriteUlong(
    WDFKEY Key,
    PCWSTR Name,
    ULONG Value
    )
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    (void)WdfRegistryAssignULong(Key, &ValueName, Value);
}

static
NTSTATUS
B2lUsbMonOpenDeviceKey(
    WDFDEVICE Device,
    WDFKEY *Key
    )
{
    return WdfDeviceOpenRegistryKey(
        Device,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_SET_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        Key
        );
}

static
VOID
B2lUsbMonRecordFlag(
    WDFDEVICE Device,
    PCWSTR Name,
    ULONG Value
    )
{
    WDFKEY Key;

    if (!NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        return;
    }
    B2lUsbMonWriteUlong(Key, Name, Value);
    WdfRegistryClose(Key);
}

static
VOID
B2lUsbMonProbeDmaProfile(
    WDFDEVICE Device,
    WDF_DMA_PROFILE Profile,
    PCWSTR EnablerStatusName,
    PCWSTR BufferStatusName,
    PCWSTR AddressLowName,
    PCWSTR AddressHighName,
    PCWSTR LengthName
    )
{
    WDFCOMMONBUFFER CommonBuffer;
    WDF_DMA_ENABLER_CONFIG DmaConfig;
    WDFDMAENABLER DmaEnabler;
    PHYSICAL_ADDRESS LogicalAddress;
    NTSTATUS Status;

    // The buffer is never handed to hardware and is deleted immediately.  The
    // probe only asks Windows whether this DMA address-width profile can obtain
    // a common buffer for the host PDO.
    B2lUsbMonRecordFlag(Device, BufferStatusName, (ULONG)STATUS_NOT_SUPPORTED);
    B2lUsbMonRecordFlag(Device, AddressLowName, MAXULONG);
    B2lUsbMonRecordFlag(Device, AddressHighName, MAXULONG);
    B2lUsbMonRecordFlag(Device, LengthName, 0UL);
    WDF_DMA_ENABLER_CONFIG_INIT(
        &DmaConfig,
        Profile,
        B2L_USB_MON_DMA_BYTES
        );
    Status = WdfDmaEnablerCreate(
        Device,
        &DmaConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &DmaEnabler
        );
    B2lUsbMonRecordFlag(
        Device,
        EnablerStatusName,
        (ULONG)Status
        );
    if (!NT_SUCCESS(Status)) {
        return;
    }

    Status = WdfCommonBufferCreate(
        DmaEnabler,
        B2L_USB_MON_DMA_BYTES,
        WDF_NO_OBJECT_ATTRIBUTES,
        &CommonBuffer
        );
    B2lUsbMonRecordFlag(
        Device,
        BufferStatusName,
        (ULONG)Status
        );
    if (NT_SUCCESS(Status)) {
        LogicalAddress = WdfCommonBufferGetAlignedLogicalAddress(CommonBuffer);
        B2lUsbMonRecordFlag(
            Device,
            AddressLowName,
            LogicalAddress.LowPart
            );
        B2lUsbMonRecordFlag(
            Device,
            AddressHighName,
            (ULONG)LogicalAddress.HighPart
            );
        B2lUsbMonRecordFlag(
            Device,
            LengthName,
            (ULONG)WdfCommonBufferGetLength(CommonBuffer)
            );
        WdfObjectDelete(CommonBuffer);
    }
    WdfObjectDelete(DmaEnabler);
}

static
VOID
B2lUsbMonProbeDma(
    WDFDEVICE Device
    )
{
    // AC64 in HCCPARAMS1 says the controller accepts 64-bit bus addresses.
    B2lUsbMonProbeDmaProfile(
        Device,
        WdfDmaProfileScatterGather64Duplex,
        L"B2LMonDma64EnablerStatus",
        L"B2LMonDma64BufferStatus",
        L"B2LMonDma64AddressLo",
        L"B2LMonDma64AddressHi",
        L"B2LMonDma64Length"
        );

    // The 32-bit profile is a read-only A/B for the guarded memory map.  A
    // successful address below 4 GiB proves that Windows can still provide a
    // bounded low DMA window without exposing any protected DRAM bank.
    B2lUsbMonProbeDmaProfile(
        Device,
        WdfDmaProfileScatterGatherDuplex,
        L"B2LMonDma32EnablerStatus",
        L"B2LMonDma32BufferStatus",
        L"B2LMonDma32AddressLo",
        L"B2LMonDma32AddressHi",
        L"B2LMonDma32Length"
        );
    B2lUsbMonRecordFlag(Device, L"B2LMonDmaProbeSeen", 1UL);
}

NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, B2lUsbMonEvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &Config,
        WDF_NO_HANDLE
        );
}

NTSTATUS
B2lUsbMonEvtDeviceAdd(
    WDFDRIVER Driver,
    PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpCallbacks);
    PnpCallbacks.EvtDevicePrepareHardware = B2lUsbMonEvtPrepareHardware;
    PnpCallbacks.EvtDeviceReleaseHardware = B2lUsbMonEvtReleaseHardware;
    PnpCallbacks.EvtDeviceD0Entry = B2lUsbMonEvtD0Entry;
    PnpCallbacks.EvtDeviceD0Exit = B2lUsbMonEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpCallbacks);

    Status = WdfDeviceCreate(
        &DeviceInit,
        WDF_NO_OBJECT_ATTRIBUTES,
        &Device
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    B2lUsbMonRecordFlag(Device, L"B2LMonAddSeen", 1UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonPrepareSeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonD0EntrySeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonReleaseSeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonDmaProbeSeen", 0UL);
    B2lUsbMonProbeDma(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtPrepareHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    WDFKEY Key;
    ULONG Count;
    ULONG Index;
    ULONG InterruptCount;
    ULONG MemoryCount;

    if (!NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        return STATUS_SUCCESS;
    }

    B2lUsbMonWriteUlong(
        Key,
        L"B2LMonRawCount",
        WdfCmResourceListGetCount(ResourcesRaw)
        );
    Count = WdfCmResourceListGetCount(ResourcesTranslated);
    B2lUsbMonWriteUlong(Key, L"B2LMonTranslatedCount", Count);
    MemoryCount = 0;
    InterruptCount = 0;

    for (Index = 0; Index < Count; ++Index) {
        Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        if (Resource == NULL) {
            continue;
        }

        if (Resource->Type == CmResourceTypeMemory) {
            ++MemoryCount;
            if (MemoryCount == 1) {
                B2lUsbMonWriteUlong(
                    Key,
                    L"B2LMonMemoryBaseLo",
                    Resource->u.Memory.Start.LowPart
                    );
                B2lUsbMonWriteUlong(
                    Key,
                    L"B2LMonMemoryBaseHi",
                    (ULONG)Resource->u.Memory.Start.HighPart
                    );
                B2lUsbMonWriteUlong(
                    Key,
                    L"B2LMonMemoryLength",
                    Resource->u.Memory.Length
                    );
            }
        } else if (Resource->Type == CmResourceTypeInterrupt) {
            ++InterruptCount;
            if (InterruptCount == 1) {
                B2lUsbMonWriteUlong(
                    Key,
                    L"B2LMonInterruptFlags",
                    Resource->Flags
                    );
                if ((Resource->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0) {
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonInterruptVector",
                        Resource->u.MessageInterrupt.Translated.Vector
                        );
                } else {
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonInterruptVector",
                        Resource->u.Interrupt.Vector
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonInterruptLevel",
                        Resource->u.Interrupt.Level
                        );
                }
            }
        }
    }

    B2lUsbMonWriteUlong(Key, L"B2LMonMemoryCount", MemoryCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonInterruptCount", InterruptCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonPrepareSeen", 1UL);
    WdfRegistryClose(Key);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtReleaseHardware(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesTranslated
    )
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    B2lUsbMonRecordFlag(Device, L"B2LMonReleaseSeen", 1UL);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtD0Entry(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE PreviousState
    )
{
    UNREFERENCED_PARAMETER(PreviousState);

    B2lUsbMonRecordFlag(Device, L"B2LMonD0EntrySeen", 1UL);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtD0Exit(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE TargetState
    )
{
    B2lUsbMonRecordFlag(Device, L"B2LMonD0ExitTarget", (ULONG)TargetState);
    return STATUS_SUCCESS;
}
