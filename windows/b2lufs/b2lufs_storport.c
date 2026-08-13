/*
 * B2LUFS — Exynos 9820 physical UFS StorPort miniport, Faz 0.
 *
 * Bu dosya HCI'yi BAŞLATMAZ ve I/O ALMAZ. Tek görevi StorPort'un verdiği
 * ACPI kaynaklarını işletim sistemi bağımsız B2LUfsBindResources katmanına
 * aktarmaktır. b2lufs.inf henüz WIM'e yerleştirilmediği için bu dosya mevcut
 * WinPE davranışına dokunmaz.
 */
#include <ntddk.h>
#include <storport.h>

#include "b2lufs_hw.h"

typedef struct B2LUFS_ADAPTER {
    B2LUFS_RESOURCES Resources;
} B2LUFS_ADAPTER;

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

    RtlZeroMemory(Given, sizeof(Given));
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
    if (B2LUfsBindResources(Given, B2LUFS_RESOURCE_COUNT,
                            &Adapter->Resources) != 0) {
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        return SP_RETURN_BAD_CONFIG;
    }

    /*
     * Bunlar Faz 0'da asla I/O'ya ulaşmaz; HwInitialize bilerek FALSE döner.
     * Buna rağmen StorPort sözleşmesi için sınırları açıkça bildiriyoruz.
     * UTRD/PRDT gelince gerçek HCI kapasitesine göre yeniden hesaplanacak.
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

    /*
     * Bağlı ilk sürümde buraya sırayla VCC/PHY isolation, IO coherency,
     * 1US_TO_CNT, refclk/clock-gating, CAL pre-link, DME_LINKSTARTUP ve
     * CAL post-link gelecek. Bu tamamlanmadan TRUE dönmek güvenli değildir.
     * Faz 0 başlatmayı reddettiği için bu çağrıda alınan eşlemeleri de geri
     * veriyoruz; hiçbir kalıcı donanım sahibi bırakmıyoruz.
     */
    B2LUfsReleaseAdapterResources(Adapter);
    return FALSE;
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
            Supported->MaxControlType <= ScsiRestartAdapter) {
            return ScsiAdapterControlUnsuccessful;
        }

        /* Faz 0 aygıtı başlatmaz; yine de PnP stop/restart sözleşmesini tutar. */
        Supported->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        Supported->SupportedTypeList[ScsiStopAdapter] = TRUE;
        Supported->SupportedTypeList[ScsiRestartAdapter] = TRUE;
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiStopAdapter || ControlType == ScsiRestartAdapter) {
        UNREFERENCED_PARAMETER(DeviceExtension);
        UNREFERENCED_PARAMETER(Parameters);
        /* Donanım başlatılmadığı için durdurulacak/yeniden kurulacak I/O yok. */
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

    RtlZeroMemory(&InitData, sizeof(InitData));
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
