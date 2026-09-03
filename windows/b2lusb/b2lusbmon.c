#include <ntddk.h>
#include <wdf.h>

// Passive upper filter for the URS host child.  It never touches controller
// registers, changes resource lists or fails PnP; it only persists the resource
// requirements, assigned resources and PnP stages that actually reached the
// USBXHCI stack.  This remains useful after a failed start, when pnputil may no
// longer expose the released resource assignment.

#define B2L_USB_MON_DMA_BYTES 0x00010000UL

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lUsbMonEvtDeviceAdd;
EVT_WDF_DEVICE_FILTER_RESOURCE_REQUIREMENTS B2lUsbMonEvtFilterAddResourceRequirements;
EVT_WDF_DEVICE_FILTER_RESOURCE_REQUIREMENTS B2lUsbMonEvtFilterRemoveResourceRequirements;
EVT_WDF_DEVICE_REMOVE_ADDED_RESOURCES B2lUsbMonEvtRemoveAddedResources;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lUsbMonEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lUsbMonEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lUsbMonEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT B2lUsbMonEvtD0Exit;
EVT_WDFDEVICE_WDM_IRP_PREPROCESS B2lUsbMonEvtWdmPreprocessPnp;
EVT_WDF_WORKITEM B2lUsbMonEvtStartTraceWorkItem;
IO_COMPLETION_ROUTINE B2lUsbMonStartCompletion;

typedef struct _B2L_USB_MON_CONTEXT {
    WDFWORKITEM StartTraceWorkItem;
    volatile LONG StartIrpSeen;
    volatile LONG StartCompletionSeen;
    volatile LONG StartCompletionStatus;
} B2L_USB_MON_CONTEXT, *PB2L_USB_MON_CONTEXT;

typedef struct _B2L_USB_MON_RESOURCE_SUMMARY {
    ULONG DescriptorCount;
    ULONG MemoryCount;
    ULONG MemoryBaseLo;
    ULONG MemoryBaseHi;
    ULONG MemoryLength;
    ULONG InterruptCount;
    ULONG InterruptFlags;
    ULONG InterruptLevel;
    ULONG InterruptVector;
    ULONG InterruptAffinityLo;
    ULONG InterruptAffinityHi;
} B2L_USB_MON_RESOURCE_SUMMARY, *PB2L_USB_MON_RESOURCE_SUMMARY;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(B2L_USB_MON_CONTEXT, B2lUsbMonGetContext)

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
ULONG
B2lUsbMonCountRequirementDescriptors(
    WDFIORESREQLIST RequirementsList
    )
{
    WDFIORESLIST ResourceList;
    ULONG AlternativeCount;
    ULONG AlternativeIndex;
    ULONG DescriptorCount;

    AlternativeCount = WdfIoResourceRequirementsListGetCount(RequirementsList);
    DescriptorCount = 0;
    for (AlternativeIndex = 0;
         AlternativeIndex < AlternativeCount;
         ++AlternativeIndex) {
        ResourceList = WdfIoResourceRequirementsListGetIoResList(
            RequirementsList,
            AlternativeIndex
            );
        if (ResourceList != NULL) {
            DescriptorCount += WdfIoResourceListGetCount(ResourceList);
        }
    }
    return DescriptorCount;
}

static
VOID
B2lUsbMonSummarizeAssignedResources(
    WDFCMRESLIST Resources,
    BOOLEAN Translated,
    PB2L_USB_MON_RESOURCE_SUMMARY Summary
    )
{
    ULONGLONG Affinity;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    ULONG Index;

    RtlZeroMemory(Summary, sizeof(*Summary));
    Summary->DescriptorCount = WdfCmResourceListGetCount(Resources);
    for (Index = 0; Index < Summary->DescriptorCount; ++Index) {
        Resource = WdfCmResourceListGetDescriptor(Resources, Index);
        if (Resource == NULL) {
            continue;
        }

        if (Resource->Type == CmResourceTypeMemory) {
            ++Summary->MemoryCount;
            if (Summary->MemoryCount == 1) {
                Summary->MemoryBaseLo = Resource->u.Memory.Start.LowPart;
                Summary->MemoryBaseHi =
                    (ULONG)Resource->u.Memory.Start.HighPart;
                Summary->MemoryLength = Resource->u.Memory.Length;
            }
        } else if (Resource->Type == CmResourceTypeInterrupt) {
            ++Summary->InterruptCount;
            if (Summary->InterruptCount != 1) {
                continue;
            }

            Summary->InterruptFlags = Resource->Flags;
            if ((Resource->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0) {
                if (Translated) {
                    Summary->InterruptLevel =
                        Resource->u.MessageInterrupt.Translated.Level;
                    Summary->InterruptVector =
                        Resource->u.MessageInterrupt.Translated.Vector;
                    Affinity = (ULONGLONG)(ULONG_PTR)
                        Resource->u.MessageInterrupt.Translated.Affinity;
                } else {
                    Summary->InterruptLevel = 0;
                    Summary->InterruptVector =
                        Resource->u.MessageInterrupt.Raw.Vector;
                    Affinity = (ULONGLONG)(ULONG_PTR)
                        Resource->u.MessageInterrupt.Raw.Affinity;
                }
            } else {
                Summary->InterruptLevel = Resource->u.Interrupt.Level;
                Summary->InterruptVector = Resource->u.Interrupt.Vector;
                Affinity = (ULONGLONG)(ULONG_PTR)
                    Resource->u.Interrupt.Affinity;
            }
            Summary->InterruptAffinityLo = (ULONG)Affinity;
            Summary->InterruptAffinityHi = (ULONG)(Affinity >> 32);
        }
    }
}

static
VOID
B2lUsbMonResetRequirementTrace(
    WDFDEVICE Device
    )
{
    WDFKEY Key;

    if (!NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        return;
    }

    B2lUsbMonWriteUlong(Key, L"B2LMonReqRemoveSeen", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqAddSeen", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonRemoveAddedSeen", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqAlternativeCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqDescriptorCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryMinLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryMinHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryMaxLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryMaxHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryLengthLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryLengthHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqInterruptCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqInterruptMin", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqInterruptMax", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqInterruptFlags", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqAddAlternativeCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqAddDescriptorCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryBaseLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryBaseHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryLength", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptFlags", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptLevel", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptVector", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptAffinityLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptAffinityHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryBaseLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryBaseHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryLength", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptCount", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptFlags", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptLevel", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptVector", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptAffinityLo", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptAffinityHi", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonStartIrpSeen", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonStartCompletionSeen", 0UL);
    B2lUsbMonWriteUlong(Key, L"B2LMonStartCompletionStatus", 0UL);
    WdfRegistryClose(Key);
}

VOID
B2lUsbMonEvtStartTraceWorkItem(
    WDFWORKITEM WorkItem
    )
{
    PB2L_USB_MON_CONTEXT Context;
    WDFDEVICE Device;
    WDFKEY Key;

    Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    Context = B2lUsbMonGetContext(Device);
    if (!NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        return;
    }

    B2lUsbMonWriteUlong(
        Key,
        L"B2LMonStartIrpSeen",
        (ULONG)InterlockedCompareExchange(&Context->StartIrpSeen, 0, 0)
        );
    B2lUsbMonWriteUlong(
        Key,
        L"B2LMonStartCompletionSeen",
        (ULONG)InterlockedCompareExchange(&Context->StartCompletionSeen, 0, 0)
        );
    B2lUsbMonWriteUlong(
        Key,
        L"B2LMonStartCompletionStatus",
        (ULONG)InterlockedCompareExchange(&Context->StartCompletionStatus, 0, 0)
        );
    WdfRegistryClose(Key);
}

NTSTATUS
B2lUsbMonStartCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID CompletionContext
    )
{
    PB2L_USB_MON_CONTEXT Context;
    WDFDEVICE Device;

    UNREFERENCED_PARAMETER(DeviceObject);

    Device = (WDFDEVICE)CompletionContext;
    Context = B2lUsbMonGetContext(Device);
    InterlockedExchange(&Context->StartCompletionStatus, Irp->IoStatus.Status);
    InterlockedExchange(&Context->StartCompletionSeen, 1);
    WdfWorkItemEnqueue(Context->StartTraceWorkItem);
    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }
    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
B2lUsbMonEvtWdmPreprocessPnp(
    WDFDEVICE Device,
    PIRP Irp
    )
{
    PB2L_USB_MON_CONTEXT Context;

    Context = B2lUsbMonGetContext(Device);
    InterlockedExchange(&Context->StartIrpSeen, 1);
    WdfWorkItemEnqueue(Context->StartTraceWorkItem);

    // The extra stack location supplied by KMDF lets the upper filter observe
    // the final status without changing it or delaying START_DEVICE.
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(
        Irp,
        B2lUsbMonStartCompletion,
        Device,
        TRUE,
        TRUE,
        TRUE
        );
    return WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
}

static
VOID
B2lUsbMonRecordRequirements(
    WDFDEVICE Device,
    WDFIORESREQLIST RequirementsList
    )
{
    PIO_RESOURCE_DESCRIPTOR Resource;
    WDFIORESLIST ResourceList;
    WDFKEY Key;
    ULONGLONG Length;
    ULONGLONG MaximumAddress;
    ULONGLONG MinimumAddress;
    ULONG AlternativeCount;
    ULONG AlternativeIndex;
    ULONG DescriptorCount;
    ULONG DescriptorIndex;
    ULONG InterruptCount;
    ULONG MemoryCount;

    if (!NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        return;
    }

    AlternativeCount = WdfIoResourceRequirementsListGetCount(RequirementsList);
    DescriptorCount = 0;
    MemoryCount = 0;
    InterruptCount = 0;
    for (AlternativeIndex = 0;
         AlternativeIndex < AlternativeCount;
         ++AlternativeIndex) {
        ResourceList = WdfIoResourceRequirementsListGetIoResList(
            RequirementsList,
            AlternativeIndex
            );
        if (ResourceList == NULL) {
            continue;
        }

        for (DescriptorIndex = 0;
             DescriptorIndex < WdfIoResourceListGetCount(ResourceList);
             ++DescriptorIndex) {
            Resource = WdfIoResourceListGetDescriptor(
                ResourceList,
                DescriptorIndex
                );
            if (Resource == NULL) {
                continue;
            }
            ++DescriptorCount;

            if (Resource->Type == CmResourceTypeMemory ||
                Resource->Type == CmResourceTypeMemoryLarge) {
                ++MemoryCount;
                if (MemoryCount == 1) {
                    Length = RtlIoDecodeMemIoResource(
                        Resource,
                        NULL,
                        &MinimumAddress,
                        &MaximumAddress
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryMinLo",
                        (ULONG)MinimumAddress
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryMinHi",
                        (ULONG)(MinimumAddress >> 32)
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryMaxLo",
                        (ULONG)MaximumAddress
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryMaxHi",
                        (ULONG)(MaximumAddress >> 32)
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryLengthLo",
                        (ULONG)Length
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqMemoryLengthHi",
                        (ULONG)(Length >> 32)
                        );
                }
            } else if (Resource->Type == CmResourceTypeInterrupt) {
                ++InterruptCount;
                if (InterruptCount == 1) {
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqInterruptMin",
                        Resource->u.Interrupt.MinimumVector
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqInterruptMax",
                        Resource->u.Interrupt.MaximumVector
                        );
                    B2lUsbMonWriteUlong(
                        Key,
                        L"B2LMonReqInterruptFlags",
                        Resource->Flags
                        );
                }
            }
        }
    }

    B2lUsbMonWriteUlong(Key, L"B2LMonReqAlternativeCount", AlternativeCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqDescriptorCount", DescriptorCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqMemoryCount", MemoryCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqInterruptCount", InterruptCount);
    B2lUsbMonWriteUlong(Key, L"B2LMonReqRemoveSeen", 1UL);
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
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_FDO_EVENT_CALLBACKS FdoCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;
    WDF_WORKITEM_CONFIG WorkItemConfig;
    PB2L_USB_MON_CONTEXT Context;
    WDFDEVICE Device;
    UCHAR MinorFunctions[] = { IRP_MN_START_DEVICE };
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);
    WDF_FDO_EVENT_CALLBACKS_INIT(&FdoCallbacks);
    FdoCallbacks.EvtDeviceFilterAddResourceRequirements =
        B2lUsbMonEvtFilterAddResourceRequirements;
    FdoCallbacks.EvtDeviceFilterRemoveResourceRequirements =
        B2lUsbMonEvtFilterRemoveResourceRequirements;
    FdoCallbacks.EvtDeviceRemoveAddedResources =
        B2lUsbMonEvtRemoveAddedResources;
    WdfFdoInitSetEventCallbacks(DeviceInit, &FdoCallbacks);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpCallbacks);
    PnpCallbacks.EvtDevicePrepareHardware = B2lUsbMonEvtPrepareHardware;
    PnpCallbacks.EvtDeviceReleaseHardware = B2lUsbMonEvtReleaseHardware;
    PnpCallbacks.EvtDeviceD0Entry = B2lUsbMonEvtD0Entry;
    PnpCallbacks.EvtDeviceD0Exit = B2lUsbMonEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpCallbacks);

    Status = WdfDeviceInitAssignWdmIrpPreprocessCallback(
        DeviceInit,
        B2lUsbMonEvtWdmPreprocessPnp,
        IRP_MJ_PNP,
        MinorFunctions,
        ARRAYSIZE(MinorFunctions)
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &Attributes,
        B2L_USB_MON_CONTEXT
        );
    Status = WdfDeviceCreate(
        &DeviceInit,
        &Attributes,
        &Device
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Context = B2lUsbMonGetContext(Device);
    Context->StartIrpSeen = 0;
    Context->StartCompletionSeen = 0;
    Context->StartCompletionStatus = STATUS_PENDING;
    WDF_WORKITEM_CONFIG_INIT(
        &WorkItemConfig,
        B2lUsbMonEvtStartTraceWorkItem
        );
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Status = WdfWorkItemCreate(
        &WorkItemConfig,
        &Attributes,
        &Context->StartTraceWorkItem
        );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    B2lUsbMonRecordFlag(Device, L"B2LMonAddSeen", 1UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonPrepareSeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonD0EntrySeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonReleaseSeen", 0UL);
    B2lUsbMonRecordFlag(Device, L"B2LMonDmaProbeSeen", 0UL);
    B2lUsbMonResetRequirementTrace(Device);
    B2lUsbMonProbeDma(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtFilterRemoveResourceRequirements(
    WDFDEVICE Device,
    WDFIORESREQLIST RequirementsList
    )
{
    // Observe the requirements on the downward pass.  The monitor never
    // removes, updates or appends a descriptor.
    B2lUsbMonRecordRequirements(Device, RequirementsList);
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtFilterAddResourceRequirements(
    WDFDEVICE Device,
    WDFIORESREQLIST RequirementsList
    )
{
    WDFKEY Key;

    // Observe the returning pass only.  Registering this callback requires a
    // matching EvtDeviceRemoveAddedResources callback even though we add no
    // descriptors.
    if (NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        B2lUsbMonWriteUlong(Key, L"B2LMonReqAddSeen", 1UL);
        B2lUsbMonWriteUlong(
            Key,
            L"B2LMonReqAddAlternativeCount",
            WdfIoResourceRequirementsListGetCount(RequirementsList)
            );
        B2lUsbMonWriteUlong(
            Key,
            L"B2LMonReqAddDescriptorCount",
            B2lUsbMonCountRequirementDescriptors(RequirementsList)
            );
        WdfRegistryClose(Key);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
B2lUsbMonEvtRemoveAddedResources(
    WDFDEVICE Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
    )
{
    B2L_USB_MON_RESOURCE_SUMMARY Raw;
    B2L_USB_MON_RESOURCE_SUMMARY Translated;
    WDFKEY Key;

    // No descriptors were added, so there is deliberately nothing to remove.
    // Reaching this callback proves that PnP produced an assigned resource list.
    // Preserve the exact read-only MMIO/IRQ summary before USBXHCI can fail and
    // PnP releases the assignment.
    B2lUsbMonSummarizeAssignedResources(ResourcesRaw, FALSE, &Raw);
    B2lUsbMonSummarizeAssignedResources(
        ResourcesTranslated,
        TRUE,
        &Translated
        );
    if (NT_SUCCESS(B2lUsbMonOpenDeviceKey(Device, &Key))) {
        B2lUsbMonWriteUlong(Key, L"B2LMonRemoveAddedSeen", 1UL);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawCount", Raw.DescriptorCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryCount", Raw.MemoryCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryBaseLo", Raw.MemoryBaseLo);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryBaseHi", Raw.MemoryBaseHi);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawMemoryLength", Raw.MemoryLength);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptCount", Raw.InterruptCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptFlags", Raw.InterruptFlags);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptLevel", Raw.InterruptLevel);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptVector", Raw.InterruptVector);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptAffinityLo", Raw.InterruptAffinityLo);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedRawInterruptAffinityHi", Raw.InterruptAffinityHi);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedCount", Translated.DescriptorCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryCount", Translated.MemoryCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryBaseLo", Translated.MemoryBaseLo);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryBaseHi", Translated.MemoryBaseHi);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedMemoryLength", Translated.MemoryLength);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptCount", Translated.InterruptCount);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptFlags", Translated.InterruptFlags);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptLevel", Translated.InterruptLevel);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptVector", Translated.InterruptVector);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptAffinityLo", Translated.InterruptAffinityLo);
        B2lUsbMonWriteUlong(Key, L"B2LMonAssignedTranslatedInterruptAffinityHi", Translated.InterruptAffinityHi);
        WdfRegistryClose(Key);
    }
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
