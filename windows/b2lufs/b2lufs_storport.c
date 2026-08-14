/*
 * B2LUFS — Exynos 9820 physical UFS StorPort miniport, Faz 1.
 *
 * Faz 0 kaynak sözleşmesini korur. Faz 1, firmware'in zaten başlattığı HCI
 * linkini resetlemeden kendi tek UTRD/UCD slotuna bağlar ve yalnız LUN 0 için
 * salt-okunur SCSI SRB'lerini UFS'ye iletir. Bu, Windows'un GPT'yi okuyup
 * DiskPart'ta Android bölümlerini görmesine yeter; hiçbir yazma yolu yoktur.
 *
 * Bu fiziksel bir ACPI HBA'sıdır. AdapterInterfaceType=Internal, StorPort'un
 * sanal miniport yolu için kullandığı işarettir; bu nedenle arayüz türünü
 * StorPort'un PnP aygıtından seçmesini özellikle istiyoruz.
 */
#include <ntddk.h>
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
     * DMA yapılandırması FindAdapter içinde yapılmalıdır: StorPort'un
     * uncached ortak-bellek API'si ConfigInfo'nun DMA sözleşmesini burada
     * bekler. Bu çağrı HCI'ye ya da herhangi bir UFS LUN'una dokunmaz.
     */
    /*
     * Tek PRDT ile yalnız 4 KiB aktarım kabul edilir. Bu cihazın doğrulanan
     * mantıksal blok boyutudur; ilk GPT/partition keşfi için yeterlidir ve
     * fiziksel olarak parçalı Windows tamponlarını kabul etmeyiz.
     */
    if (!B2LUfsAllocateTransport(DeviceExtension, Adapter, ConfigInfo)) {
        B2LUfsLogFindFailure(
            DeviceExtension,
            B2LUfsFindFailureResourceContract,
            MAXULONG,
            ConfigInfo,
            0,
            0);
        return SP_RETURN_BAD_CONFIG;
    }
    return SP_RETURN_FOUND;
}

static HW_INITIALIZE B2LUfsInitialize;

_Use_decl_annotations_
static BOOLEAN
B2LUfsInitialize(_In_ PVOID DeviceExtension)
{
    B2LUFS_ADAPTER *Adapter = (B2LUFS_ADAPTER *)DeviceExtension;

    if (!B2LUfsStartTransport(DeviceExtension, Adapter)) {
        return FALSE;
    }

    /* UFS LUN 0 artık sorgulanabilir; StorPort'a tek hedefi tarat. */
    StorPortNotification(BusChangeDetected, DeviceExtension, (UCHAR)0);
    return TRUE;
}

static HW_STARTIO B2LUfsStartIo;

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

        /* Salt-okunur LUN 0 taşıması PnP stop/restart sözleşmesini tutar. */
        Supported->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        Supported->SupportedTypeList[ScsiStopAdapter] = TRUE;
        Supported->SupportedTypeList[ScsiRestartAdapter] = TRUE;
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiStopAdapter) {
        /*
         * StorPortFreeDeviceBase yalniz HwFindAdapter icinden cagrilabilir.
         * Ortak DMA belleğinin ve MMIO eşlemelerinin ömrünü StorPort yönetir.
         * HCE/link/PHY resetlenmez; Android verisine hiçbir stop yazısı yoktur.
         */
        UNREFERENCED_PARAMETER(DeviceExtension);
        UNREFERENCED_PARAMETER(Parameters);
        /* Bu ilk aşama kesme ya da eşzamanlı I/O kullanmaz. */
        return ScsiAdapterControlSuccess;
    }

    if (ControlType == ScsiRestartAdapter) {
        /* PnP yeniden başlatırsa HwInitialize canlı firmware linkini yeniden bağlar. */
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
    /* Tek UTRD slotu: StorPort'un StartIo çağrılarını sıralı tut. */
    InitData.TaggedQueuing = FALSE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = FALSE;
    InitData.FeatureSupport = STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT |
                              STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE;

    return StorPortInitialize(Argument1, Argument2, &InitData, NULL);
}
