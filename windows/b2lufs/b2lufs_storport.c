/*
 * B2LUFS — Exynos 9820 physical UFS StorPort miniport, Faz 0.
 *
 * Bu dosya HCI'yi BAŞLATMAZ ve I/O ALMAZ. Tek görevi StorPort'un verdiği
 * ACPI kaynaklarını işletim sistemi bağımsız B2LUfsBindResources katmanına
 * aktarmaktır. İlk bağlı turda HwInitialize basariyla döner; bu yalnız PnP
 * kaynak sözleşmesini ölçer, hiçbir UFS register'ına dokunmaz ve LUN kurmaz.
 */
#include <ntddk.h>
#include <storport.h>

#include "b2lufs_hw.h"

typedef struct B2LUFS_ADAPTER {
    B2LUFS_RESOURCES Resources;
} B2LUFS_ADAPTER;

/*
 * Faz 0 derlemesi CRT'ye baglanmaz. RtlZeroMemory'nin libc'ye donusme
 * olasiligini da ortadan kaldirmak icin bu kucuk yerel yardimci kullanilir.
 */
static VOID
B2LUfsZeroMemory(_Out_ PVOID Buffer, _In_ SIZE_T ByteCount)
{
    PUCHAR Cursor = (PUCHAR)Buffer;

    while (ByteCount-- != 0) {
        *Cursor++ = 0;
    }
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

static VOID
B2LUfsReleaseAdapterResources(_In_ B2LUFS_ADAPTER *Adapter)
{
    ULONG Index;

    for (Index = 0; Index < B2LUFS_RESOURCE_COUNT; ++Index) {
        if (Adapter->Resources.Base[Index] != NULL) {
            StorPortFreeDeviceBase(Adapter, Adapter->Resources.Base[Index]);
            Adapter->Resources.Base[Index] = NULL;
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
    ULONG Index;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    UNREFERENCED_PARAMETER(Reserved3);
    if (ConfigInfo->NumberOfAccessRanges != B2LUFS_RESOURCE_COUNT ||
        ConfigInfo->AccessRanges == NULL) {
        return SP_RETURN_BAD_CONFIG;
    }

    B2LUfsZeroMemory(Given, sizeof(Given));
    for (Index = 0; Index < B2LUFS_RESOURCE_COUNT; ++Index) {
        PACCESS_RANGE Range = &ConfigInfo->AccessRanges[Index];

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
            B2LUfsReleaseGivenResources(DeviceExtension, Given);
            return SP_RETURN_BAD_CONFIG;
        }
    }

    Adapter = (B2LUFS_ADAPTER *)DeviceExtension;
    B2LUfsZeroMemory(Adapter, sizeof(*Adapter));
    if (B2LUfsBindResources(Given, B2LUFS_RESOURCE_COUNT,
                            &Adapter->Resources) != 0) {
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        return SP_RETURN_BAD_CONFIG;
    }

    /*
     * Bu turda kaynaklar yalnız elde tutulur; bir register okunmaz ya da
     * yazılmaz. UTRD/PRDT sinirlari gerçek HCI kapasitesi kanitlandiktan
     * sonra ayri bir değişiklikte hesaplanacak.
     */
    ConfigInfo->MaximumTransferLength = PAGE_SIZE;
    ConfigInfo->NumberOfPhysicalBreaks = 1;
    ConfigInfo->NumberOfBuses = 1;
    return SP_RETURN_FOUND;
}

static HW_INITIALIZE B2LUfsInitialize;

_Use_decl_annotations_
static BOOLEAN
B2LUfsInitialize(_In_ PVOID DeviceExtension)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    UNREFERENCED_PARAMETER(Adapter);

    /*
     * Bu "kaynak sözleşmesi" turu için başarı, donanımın başladığı anlamına
     * gelmez. Bunun tek etkisi PnP'nin b2lufs.inf'i Started göstermesidir.
     * HCE, UIC/DME, PHY/CAL, DMA ve LUN taraması sonraki faza kadar YOKTUR.
     */
    return TRUE;
}

static HW_STARTIO B2LUfsStartIo;

_Use_decl_annotations_
static BOOLEAN
B2LUfsStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    /* Faz 0 asla disk isteği kabul etmez. */
    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    StorPortNotification(RequestComplete, DeviceExtension, Srb);
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
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(PathId);
    return FALSE;
}

static HW_ADAPTER_CONTROL B2LUfsAdapterControl;

_Use_decl_annotations_
static SCSI_ADAPTER_CONTROL_STATUS
B2LUfsAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    if (ControlType == ScsiQuerySupportedControlTypes) {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST Supported;

        Supported = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        if (Supported == NULL ||
            Supported->MaxControlType <= ScsiStopAdapter) {
            return ScsiAdapterControlUnsuccessful;
        }

        /* Faz 0 aygıtı başlatmaz; yine de PnP stop/restart sözleşmesini tutar. */
        Supported->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        Supported->SupportedTypeList[ScsiStopAdapter] = TRUE;
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiStopAdapter) {
        B2LUfsReleaseAdapterResources((B2LUFS_ADAPTER *)DeviceExtension);
        UNREFERENCED_PARAMETER(Parameters);
        /* Donanım başlatılmadığı için durdurulacak I/O veya resetlenecek HCI yok. */
        return ScsiAdapterControlSuccess;
    }

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Parameters);
    return ScsiAdapterControlUnsuccessful;
}

NTSTATUS
DriverEntry(_In_ PVOID Argument1, _In_ PVOID Argument2)
{
    HW_INITIALIZATION_DATA InitData;

    B2LUfsZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = Internal;
    InitData.HwFindAdapter = B2LUfsFindAdapter;
    InitData.HwInitialize = B2LUfsInitialize;
    InitData.HwStartIo = B2LUfsStartIo;
    InitData.HwInterrupt = B2LUfsInterrupt;
    InitData.HwResetBus = B2LUfsResetBus;
    InitData.HwAdapterControl = B2LUfsAdapterControl;
    InitData.DeviceExtensionSize = sizeof(B2LUFS_ADAPTER);
    InitData.NumberOfAccessRanges = B2LUFS_RESOURCE_COUNT;
    InitData.MapBuffers = TRUE;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;
    InitData.FeatureSupport = STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT;

    return StorPortInitialize(Argument1, Argument2, &InitData, NULL);
}
