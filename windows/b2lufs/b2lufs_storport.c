/*
 * B2LUFS — Exynos 9820 physical UFS StorPort miniport, Faz 0.
 *
 * Bu dosya HCI'yi BAŞLATMAZ ve I/O ALMAZ. Tek görevi StorPort'un verdiği
 * ACPI kaynaklarını işletim sistemi bağımsız B2LUfsBindResources katmanına
 * aktarmaktır. İlk bağlı turda HwInitialize basariyla döner; bu yalnız PnP
 * kaynak sözleşmesini ölçer, hiçbir UFS register'ına dokunmaz ve LUN kurmaz.
 *
 * Bu fiziksel bir ACPI HBA'sıdır. AdapterInterfaceType=Internal, StorPort'un
 * sanal miniport yolu için kullandığı işarettir; bu nedenle arayüz türünü
 * StorPort'un PnP aygıtından seçmesini özellikle istiyoruz.
 */
#include <ntddk.h>
#include <storport.h>

#include "b2lufs_hw.h"

typedef struct B2LUFS_ADAPTER {
    B2LUFS_RESOURCES Resources;
} B2LUFS_ADAPTER;

/*
 * PnP, HwFindAdapter'dan yalnız bir SP_RETURN_* değeri alır. Bu değer
 * Aygıt Yöneticisi'nde genelde tek başına CM_PROB_FAILED_ADD/NO_SUCH_DEVICE
 * olarak görünür ve hangi ACPI kaynağında durduğunu söylemez. Her red yolu
 * bu küçük ikili kaydı StorPort Operational ETW kanalına bırakır. Kayıt
 * yalnız WIM'deki startnet.cmd ile okunur; UFS'ye veya HCI'ye yazı yapmaz.
 */
#define B2LUFS_ETW_FIND_ADAPTER_FAILURE  0x42324C01UL

typedef enum B2LUFS_FIND_FAILURE {
    B2LUfsFindFailureRangeCount = 1,
    B2LUfsFindFailureMap = 2,
    B2LUfsFindFailureResourceContract = 3
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

static VOID
B2LUfsLogFindFailure(
    _In_ PVOID DeviceExtension,
    _In_ B2LUFS_FIND_FAILURE Failure,
    _In_ ULONG RangeIndex,
    _In_opt_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ ULONGLONG RangeBase,
    _In_ ULONG RangeLength)
{
    B2LUFS_FIND_FAILURE_DATA Data;

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
    ULONG Index;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    UNREFERENCED_PARAMETER(Reserved3);
    if (ConfigInfo->NumberOfAccessRanges != B2LUFS_RESOURCE_COUNT ||
        ConfigInfo->AccessRanges == NULL) {
        B2LUfsLogFindFailure(
            DeviceExtension,
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
    B2LUfsZeroMemory(Adapter, sizeof(*Adapter));
    if (B2LUfsBindResources(Given, B2LUFS_RESOURCE_COUNT,
                            &Adapter->Resources) != 0) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            B2LUfsFindFailureResourceContract,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        B2LUfsReleaseGivenResources(DeviceExtension, Given);
        return SP_RETURN_BAD_CONFIG;
    }

    /*
     * Bu turda kaynaklar yalnız elde tutulur; bir register okunmaz ya da
     * yazılmaz. UTRD/PRDT sinirlari gerçek HCI kapasitesi kanitlandiktan
     * sonra ayri bir değişiklikte hesaplanacak.
     */
    /*
     * Faz 0 her SRB'yi InvalidRequest ile bitirse de fiziksel StorPort
     * miniportu olarak DMA/etiketli-kuyruk/sense temel sozlesmesini korur.
     * 4 KiB, UTRD/PRDT henuz uygulanmadigi icin bir sonraki faza kadar
     * bildirilen en muhafazakar aktarim siniridir; donanima I/O yoktur.
     */
    ConfigInfo->MaximumTransferLength = PAGE_SIZE;
    ConfigInfo->NumberOfPhysicalBreaks = 1;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MapBuffers = STOR_MAP_NO_BUFFERS;
    return SP_RETURN_FOUND;
}

static HW_INITIALIZE B2LUfsInitialize;

_Use_decl_annotations_
static BOOLEAN
B2LUfsInitialize(_In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

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
    /*
     * Faz 0 asla disk istegi kabul etmez. StorPort PnP durdurmadan once
     * SRB_FUNCTION_FLUSH gonderebilir; ortada baslatilmis HCI veya onbellek
     * olmadigi icin onu zararsiz bir no-op olarak basariyla tamamlamak
     * stop/restart sozlesmesi icin gereklidir.
     */
    Srb->DataTransferLength = 0;
    if (Srb->Function == SRB_FUNCTION_FLUSH ||
        Srb->Function == SRB_FUNCTION_SHUTDOWN) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    } else {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    }
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

    if (ControlType == ScsiStopAdapter) {
        /*
         * StorPortFreeDeviceBase yalniz HwFindAdapter icinden cagrilabilir.
         * Faz 0 HCI'yi baslatmadigi icin burada kapatilacak DMA/IRQ yoktur;
         * PnP kaynaklarinin omrunu StorPort yonetir.
         */
        UNREFERENCED_PARAMETER(DeviceExtension);
        UNREFERENCED_PARAMETER(Parameters);
        /* Donanım başlatılmadığı için durdurulacak I/O veya resetlenecek HCI yok. */
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiRestartAdapter) {
        /* Stop sonrasi da HCI/DMA kurmuyoruz; PnP yeniden baslatmasi guvenli. */
        UNREFERENCED_PARAMETER(DeviceExtension);
        UNREFERENCED_PARAMETER(Parameters);
        return ScsiAdapterControlSuccess;
    }

    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Parameters);
    return ScsiAdapterControlUnsuccessful;
}

ULONG
DriverEntry(_In_ PVOID Argument1, _In_ PVOID Argument2)
{
    HW_INITIALIZATION_DATA InitData;

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
    InitData.HwStartIo = B2LUfsStartIo;
    InitData.HwInterrupt = B2LUfsInterrupt;
    InitData.HwResetBus = B2LUfsResetBus;
    InitData.HwAdapterControl = B2LUfsAdapterControl;
    InitData.DeviceExtensionSize = sizeof(B2LUFS_ADAPTER);
    InitData.NumberOfAccessRanges = B2LUFS_RESOURCE_COUNT;
    InitData.MapBuffers = STOR_MAP_NO_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;
    InitData.FeatureSupport = STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT |
                              STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE;

    return StorPortInitialize(Argument1, Argument2, &InitData, NULL);
}
