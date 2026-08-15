/*
 * B2LUFS — Exynos 9820 physical UFS StorPort miniport.
 *
 * Firmware'in zaten başlattığı HCI linkini resetlemeden kendi tek UTRD/UCD
 * slotuna bağlar ve yalnız kullanıcı LUN 0 için normal SCSI okuma/yazma
 * SRB'lerini UFS'ye iletir. Boot ve RPMB LUN'ları numaralandırılmaz.
 *
 * Bu fiziksel bir ACPI HBA'sıdır. AdapterInterfaceType=Internal, StorPort'un
 * sanal miniport yolu için kullandığı işarettir; bu nedenle arayüz türünü
 * StorPort'un PnP aygıtından seçmesini özellikle istiyoruz.
 */
#include <ntddk.h>
#include <ntddstor.h>
#include <storport.h>

#include "b2lufs_transport.h"

/*
 * PnP, HwFindAdapter'dan yalnız bir SP_RETURN_* değeri alır. Bu değer
 * Aygıt Yöneticisi'nde genelde tek başına CM_PROB_FAILED_ADD/NO_SUCH_DEVICE
 * olarak görünür ve hangi ACPI kaynağında durduğunu söylemez. Her red yolu
 * bu küçük ikili kaydı StorPort Operational ETW kanalına bırakır. Kayıt
 * yalnız WIM'deki startnet.cmd ile okunur; UFS'ye veya HCI'ye yazı yapmaz.
 */
#define B2LUFS_ETW_FIND_ADAPTER_FAILURE  0x42324C01UL
#define B2LUFS_ETW_DEFERRED_BUS_SCAN      0x42324C04UL
#define B2LUFS_BUS_SCAN_DELAY_US          500000UL
#define B2LUFS_ADAPTER_SIGNATURE          0x41465542UL /* "BUFA" */

/*
 * WinPE reads this diagnostic-only subkey after PnP settles.  All contents
 * are obtained with standard read-only SCSI discovery commands and reside
 * in WinPE's volatile registry hive, never on the UFS media.
 */
static UCHAR B2LUFS_REG_SUBKEY[] = "B2LUFS";
static UCHAR B2LUFS_REG_SCHEMA_VERSION[] = "SchemaVersion";
static UCHAR B2LUFS_REG_HCI_CAP[] = "HciCapabilities";
static UCHAR B2LUFS_REG_HCI_STATUS[] = "HciStatus";
static UCHAR B2LUFS_REG_AHIT_BEFORE[] = "AutoHibern8Before";
static UCHAR B2LUFS_REG_TRANSFER_SLOTS[] = "TransferSlotCount";
static UCHAR B2LUFS_REG_TASK_SLOTS[] = "TaskSlotCount";
static UCHAR B2LUFS_REG_SRB_TYPE[] = "SrbType";
static UCHAR B2LUFS_REG_ADDRESS_TYPE[] = "AddressType";
static UCHAR B2LUFS_REG_BUS_COUNT[] = "NumberOfBuses";
static UCHAR B2LUFS_REG_MAX_TARGETS[] = "MaximumTargets";
static UCHAR B2LUFS_REG_INITIATOR_ID[] = "InitiatorBusId";
static UCHAR B2LUFS_REG_SCATTER_GATHER[] = "ScatterGather";
static UCHAR B2LUFS_REG_MASTER[] = "Master";
static UCHAR B2LUFS_REG_TAGGED_QUEUEING[] = "TaggedQueuing";
static UCHAR B2LUFS_REG_AUTO_SENSE[] = "AutoRequestSense";
static UCHAR B2LUFS_REG_MULTIPLE_REQUESTS[] = "MultipleRequestPerLu";
static UCHAR B2LUFS_REG_PHYSICAL_BREAKS[] = "NumberOfPhysicalBreaks";
static UCHAR B2LUFS_REG_SYNCHRONIZATION_MODEL[] = "SynchronizationModel";
static UCHAR B2LUFS_REG_MAX_IO[] = "MaxNumberOfIO";
static UCHAR B2LUFS_REG_MAX_IO_PER_LUN[] = "MaxIOsPerLun";
static UCHAR B2LUFS_REG_INITIAL_QUEUE_DEPTH[] = "InitialLunQueueDepth";
static UCHAR B2LUFS_REG_BUS_TYPE_FEATURE_STATUS[] = "BusTypeFeatureStatus";
static UCHAR B2LUFS_REG_UNIT_BUS_TYPE[] = "UnitBusType";
static UCHAR B2LUFS_REG_BUS_TYPE_QUERY_COUNT[] = "BusTypeQueryCount";
static UCHAR B2LUFS_REG_TIMER_HANDLE_VALID[] = "BusScanTimerHandleValid";
static UCHAR B2LUFS_REG_TIMER_INITIALIZE_STATUS[] = "BusScanTimerInitializeStatus";
static UCHAR B2LUFS_REG_TIMER_REQUEST_STATUS[] = "BusScanTimerRequestStatus";
static UCHAR B2LUFS_REG_CAPACITY_RESULT[] = "ReadCapacityResult";
static UCHAR B2LUFS_REG_CAPACITY_SCSI_STATUS[] = "ReadCapacityScsiStatus";
static UCHAR B2LUFS_REG_CAPACITY_OPCODE[] = "ReadCapacityOpcode";
static UCHAR B2LUFS_REG_CAPACITY_RETURNED_BYTES[] = "ReadCapacityReturnedBytes";
static UCHAR B2LUFS_REG_CAPACITY_VALID[] = "ReadCapacityValid";
static UCHAR B2LUFS_REG_CAPACITY_BLOCK_BYTES[] = "ReadCapacityBlockBytes";
static UCHAR B2LUFS_REG_CAPACITY_RAW[] = "ReadCapacityRaw";
static UCHAR B2LUFS_REG_LUNS_RESULT[] = "ReportLunsResult";
static UCHAR B2LUFS_REG_LUNS_SCSI_STATUS[] = "ReportLunsScsiStatus";
static UCHAR B2LUFS_REG_LUNS_LIST_BYTES[] = "ReportLunsListBytes";
static UCHAR B2LUFS_REG_LUNS_RETURNED_BYTES[] = "ReportLunsReturnedBytes";
static UCHAR B2LUFS_REG_LUNS_ENTRY_COUNT[] = "ReportLunsEntryCount";
static UCHAR B2LUFS_REG_LUNS_RAW[] = "ReportLunsRaw";

typedef enum B2LUFS_FIND_FAILURE {
    B2LUfsFindFailureRangeCount = 1,
    B2LUfsFindFailureMap = 2,
    B2LUfsFindFailureResourceContract = 3,
    B2LUfsFindFailureTimer = 4
} B2LUFS_FIND_FAILURE;

typedef struct B2LUFS_FIND_FAILURE_DATA {
    ULONG Failure;
    ULONG RangeIndex;
    ULONG RangeCount;
    ULONG InterfaceType;
    ULONG BusNumber;
    ULONG RangeLength;
    ULONGLONG RangeBase;
} B2LUFS_FIND_FAILURE_DATA;

typedef struct B2LUFS_DEFERRED_BUS_SCAN_DATA {
    ULONG Magic;
    ULONG DelayMicroseconds;
    ULONG TimerInitializeStatus;
    ULONG TimerRequestStatus;
    ULONG TransportReady;
    ULONG HciStatus;
} B2LUFS_DEFERRED_BUS_SCAN_DATA;

C_ASSERT(sizeof(B2LUFS_DEFERRED_BUS_SCAN_DATA) == 24);

/*
 * Faz 0 derlemesi CRT'ye baglanmaz. RtlZeroMemory'nin libc'ye donusme
 * olasiligini da ortadan kaldirmak icin bu kucuk yerel yardimci kullanilir.
 */
static VOID
B2LUfsZeroMemory(_Out_ PVOID Buffer, _In_ SIZE_T ByteCount)
{
    volatile UCHAR *Cursor = (volatile UCHAR *)Buffer;

    while (ByteCount-- != 0) {
        *Cursor++ = 0;
    }
}

static BOOLEAN
B2LUfsArgumentHasDumpFlag(_In_opt_ const CHAR *ArgumentString)
{
    static const CHAR DumpToken[] = "dump=1";
    const CHAR *Cursor;
    ULONG Index;

    if (ArgumentString == NULL) {
        return FALSE;
    }

    for (Cursor = ArgumentString; *Cursor != '\0'; ++Cursor) {
        for (Index = 0; Index < sizeof(DumpToken) - 1UL; ++Index) {
            CHAR Left = Cursor[Index];
            CHAR Right = DumpToken[Index];

            if (Left >= 'A' && Left <= 'Z') {
                Left = (CHAR)(Left - 'A' + 'a');
            }
            if (Left == '\0' || Left != Right) {
                break;
            }
        }
        if (Index == sizeof(DumpToken) - 1UL) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
B2LUfsLogFindFailure(
    _In_ PVOID DeviceExtension,
    _In_ BOOLEAN Suppress,
    _In_ B2LUFS_FIND_FAILURE Failure,
    _In_ ULONG RangeIndex,
    _In_opt_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ ULONGLONG RangeBase,
    _In_ ULONG RangeLength)
{
    B2LUFS_FIND_FAILURE_DATA Data;

    if (Suppress) {
        return;
    }

    B2LUfsZeroMemory(&Data, sizeof(Data));
    Data.Failure = (ULONG)Failure;
    Data.RangeIndex = RangeIndex;
    Data.RangeBase = RangeBase;
    Data.RangeLength = RangeLength;
    if (ConfigInfo != NULL) {
        Data.RangeCount = ConfigInfo->NumberOfAccessRanges;
        Data.InterfaceType = (ULONG)ConfigInfo->AdapterInterfaceType;
        Data.BusNumber = ConfigInfo->SystemIoBusNumber;
    }

    /*
     * ETW'nin devre disi oldugu WinPE'lerde StorPort bu istegi zararsizca
     * reddedebilir. Tani kanali hicbir zaman PnP sonucunu degistirmez.
     */
    (void)StorPortEtwLogError(
        DeviceExtension,
        NULL,
        B2LUFS_ETW_FIND_ADAPTER_FAILURE,
        L"B2LUFS HwFindAdapter resource contract failure",
        sizeof(Data),
        &Data);
}

static VOID
B2LUfsDeferredBusScan(
    _In_ PVOID DeviceExtension,
    _In_opt_ PVOID Context)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;
    B2LUFS_DEFERRED_BUS_SCAN_DATA Data;

    UNREFERENCED_PARAMETER(Context);
    B2LUfsZeroMemory(&Data, sizeof(Data));
    Data.Magic = 0x31544C42UL; /* "BLT1" little-endian */
    Data.DelayMicroseconds = B2LUFS_BUS_SCAN_DELAY_US;
    Data.TimerInitializeStatus = Adapter->BusScanTimerInitializeStatus;
    Data.TimerRequestStatus = Adapter->BusScanTimerRequestStatus;
    Data.TransportReady = Adapter->TransportReady ? 1UL : 0UL;
    Data.HciStatus = Adapter->ControllerStatus;

    /*
     * This record distinguishes a timer that actually fired from an early
     * BusChangeDetected notification that StorPort coalesced during adapter
     * initialization.  It contains controller state only, never media data.
     */
    (void)StorPortEtwLogError(
        DeviceExtension,
        NULL,
        B2LUFS_ETW_DEFERRED_BUS_SCAN,
        L"B2LUFS deferred bus scan timer fired; binary data is BLT1",
        sizeof(Data),
        &Data);

    if (!Adapter->TransportReady || Adapter->AdapterRemoved) {
        return;
    }

    StorPortNotification(BusChangeDetected, DeviceExtension, (UCHAR)0);
}

/*
 * HwFindAdapter yarıda başarısız olursa StorPort eşlemelerini aynı çağrıda
 * geri vermeliyiz. Bu aşamada kalıcı eşleme yalnız kaynak doğrulamasından
 * sonra Adapter->Resources içine geçer.
 */
static VOID
B2LUfsReleaseGivenResources(
    _In_ PVOID DeviceExtension,
    _In_ B2LUFS_RESOURCE *Given)
{
    ULONG Index;

    for (Index = 0; Index < B2LUFS_RESOURCE_COUNT; ++Index) {
        if (Given[Index].MappedBase != NULL) {
            StorPortFreeDeviceBase(DeviceExtension, Given[Index].MappedBase);
            Given[Index].MappedBase = NULL;
        }
    }
}

static HW_FIND_ADAPTER B2LUfsFindAdapter;

_Use_decl_annotations_
static ULONG
B2LUfsFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ PBOOLEAN Reserved3)
{
    B2LUFS_RESOURCE Given[B2LUFS_RESOURCE_COUNT];
    B2LUFS_ADAPTER *Adapter;
    BOOLEAN FeatureList[StorportFeatureMax];
    ULONG BusTypeFeatureStatus;
    ULONG Index;
    BOOLEAN DumpMode;

    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(Reserved3);

    /*
     * Microsoft documents three dump-stack signals: NULL DriverEntry
     * arguments (carried through HwContext below), ConfigInfo->DumpMode and
     * the legacy "dump=1" argument string. Honour all three before allocating
     * timers or enabling PASSIVE-only diagnostics.
     */
    DumpMode = ConfigInfo->DumpMode || HwContext != NULL ||
               B2LUfsArgumentHasDumpFlag(ArgumentString);

    /*
     * Microsoft requires this registration at the beginning of every
     * HwFindAdapter invocation before ScsiUnitQueryBusType may be advertised.
     * Without it StorPort never asks for the LUN bus type and disk.sys falls
     * back to the controller's legacy Fibre value.
     */
    B2LUfsZeroMemory(FeatureList, sizeof(FeatureList));
    FeatureList[StorportFeatureBusTypeUnitControl] = TRUE;
    BusTypeFeatureStatus = StorPortSetFeatureList(
        DeviceExtension,
        StorportFeatureMax,
        FeatureList);
    if (BusTypeFeatureStatus != STOR_STATUS_SUCCESS) {
        return SP_RETURN_ERROR;
    }

    if (ConfigInfo->NumberOfAccessRanges != B2LUFS_RESOURCE_COUNT ||
        ConfigInfo->AccessRanges == NULL) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            DumpMode,
            B2LUfsFindFailureRangeCount,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        return SP_RETURN_BAD_CONFIG;
    }

    B2LUfsZeroMemory(Given, sizeof(Given));
    for (Index = 0; Index < B2LUFS_RESOURCE_COUNT; ++Index) {
        /*
         * WDK bunu PACCESS_RANGE (*)[] olarak bildirir, duz PACCESS_RANGE
         * olarak degil. Once belirsiz-boyutlu diziyi dereference etmek hem
         * WDK 28000 ile derlenir hem de StorPort'un verdigi dizinin gercek
         * elemanini alir; ConfigInfo->AccessRanges[Index] yanlistir.
         */
        PACCESS_RANGE Range = &(*ConfigInfo->AccessRanges)[Index];

        Given[Index].Base = Range->RangeStart.QuadPart;
        Given[Index].Length = Range->RangeLength;
        Given[Index].InMemory = Range->RangeInMemory ? 1U : 0U;

        /*
         * Önemli: buradaki adres StorPort'un PnP'den aldığı adrestir.
         * Miniport kendi sabit fiziksel adresini üretip haritalamaz.
         */
        Given[Index].MappedBase = StorPortGetDeviceBase(
            DeviceExtension,
            ConfigInfo->AdapterInterfaceType,
            ConfigInfo->SystemIoBusNumber,
            Range->RangeStart,
            Range->RangeLength,
            /* StorPort API'si "InIoSpace" bekler; ACCESS_RANGE tersidir. */
            Range->RangeInMemory ? FALSE : TRUE);
        if (Given[Index].MappedBase == NULL) {
            B2LUfsLogFindFailure(
                DeviceExtension,
                DumpMode,
                B2LUfsFindFailureMap,
                Index,
                ConfigInfo,
                Given[Index].Base,
                Given[Index].Length);
            B2LUfsReleaseGivenResources(DeviceExtension, Given);
            return SP_RETURN_BAD_CONFIG;
        }
    }

    Adapter = (B2LUFS_ADAPTER *)DeviceExtension;
    /*
     * StorPort zeroes a physical miniport's device extension only once and
     * explicitly preserves it across PnP stop/restart.  Clear only an
     * extension that has never completed our first FindAdapter call.
     */
    if (Adapter->Signature != B2LUFS_ADAPTER_SIGNATURE) {
        B2LUfsZeroMemory(Adapter, sizeof(*Adapter));
    }
    Adapter->DumpMode = DumpMode;
    Adapter->BusTypeFeatureStatus = BusTypeFeatureStatus;
    Adapter->ConfiguredUnitBusType = (ULONG)BusTypeUfs;
    if (B2LUfsBindResources(Given, B2LUFS_RESOURCE_COUNT,
                            &Adapter->Resources) != 0) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            DumpMode,
            B2LUfsFindFailureResourceContract,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        return SP_RETURN_BAD_CONFIG;
    }

    /*
     * DMA yapılandırması FindAdapter içinde yapılmalıdır: StorPort'un
     * uncached ortak-bellek API'si ConfigInfo'nun DMA sözleşmesini burada
     * bekler. Bu çağrı HCI'ye ya da herhangi bir UFS LUN'una dokunmaz.
     */
    /*
     * İstek üst sınırı 32 adet doğrulanmış 4 KiB PRDT parçasıdır (128 KiB).
     * Transport, Windows fiziksel sayfa sınırlarını bu çok girişli PRDT'ye
     * böler; daha büyük bir transfer StorPort üst sınırıyla parçalanır.
     */
    if (!B2LUfsAllocateTransport(DeviceExtension, Adapter, ConfigInfo)) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            DumpMode,
            B2LUfsFindFailureResourceContract,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        B2LUfsZeroMemory(&Adapter->Resources, sizeof(Adapter->Resources));
        return SP_RETURN_BAD_CONFIG;
    }

    /*
     * Unlike RequestTimerCall's implicit timer, this object has observable
     * creation and scheduling results.  Microsoft recommends allocating an
     * explicit StorPort timer in HwFindAdapter so its resources are known to
     * be available before adapter initialization completes.
     */
    if (!Adapter->DumpMode && Adapter->BusScanTimer == NULL) {
        Adapter->BusScanTimerInitializeStatus =
            StorPortInitializeTimer(DeviceExtension, &Adapter->BusScanTimer);
    }
    if (!Adapter->DumpMode &&
        (Adapter->BusScanTimerInitializeStatus != STOR_STATUS_SUCCESS ||
         Adapter->BusScanTimer == NULL)) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            DumpMode,
            B2LUfsFindFailureTimer,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        B2LUfsZeroMemory(&Adapter->Resources, sizeof(Adapter->Resources));
        return SP_RETURN_ERROR;
    }
    Adapter->Signature = B2LUFS_ADAPTER_SIGNATURE;
    Adapter->AdapterRemoved = FALSE;
    return SP_RETURN_FOUND;
}

static HW_INITIALIZE B2LUfsInitialize;
static HW_PASSIVE_INITIALIZE_ROUTINE B2LUfsPassiveInitialize;

/* HwInitialize runs at DIRQL; registry publication is deferred to PASSIVE. */
static VOID
B2LUfsPublishLunSnapshot(
    _In_ PVOID DeviceExtension,
    _In_ B2LUFS_ADAPTER *Adapter)
{
    ULONG SchemaVersion = 12;
    ULONG CapacityScsiStatus = Adapter->CapacityProbeScsiStatus;
    ULONG CapacityOpcode = Adapter->CapacityProbeOpcode;
    ULONG CapacityValid = Adapter->CapacityValid ? 1UL : 0UL;
    ULONG ScsiStatus = Adapter->LunReportScsiStatus;
    ULONG TimerHandleValid = Adapter->BusScanTimer != NULL ? 1UL : 0UL;
    ULONG Index;
    UCHAR ValueName[] = "Lun00Id";

    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_SCHEMA_VERSION,
        REG_DWORD, &SchemaVersion, sizeof(SchemaVersion));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_HCI_CAP,
        REG_DWORD, &Adapter->ControllerCapabilities,
        sizeof(Adapter->ControllerCapabilities));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_HCI_STATUS,
        REG_DWORD, &Adapter->ControllerStatus,
        sizeof(Adapter->ControllerStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_AHIT_BEFORE,
        REG_DWORD, &Adapter->AutoHibern8Before,
        sizeof(Adapter->AutoHibern8Before));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TRANSFER_SLOTS,
        REG_DWORD, &Adapter->TransferSlotCount,
        sizeof(Adapter->TransferSlotCount));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TASK_SLOTS,
        REG_DWORD, &Adapter->TaskSlotCount,
        sizeof(Adapter->TaskSlotCount));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_SRB_TYPE,
        REG_DWORD, &Adapter->ConfiguredSrbType,
        sizeof(Adapter->ConfiguredSrbType));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_ADDRESS_TYPE,
        REG_DWORD, &Adapter->ConfiguredAddressType,
        sizeof(Adapter->ConfiguredAddressType));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_BUS_COUNT,
        REG_DWORD, &Adapter->ConfiguredNumberOfBuses,
        sizeof(Adapter->ConfiguredNumberOfBuses));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_MAX_TARGETS,
        REG_DWORD, &Adapter->ConfiguredMaximumTargets,
        sizeof(Adapter->ConfiguredMaximumTargets));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_INITIATOR_ID,
        REG_DWORD, &Adapter->ConfiguredInitiatorBusId,
        sizeof(Adapter->ConfiguredInitiatorBusId));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_SCATTER_GATHER,
        REG_DWORD, &Adapter->ConfiguredScatterGather,
        sizeof(Adapter->ConfiguredScatterGather));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_MASTER,
        REG_DWORD, &Adapter->ConfiguredMaster,
        sizeof(Adapter->ConfiguredMaster));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TAGGED_QUEUEING,
        REG_DWORD, &Adapter->ConfiguredTaggedQueuing,
        sizeof(Adapter->ConfiguredTaggedQueuing));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_AUTO_SENSE,
        REG_DWORD, &Adapter->ConfiguredAutoRequestSense,
        sizeof(Adapter->ConfiguredAutoRequestSense));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_MULTIPLE_REQUESTS,
        REG_DWORD, &Adapter->ConfiguredMultipleRequestPerLu,
        sizeof(Adapter->ConfiguredMultipleRequestPerLu));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_PHYSICAL_BREAKS,
        REG_DWORD, &Adapter->ConfiguredPhysicalBreaks,
        sizeof(Adapter->ConfiguredPhysicalBreaks));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_SYNCHRONIZATION_MODEL,
        REG_DWORD, &Adapter->ConfiguredSynchronizationModel,
        sizeof(Adapter->ConfiguredSynchronizationModel));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_MAX_IO,
        REG_DWORD, &Adapter->ConfiguredMaxNumberOfIo,
        sizeof(Adapter->ConfiguredMaxNumberOfIo));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_MAX_IO_PER_LUN,
        REG_DWORD, &Adapter->ConfiguredMaxIoPerLun,
        sizeof(Adapter->ConfiguredMaxIoPerLun));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_INITIAL_QUEUE_DEPTH,
        REG_DWORD, &Adapter->ConfiguredInitialLunQueueDepth,
        sizeof(Adapter->ConfiguredInitialLunQueueDepth));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY,
        B2LUFS_REG_BUS_TYPE_FEATURE_STATUS,
        REG_DWORD, &Adapter->BusTypeFeatureStatus,
        sizeof(Adapter->BusTypeFeatureStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_UNIT_BUS_TYPE,
        REG_DWORD, &Adapter->ConfiguredUnitBusType,
        sizeof(Adapter->ConfiguredUnitBusType));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_BUS_TYPE_QUERY_COUNT,
        REG_DWORD, &Adapter->BusTypeQueryCount,
        sizeof(Adapter->BusTypeQueryCount));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TIMER_HANDLE_VALID,
        REG_DWORD, &TimerHandleValid, sizeof(TimerHandleValid));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TIMER_INITIALIZE_STATUS,
        REG_DWORD, &Adapter->BusScanTimerInitializeStatus,
        sizeof(Adapter->BusScanTimerInitializeStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_TIMER_REQUEST_STATUS,
        REG_DWORD, &Adapter->BusScanTimerRequestStatus,
        sizeof(Adapter->BusScanTimerRequestStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_RESULT,
        REG_DWORD, &Adapter->CapacityProbeResult,
        sizeof(Adapter->CapacityProbeResult));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_SCSI_STATUS,
        REG_DWORD, &CapacityScsiStatus, sizeof(CapacityScsiStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_OPCODE,
        REG_DWORD, &CapacityOpcode, sizeof(CapacityOpcode));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_RETURNED_BYTES,
        REG_DWORD, &Adapter->CapacityProbeBytes,
        sizeof(Adapter->CapacityProbeBytes));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_VALID,
        REG_DWORD, &CapacityValid, sizeof(CapacityValid));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_BLOCK_BYTES,
        REG_DWORD, &Adapter->CapacityBlockLength,
        sizeof(Adapter->CapacityBlockLength));
    if (Adapter->CapacityProbeBytes != 0) {
        (void)StorPortRegistryWriteAdapterKey(
            DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_CAPACITY_RAW,
            REG_BINARY, Adapter->CapacityProbeVirtual,
            Adapter->CapacityProbeBytes);
    }
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_RESULT,
        REG_DWORD, &Adapter->LunReportResult, sizeof(Adapter->LunReportResult));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_SCSI_STATUS,
        REG_DWORD, &ScsiStatus, sizeof(ScsiStatus));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_LIST_BYTES,
        REG_DWORD, &Adapter->LunReportListLength,
        sizeof(Adapter->LunReportListLength));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_RETURNED_BYTES,
        REG_DWORD, &Adapter->LunReportBytes, sizeof(Adapter->LunReportBytes));
    (void)StorPortRegistryWriteAdapterKey(
        DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_ENTRY_COUNT,
        REG_DWORD, &Adapter->LunReportEntryCount,
        sizeof(Adapter->LunReportEntryCount));

    if (Adapter->LunReportValid && Adapter->LunReportBytes != 0) {
        (void)StorPortRegistryWriteAdapterKey(
            DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_LUNS_RAW,
            REG_BINARY, Adapter->LunReportVirtual, Adapter->LunReportBytes);

        /* Make every returned 8-byte address visible as a labelled CMD row. */
        for (Index = 0; Index < Adapter->LunReportEntryCount; ++Index) {
            ValueName[3] = (UCHAR)('0' + ((Index / 10UL) % 10UL));
            ValueName[4] = (UCHAR)('0' + (Index % 10UL));
            (void)StorPortRegistryWriteAdapterKey(
                DeviceExtension, B2LUFS_REG_SUBKEY, ValueName,
                REG_BINARY,
                (PUCHAR)Adapter->LunReportVirtual + 8UL + (Index * 8UL),
                8UL);
        }
    }
}

_Use_decl_annotations_
static BOOLEAN
B2LUfsPassiveInitialize(_In_ PVOID DeviceExtension)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    /* Time-consuming polled discovery belongs at PASSIVE_LEVEL, not DIRQL. */
    B2LUfsProbeLun0Capacity(DeviceExtension, Adapter);
    B2LUfsProbeLuns(DeviceExtension, Adapter);

    /*
     * This callback runs after HwInitialize, but it is still part of the
     * StorPort initialization sequence.  Defer the actual bus change until
     * StorPort has completed its adapter-control capability query and made
     * the demand-started adapter available for normal SRB processing.
     */
    if (Adapter->BusScanTimer != NULL &&
        Adapter->BusScanTimerInitializeStatus == STOR_STATUS_SUCCESS) {
        Adapter->BusScanTimerRequestStatus = StorPortRequestTimer(
            DeviceExtension,
            Adapter->BusScanTimer,
            B2LUfsDeferredBusScan,
            NULL,
            (ULONGLONG)B2LUFS_BUS_SCAN_DELAY_US,
            0ULL);
    } else {
        Adapter->BusScanTimerRequestStatus = STOR_STATUS_INVALID_PARAMETER;
    }

    /* Publish after scheduling so WinPE can distinguish every timer state. */
    B2LUfsPublishLunSnapshot(DeviceExtension, Adapter);
    return TRUE;
}

VOID
B2LUfsReleaseBusScanTimer(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    if (Adapter->BusScanTimer != NULL) {
        /* TimerValue=0 cancels a still-pending callback before destruction. */
        (void)StorPortRequestTimer(
            DeviceExtension,
            Adapter->BusScanTimer,
            B2LUfsDeferredBusScan,
            NULL,
            0ULL,
            0ULL);
        if (StorPortFreeTimer(DeviceExtension, Adapter->BusScanTimer) ==
            STOR_STATUS_SUCCESS) {
            Adapter->BusScanTimer = NULL;
        }
    }
}

_Use_decl_annotations_
static BOOLEAN
B2LUfsInitialize(_In_ PVOID DeviceExtension)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    if (!B2LUfsStartTransport(DeviceExtension, Adapter)) {
        return FALSE;
    }

    /* Crash-dump stacks run at elevated IRQL and cannot use PASSIVE work. */
    if (Adapter->DumpMode) {
        return TRUE;
    }

    /* The diagnostic registry key is written only from PASSIVE_LEVEL. */
    return StorPortEnablePassiveInitialization(
        DeviceExtension, B2LUfsPassiveInitialize);
}

static HW_STARTIO B2LUfsStartIo;
static HW_BUILDIO B2LUfsBuildIo;
static HW_UNIT_CONTROL B2LUfsUnitControl;

_Use_decl_annotations_
static BOOLEAN
B2LUfsBuildIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    /*
     * Match Microsoft's StorAHCI fixed-media path: software-generated READ
     * CAPACITY data is written and completed here. Returning FALSE tells
     * StorPort not to pass an already-completed SRB to HwStartIo. The cached
     * geometry is immutable after the pre-enumeration probe, so concurrent
     * HwBuildIo calls only read shared adapter state.
     */
    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
        Srb->PathId == 0 && Srb->TargetId == 0 && Srb->Lun == 0 &&
        Srb->CdbLength != 0 &&
        Srb->CdbLength <= (UCHAR)sizeof(Srb->Cdb) &&
        B2LUfsTryCompleteCachedCapacity(DeviceExtension, Adapter, Srb)) {
        return FALSE;
    }

    /* PnP/lifecycle and actual UFS commands remain serialized in StartIo. */
    return TRUE;
}

static BOOLEAN
B2LUfsUnitAddressIsLun0(_In_ const STOR_ADDR_BTL8 *Address)
{
    return Address != NULL &&
           Address->Type == STOR_ADDRESS_TYPE_BTL8 &&
           Address->AddressLength == STOR_ADDR_BTL8_ADDRESS_LENGTH &&
           Address->Path == 0 && Address->Target == 0 && Address->Lun == 0;
}

_Use_decl_annotations_
static SCSI_UNIT_CONTROL_STATUS
B2LUfsUnitControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_UNIT_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    if (ControlType == ScsiQuerySupportedUnitControlTypes) {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST Supported;

        Supported = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        if (Supported == NULL) {
            return ScsiUnitControlUnsuccessful;
        }

        /* Never index outside the MaxControlType supplied by StorPort. */
        if (Supported->MaxControlType > ScsiQuerySupportedUnitControlTypes) {
            Supported->SupportedTypeList[ScsiQuerySupportedUnitControlTypes] = TRUE;
        }
        if (Supported->MaxControlType > ScsiUnitUsage) {
            Supported->SupportedTypeList[ScsiUnitUsage] = TRUE;
        }
        if (Supported->MaxControlType > ScsiUnitStart) {
            Supported->SupportedTypeList[ScsiUnitStart] = TRUE;
        }
        if (Supported->MaxControlType > ScsiUnitRemove) {
            Supported->SupportedTypeList[ScsiUnitRemove] = TRUE;
        }
        if (Supported->MaxControlType > ScsiUnitSurpriseRemoval) {
            Supported->SupportedTypeList[ScsiUnitSurpriseRemoval] = TRUE;
        }
        if (Supported->MaxControlType > ScsiUnitQueryBusType) {
            Supported->SupportedTypeList[ScsiUnitQueryBusType] = TRUE;
        }
        return ScsiUnitControlSuccess;
    }

    if (ControlType == ScsiUnitQueryBusType) {
        PSTOR_UNIT_CONTROL_QUERY_BUS_TYPE Query;

        Query = (PSTOR_UNIT_CONTROL_QUERY_BUS_TYPE)Parameters;
        if (Query == NULL ||
            !B2LUfsUnitAddressIsLun0(
                (const STOR_ADDR_BTL8 *)Query->Address)) {
            return ScsiUnitControlUnsuccessful;
        }

        Query->BusType = (ULONG)BusTypeUfs;
        Adapter->ConfiguredUnitBusType = (ULONG)BusTypeUfs;
        Adapter->BusTypeQueryCount++;
        /* Dump callbacks can run at HIGH_LEVEL and must not touch registry. */
        if (!Adapter->DumpMode) {
            (void)StorPortRegistryWriteAdapterKey(
                DeviceExtension, B2LUFS_REG_SUBKEY, B2LUFS_REG_UNIT_BUS_TYPE,
                REG_DWORD, &Adapter->ConfiguredUnitBusType,
                sizeof(Adapter->ConfiguredUnitBusType));
            (void)StorPortRegistryWriteAdapterKey(
                DeviceExtension, B2LUFS_REG_SUBKEY,
                B2LUFS_REG_BUS_TYPE_QUERY_COUNT,
                REG_DWORD, &Adapter->BusTypeQueryCount,
                sizeof(Adapter->BusTypeQueryCount));
        }
        return ScsiUnitControlSuccess;
    }

    if (ControlType == ScsiUnitUsage) {
        PSTOR_UC_DEVICE_USAGE Usage = (PSTOR_UC_DEVICE_USAGE)Parameters;

        /*
         * Paging, hibernation and dump usage is a notification, not a request
         * to change UFS media or power. The serialized transport and its dump
         * restrictions are valid for every usage type.
         */
        if (Usage == NULL ||
            !B2LUfsUnitAddressIsLun0(
                (const STOR_ADDR_BTL8 *)Usage->Address)) {
            return ScsiUnitControlUnsuccessful;
        }
        return ScsiUnitControlSuccess;
    }

    if (ControlType == ScsiUnitStart ||
        ControlType == ScsiUnitRemove ||
        ControlType == ScsiUnitSurpriseRemoval) {
        if (!B2LUfsUnitAddressIsLun0((const STOR_ADDR_BTL8 *)Parameters)) {
            return ScsiUnitControlUnsuccessful;
        }

        /* Unit transitions must not reset or write the live UFS device. */
        if (ControlType == ScsiUnitStart && !Adapter->TransportReady) {
            return ScsiUnitControlUnsuccessful;
        }
        return ScsiUnitControlSuccess;
    }

    return ScsiUnitControlUnsuccessful;
}

_Use_decl_annotations_
static BOOLEAN
B2LUfsStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    B2LUfsExecuteScsi(DeviceExtension, Adapter, Srb);
    return TRUE;
}

static HW_INTERRUPT B2LUfsInterrupt;

_Use_decl_annotations_
static BOOLEAN
B2LUfsInterrupt(_In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return FALSE;
}

static HW_RESET_BUS B2LUfsResetBus;

_Use_decl_annotations_
static BOOLEAN
B2LUfsResetBus(_In_ PVOID DeviceExtension, _In_ ULONG PathId)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    if (PathId != 0 || Adapter->AdapterRemoved) {
        return FALSE;
    }
    /* Boot/dump miniports must ignore bus reset requests in dump mode. */
    if (Adapter->DumpMode) {
        return TRUE;
    }
    return B2LUfsResetTransport(DeviceExtension, Adapter);
}

static HW_ADAPTER_CONTROL B2LUfsAdapterControl;

_Use_decl_annotations_
static SCSI_ADAPTER_CONTROL_STATUS
B2LUfsAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    if (ControlType == ScsiQuerySupportedControlTypes) {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST Supported;

        Supported = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        if (Supported == NULL) {
            /* StorPort requires success for every adapter-control callback. */
            return ScsiAdapterControlSuccess;
        }

        if (Supported->MaxControlType > ScsiQuerySupportedControlTypes) {
            Supported->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        }
        if (Supported->MaxControlType > ScsiStopAdapter) {
            Supported->SupportedTypeList[ScsiStopAdapter] = TRUE;
        }
        if (Supported->MaxControlType > ScsiRestartAdapter) {
            Supported->SupportedTypeList[ScsiRestartAdapter] = TRUE;
        }
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiStopAdapter) {
        /*
         * StorPort guarantees no outstanding request here.  Halt background
         * UTP processing, but never free PnP-owned mappings or DMA memory.
         * A surprise-removed adapter must not be touched through MMIO.
         */
        UNREFERENCED_PARAMETER(Parameters);
        if (Adapter->AdapterRemoved) {
            Adapter->TransportReady = FALSE;
            return ScsiAdapterControlSuccess;
        }
        (void)B2LUfsStopTransport(DeviceExtension, Adapter);
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiRestartAdapter) {
        UNREFERENCED_PARAMETER(Parameters);
        Adapter->AdapterRemoved = FALSE;
        (void)B2LUfsStartTransport(DeviceExtension, Adapter);
        return ScsiAdapterControlSuccess;
    }

    UNREFERENCED_PARAMETER(Parameters);
    /* StorPort only calls operations advertised in the query above. */
    return ScsiAdapterControlSuccess;
}

ULONG
DriverEntry(_In_ PVOID Argument1, _In_ PVOID Argument2)
{
    HW_INITIALIZATION_DATA InitData;
    PVOID HwContext;

    B2LUfsZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    /*
     * Internal StorPort'un sanal-adapter yolu icindir. Bu aygit ise ACPI
     * tarafindan numaralandirilan fiziksel UFS HBA'sidir. InterfaceTypeUndefined
     * + STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE ile interface/bus numarasini
     * StorPort'un PnP kaynak sahipliginden aliriz; HwFindAdapter bunu degistirmez
     * ve StorPortGetDeviceBase'e aynen geri verir.
     */
    InitData.AdapterInterfaceType = InterfaceTypeUndefined;
    /*
     * HwStartIo consumes the legacy SCSI_REQUEST_BLOCK and its PathId,
     * TargetId and Lun fields.  Tell StorPort that both the SRB and address
     * protocol are therefore the legacy BTL8 form.  Without this pair an
     * adapter can start but StorPort has no declared LUN address contract to
     * use when it creates its disk PDOs.
     */
    InitData.SrbTypeFlags = SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK;
    InitData.AddressTypeFlags = ADDRESS_TYPE_FLAG_BTL8;
    /*
     * WDK 28000 HW_INITIALIZATION_DATA, gercek callback imzasini tasimak
     * yerine HwFindAdapter'i kasitli olarak PVOID bildirir. B2LUfsFindAdapter
     * yine HW_FIND_ADAPTER ile tiplenmistir; bu atamadaki C4152 sadece WDK
     * ABI'sinin function/data pointer kovusumunden gelir.
     */
#pragma warning(push)
#pragma warning(disable : 4152)
    InitData.HwFindAdapter = B2LUfsFindAdapter;
#pragma warning(pop)
    InitData.HwInitialize = B2LUfsInitialize;
    InitData.HwBuildIo = B2LUfsBuildIo;
    InitData.HwStartIo = B2LUfsStartIo;
    InitData.HwInterrupt = B2LUfsInterrupt;
    InitData.HwResetBus = B2LUfsResetBus;
    InitData.HwAdapterControl = B2LUfsAdapterControl;
    InitData.HwUnitControl = B2LUfsUnitControl;
    InitData.DeviceExtensionSize = sizeof(B2LUFS_ADAPTER);
    InitData.NumberOfAccessRanges = B2LUFS_RESOURCE_COUNT;
    /* Match StorAHCI: software CDBs are mapped; read/write uses StorPort SGLs. */
    InitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    /*
     * StorPort fiziksel miniport sözleşmesi bu iki yeteneği zorunlu tutar.
     * B2LUFS'in tek UTRD sınırı bunları kapatarak değil, tam çift yönlü
     * StartIo kilidi altında her isteği dönüşten önce tamamlayarak uygulanır.
     */
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;
    InitData.FeatureSupport = STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT |
                              STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE |
                              STOR_FEATURE_FULL_PNP_DEVICE_CAPABILITIES;

    HwContext = (Argument1 == NULL && Argument2 == NULL) ?
                (PVOID)(ULONG_PTR)1 : NULL;
    return StorPortInitialize(Argument1, Argument2, &InitData, HwContext);
}
