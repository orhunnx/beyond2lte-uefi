/*
 * B2LUFS — minimal, read-only UTP transport for the Exynos 9820 UFSHCI.
 *
 * The phone's firmware has already trained the link and used it to load
 * WinPE.  We intentionally preserve that state: no HCE reset, no UIC/DME,
 * no PHY/CAL sequence, and no UFS query/write command is issued here.
 *
 * The driver owns one transfer-request slot.  StorPort's mandatory tagged
 * queue contract remains enabled, while MaxNumberOfIO=1 and half-duplex
 * dispatch serialize StartIo.  One slot is therefore enough to make the
 * first LUN-0 GPT and partition reads visible to DiskPart.
 */
#include <ntddk.h>
#include <storport.h>


#include "b2lufs_transport.h"

#define B2LUFS_UTRD_ALIGNMENT             1024UL
#define B2LUFS_UCD_ALIGNMENT              1024UL
#define B2LUFS_ALIGNED_UPIU_SIZE           1024UL
#define B2LUFS_UTRD_SLOT_BYTES               32UL
#define B2LUFS_PRDT_ENTRY_BYTES              16UL
#define B2LUFS_MAX_PRDT_ENTRIES               32UL
/*
 * Exynos' VS-HCI PRDT entry-size registers are programmed with 12 below,
 * i.e. 2^12 = 4 KiB.  Split a physically contiguous StorPort segment at
 * that boundary instead of depending on the Windows buffer being contiguous
 * or on an undocumented larger vendor entry size.
 */
#define B2LUFS_PRDT_MAX_BYTES              4096UL
/*
 * Linux programs Exynos' PRDT-entry-size registers to 4 KiB and then uses
 * one entry per scatter/gather segment.  The descriptor below has 32 entries,
 * so Windows may submit a normal 128 KiB GPT/partition-table read without
 * inventing a larger vendor PRDT entry size.
 */
#define B2LUFS_MAX_TRANSFER \
    (B2LUFS_MAX_PRDT_ENTRIES * B2LUFS_PRDT_MAX_BYTES)
#define B2LUFS_RESPONSE_UPIU_OFFSET_BYTES  B2LUFS_ALIGNED_UPIU_SIZE
#define B2LUFS_PRDT_OFFSET_BYTES \
    (B2LUFS_ALIGNED_UPIU_SIZE * 2UL)
#define B2LUFS_UCD_BYTES \
    (B2LUFS_PRDT_OFFSET_BYTES + \
     (B2LUFS_MAX_PRDT_ENTRIES * B2LUFS_PRDT_ENTRY_BYTES))
#define B2LUFS_COMMON_BUFFER_BYTES         6144UL
#define B2LUFS_CAPACITY_10_BYTES              8UL
#define B2LUFS_CAPACITY_16_BYTES             32UL
#define B2LUFS_REPORT_LUNS_BYTES             256UL
#define B2LUFS_REPORT_LUNS_HEADER_BYTES        8UL
#define B2LUFS_REPORT_LUNS_ENTRY_BYTES         8UL
/*
 * HwStartIo runs on StorPort's dispatch path. A live UFS command completes
 * in microseconds to a few milliseconds; do not make PnP wait seconds per
 * probe if firmware does not release our borrowed UTRL slot. On timeout we
 * stop submitting further work without resetting HCE/link/PHY.
 */
#define B2LUFS_COMMAND_TIMEOUT_US         50000UL
#define B2LUFS_TRACE_BUDGET                  12UL
#define B2LUFS_PNP_TRACE_BUDGET               16UL
#define B2LUFS_TRACE_SAMPLE_BYTES            64UL
#define B2LUFS_ETW_SCSI_TRACE        0x42324C02UL
#define B2LUFS_ETW_PNP_TRACE         0x42324C03UL

#define B2LUFS_HCE_ENABLE                    0x00000001UL
#define B2LUFS_UTRL_RUN_STOP                 0x00000001UL
#define B2LUFS_UTRL_SLOT_0                   0x00000001UL
#define B2LUFS_UTP_TRANSFER_REQ_COMPL        0x00000001UL
#define B2LUFS_UIC_LINK_LOST                 0x00000080UL
#define B2LUFS_DEVICE_FATAL_ERROR             0x00000800UL
#define B2LUFS_UTP_ERROR                      0x00001000UL
#define B2LUFS_CONTROLLER_FATAL_ERROR         0x00010000UL
#define B2LUFS_SYSTEM_BUS_FATAL_ERROR         0x00020000UL
#define B2LUFS_FATAL_INTERRUPT_MASK \
    (B2LUFS_UIC_LINK_LOST | B2LUFS_DEVICE_FATAL_ERROR | B2LUFS_UTP_ERROR | \
     B2LUFS_CONTROLLER_FATAL_ERROR | B2LUFS_SYSTEM_BUS_FATAL_ERROR)

#define B2LUFS_UTP_NO_DATA_TRANSFER          0x00000000UL
#define B2LUFS_UTP_HOST_TO_DEVICE             0x02000000UL
#define B2LUFS_UTP_DEVICE_TO_HOST             0x04000000UL
#define B2LUFS_UTP_SCSI_COMMAND               0x00000000UL
#define B2LUFS_UTP_REQ_DESC_INT_CMD           0x01000000UL
/* UTRD DW0[31:28]: UFS storage command type, per UFSHCI. */
#define B2LUFS_UTP_CMD_TYPE_UFS_STORAGE       0x10000000UL
#define B2LUFS_OCS_SUCCESS                    0x00000000UL
#define B2LUFS_OCS_INVALID_COMMAND_STATUS     0x0000000FUL

#define B2LUFS_UPIU_TRANSACTION_COMMAND       0x01U
#define B2LUFS_UPIU_TRANSACTION_RESPONSE      0x21U
#define B2LUFS_UPIU_FLAG_NONE                 0x00U
#define B2LUFS_UPIU_FLAG_WRITE                0x20U
#define B2LUFS_UPIU_FLAG_READ                 0x40U
#define B2LUFS_SCSI_STATUS_GOOD               0x00U
#define B2LUFS_SCSI_STATUS_CHECK_CONDITION    0x02U
#define B2LUFS_SCSI_STATUS_BUSY               0x08U
#define B2LUFS_SCSI_STATUS_TASK_SET_FULL      0x28U

/* Linux ufs.h: RESPONSE UPIU DW8 has a BE16 length then 18 sense bytes. */
#define B2LUFS_RESPONSE_SENSE_LENGTH_OFFSET     32UL
#define B2LUFS_RESPONSE_SENSE_DATA_OFFSET       34UL
#define B2LUFS_RESPONSE_SENSE_BYTES             18UL

/* Exact read-only SCSI allow-list needed by the Windows disk/GPT stack. */
#define B2LUFS_SCSI_TEST_UNIT_READY           0x00U
#define B2LUFS_SCSI_REQUEST_SENSE              0x03U
#define B2LUFS_SCSI_READ_6                     0x08U
#define B2LUFS_SCSI_INQUIRY                    0x12U
#define B2LUFS_SCSI_MODE_SENSE_6               0x1AU
#define B2LUFS_SCSI_START_STOP_UNIT             0x1BU
#define B2LUFS_SCSI_READ_FORMAT_CAPACITIES     0x23U
#define B2LUFS_SCSI_READ_CAPACITY_10           0x25U
#define B2LUFS_SCSI_READ_10                    0x28U
#define B2LUFS_SCSI_READ_BUFFER                0x3CU
#define B2LUFS_SCSI_MODE_SENSE_10              0x5AU
#define B2LUFS_SCSI_READ_16                    0x88U
#define B2LUFS_SCSI_SERVICE_ACTION_IN_16       0x9EU
#define B2LUFS_SCSI_SAI_READ_CAPACITY_16       0x10U
#define B2LUFS_SCSI_REPORT_LUNS                0xA0U
#define B2LUFS_SCSI_READ_12                    0xA8U
#define B2LUFS_SCSI_SECURITY_PROTOCOL_IN       0xA2U

/* UFSHCI offsets, all relative to the first (HCI) or second (VS) resource. */
#define B2LUFS_REG_IE                          0x0024UL
#define B2LUFS_REG_HCS                         0x0030UL
#define B2LUFS_REG_UTRLBA                      0x0050UL
#define B2LUFS_REG_UTRLBAU                     0x0054UL
#define B2LUFS_REG_UTRLDBR                     0x0058UL
#define B2LUFS_REG_UTRLRSR                     0x0060UL
#define B2LUFS_REG_AHIT                        0x0018UL
#define B2LUFS_VS_TXPRDT_ENTRY_SIZE            0x0000UL
#define B2LUFS_VS_RXPRDT_ENTRY_SIZE            0x0004UL
#define B2LUFS_VS_UTRL_NEXUS_TYPE              0x0040UL
#define B2LUFS_VS_UTMRL_NEXUS_TYPE             0x0044UL
#define B2LUFS_VS_DATA_REORDER                 0x0060UL
#define B2LUFS_VS_AXIDMA_RWDATA_BURST_LEN      0x006CUL

/* UFSHCI CAP/HCS fields used by Linux before it starts a UTP list. */
#define B2LUFS_CAP_NUTRS_MASK                  0x0000001FUL
#define B2LUFS_CAP_NUTMRS_MASK                 0x00070000UL
#define B2LUFS_CAP_NUTMRS_SHIFT                16UL
#define B2LUFS_CAP_64_ADDRESSING               0x01000000UL
#define B2LUFS_HCS_DEVICE_PRESENT              0x00000001UL
#define B2LUFS_HCS_UTRL_READY                  0x00000002UL
#define B2LUFS_HCS_UTMRL_READY                 0x00000004UL
#define B2LUFS_HCS_UIC_READY                   0x00000008UL
#define B2LUFS_HCS_READY_MASK \
    (B2LUFS_HCS_DEVICE_PRESENT | B2LUFS_HCS_UTRL_READY | \
     B2LUFS_HCS_UTMRL_READY | B2LUFS_HCS_UIC_READY)

#pragma pack(push, 1)
typedef struct B2LUFS_UTRD {
    ULONG Dw0;
    ULONG Dw1;
    ULONG Dw2;
    ULONG Dw3;
    ULONG CommandDescriptorBaseLow;
    ULONG CommandDescriptorBaseHigh;
    USHORT ResponseUpiuLength;
    USHORT ResponseUpiuOffset;
    USHORT PrdtLength;
    USHORT PrdtOffset;
} B2LUFS_UTRD;

typedef struct B2LUFS_UPIU {
    UCHAR TransactionCode;
    UCHAR Flags;
    UCHAR Lun;
    UCHAR TaskTag;
    UCHAR CommandSetType;
    UCHAR QueryFunction;
    UCHAR Response;
    UCHAR Status;
    UCHAR EhsLength;
    UCHAR DeviceInformation;
    USHORT DataSegmentLength;
    UCHAR TaskSpecificFields[20];
} B2LUFS_UPIU;

typedef struct B2LUFS_PRDT {
    ULONG BaseAddressLow;
    ULONG BaseAddressHigh;
    ULONG Reserved;
    ULONG DataByteCount;
} B2LUFS_PRDT;
#pragma pack(pop)

C_ASSERT(sizeof(B2LUFS_UTRD) == B2LUFS_UTRD_SLOT_BYTES);
C_ASSERT(sizeof(B2LUFS_UPIU) == 32);
C_ASSERT(sizeof(B2LUFS_PRDT) == B2LUFS_PRDT_ENTRY_BYTES);
C_ASSERT(B2LUFS_MAX_PRDT_ENTRIES <= MAXUSHORT);

typedef enum B2LUFS_TRANSFER_RESULT {
    B2LUfsTransferOk = 0,
    B2LUfsTransferBusy,
    B2LUfsTransferTimeout,
    B2LUfsTransferFatal,
    B2LUfsTransferOcs,
    B2LUfsTransferBadDma
} B2LUFS_TRANSFER_RESULT;

/*
 * The miniport cannot safely print to the WinPE console from HwStartIo.
 * Store the first bytes returned by a bounded number of read-only SRBs in
 * StorPort's Operational channel instead. startnet.cmd renders that channel
 * after DiskPart has caused the discovery reads. No UFS LUN is written.
 */
typedef struct B2LUFS_SCSI_TRACE_DATA {
    ULONG Magic;
    ULONG Sequence;
    ULONG TransferResult;
    ULONG RequestedLength;
    ULONG UtrdOcs;
    ULONG InterruptStatus;
    UCHAR SrbFunction;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbOperation;
    UCHAR SrbStatus;
    UCHAR ScsiStatus;
    UCHAR SampleLength;
    UCHAR Reserved;
    UCHAR Sample[B2LUFS_TRACE_SAMPLE_BYTES];
} B2LUFS_SCSI_TRACE_DATA;

C_ASSERT(sizeof(B2LUFS_SCSI_TRACE_DATA) == 96);

/*
 * Legacy StorPort sends SRB_FUNCTION_PNP as SCSI_PNP_REQUEST_BLOCK, not as
 * a SCSI CDB.  Keep a separate record so normal BLS1 tracing never reads a
 * non-existent CDB from a PnP request.  BLP1 contains only lifecycle state
 * and no UFS-media bytes.
 */
typedef struct B2LUFS_PNP_TRACE_DATA {
    ULONG Magic;
    ULONG Sequence;
    ULONG PnpAction;
    ULONG SrbPnpFlags;
    ULONG DataTransferLength;
    ULONG SrbStatus;
    ULONG TransportReady;
    UCHAR SrbFunction;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
} B2LUFS_PNP_TRACE_DATA;

C_ASSERT(sizeof(B2LUFS_PNP_TRACE_DATA) == 32);

static VOID
B2LUfsZeroMemory(_Out_ PVOID Buffer, _In_ SIZE_T ByteCount)
{
    volatile UCHAR *Cursor = (volatile UCHAR *)Buffer;

    while (ByteCount-- != 0) {
        *Cursor++ = 0;
    }
}

static VOID
B2LUfsCopyMemory(_Out_ PVOID Destination, _In_reads_bytes_(ByteCount) const VOID *Source,
                 _In_ SIZE_T ByteCount)
{
    volatile UCHAR *To = (volatile UCHAR *)Destination;
    const UCHAR *From = (const UCHAR *)Source;

    while (ByteCount-- != 0) {
        *To++ = *From++;
    }
}

static ULONGLONG
B2LUfsReadBe64(_In_reads_bytes_(8) const UCHAR *Buffer)
{
    ULONGLONG Value = 0;
    ULONG Index;

    for (Index = 0; Index < 8UL; ++Index) {
        Value = (Value << 8) | Buffer[Index];
    }
    return Value;
}

static ULONG
B2LUfsReadBe32(_In_reads_bytes_(4) const UCHAR *Buffer)
{
    ULONG Value = 0;
    ULONG Index;

    for (Index = 0; Index < 4UL; ++Index) {
        Value = (Value << 8) | Buffer[Index];
    }
    return Value;
}

static USHORT
B2LUfsReadBe16(_In_reads_bytes_(2) const UCHAR *Buffer)
{
    return (USHORT)(((USHORT)Buffer[0] << 8) | Buffer[1]);
}

static VOID
B2LUfsLogScsiTrace(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ B2LUFS_TRANSFER_RESULT Result,
    _In_ UCHAR SrbStatus)
{
    B2LUFS_SCSI_TRACE_DATA Data;
    ULONG SampleLength;

    if (Adapter->TraceBudget == 0) {
        return;
    }
    Adapter->TraceBudget--;

    B2LUfsZeroMemory(&Data, sizeof(Data));
    Data.Magic = 0x31534C42UL; /* "BLS1" little-endian */
    Data.Sequence = ++Adapter->TraceSequence;
    Data.TransferResult = (ULONG)Result;
    Data.RequestedLength = Srb->DataTransferLength;
    Data.UtrdOcs = Adapter->LastUtrdOcs;
    Data.InterruptStatus = Adapter->LastInterruptStatus;
    Data.SrbFunction = Srb->Function;
    Data.TargetId = Srb->TargetId;
    Data.Lun = Srb->Lun;
    Data.CdbOperation = Srb->Cdb[0];
    Data.SrbStatus = SrbStatus;
    Data.ScsiStatus = Srb->ScsiStatus;

    if (Result == B2LUfsTransferOk &&
        Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD &&
        Srb->DataBuffer != NULL && Srb->DataTransferLength != 0) {
        SampleLength = Srb->DataTransferLength;
        if (SampleLength > B2LUFS_TRACE_SAMPLE_BYTES) {
            SampleLength = B2LUFS_TRACE_SAMPLE_BYTES;
        }
        B2LUfsCopyMemory(Data.Sample, Srb->DataBuffer, SampleLength);
        Data.SampleLength = (UCHAR)SampleLength;
    } else if ((SrbStatus & SRB_STATUS_AUTOSENSE_VALID) != 0 &&
               Srb->SenseInfoBuffer != NULL &&
               Srb->SenseInfoBufferLength != 0) {
        SampleLength = Srb->SenseInfoBufferLength;
        if (SampleLength > B2LUFS_RESPONSE_SENSE_BYTES) {
            SampleLength = B2LUFS_RESPONSE_SENSE_BYTES;
        }
        B2LUfsCopyMemory(Data.Sample, Srb->SenseInfoBuffer, SampleLength);
        Data.SampleLength = (UCHAR)SampleLength;
    }

    (void)StorPortEtwLogError(
        DeviceExtension,
        NULL,
        B2LUFS_ETW_SCSI_TRACE,
        L"B2LUFS read-only SCSI/UFS trace; binary data is BLS1 header then sample bytes",
        sizeof(Data),
        &Data);
}

static VOID
B2LUfsLogPnpTrace(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ PSCSI_PNP_REQUEST_BLOCK Pnp)
{
    B2LUFS_PNP_TRACE_DATA Data;
    volatile LONG *Budget = (volatile LONG *)&Adapter->PnpTraceBudget;

    /*
     * ATOMIK ISLEM KULLANILMIYOR - BILEREK.
     *
     * Onceki surum InterlockedCompareExchange/InterlockedIncrement
     * kullaniyordu ve ARM64'te link su iki sembolde patliyordu:
     *
     *   error LNK2019: cozumlenmemis dis sembol _InterlockedCompareExchange
     *   error LNK2019: cozumlenmemis dis sembol _InterlockedIncrement
     *
     * ARM64'te bunlar kutuphane islevi degil derleyici icsel islevidir ve
     * /kernel altinda icsel olarak sunulmayabiliyor; derleyici o zaman
     * disaridan cagri uretiyor. #pragma intrinsic ile zorlamak da cozum
     * degil: desteklenmeyen bir ad verilirse C4163 uretiliyor ve /WX
     * altinda bu dogrudan hataya donusuyor.
     *
     * Bu iki sayac YALNIZCA TANI kaydi icin. Atomikligin karsiligi, en kotu
     * ihtimalle bir izleme kaydinin fazla ya da eksik yazilmasi. Sayaclar
     * ne UFS'e komut gonderiyor ne de bir kaynagi koruyor. Ustelik
     * ConfigInfo'da MaxNumberOfIO=1 ve StorSynchronizeHalfDuplex ilan
     * edildigi icin StorPort bu miniportu zaten sirali surer.
     *
     * Toolchain'le guresmek yerine ihtiyaci ortadan kaldirmak dogrusu.
     */
    if (*Budget <= 0) {
        return;
    }
    *Budget = *Budget - 1;

    B2LUfsZeroMemory(&Data, sizeof(Data));
    Data.Magic = 0x31504C42UL; /* "BLP1" little-endian */
    Adapter->PnpTraceSequence = Adapter->PnpTraceSequence + 1;
    Data.Sequence = (ULONG)Adapter->PnpTraceSequence;
    Data.PnpAction = (ULONG)Pnp->PnPAction;
    Data.SrbPnpFlags = Pnp->SrbPnPFlags;
    Data.DataTransferLength = Pnp->DataTransferLength;
    Data.SrbStatus = Pnp->SrbStatus;
    Data.TransportReady = Adapter->TransportReady ? 1UL : 0UL;
    Data.SrbFunction = Pnp->Function;
    Data.PathId = Pnp->PathId;
    Data.TargetId = Pnp->TargetId;
    Data.Lun = Pnp->Lun;

    (void)StorPortEtwLogError(
        DeviceExtension,
        NULL,
        B2LUFS_ETW_PNP_TRACE,
        L"B2LUFS legacy BTL8 PnP lifecycle trace; binary data is BLP1",
        sizeof(Data),
        &Data);
}

static ULONG
B2LUfsReadHci(_In_ PVOID DeviceExtension, _In_ B2LUFS_ADAPTER *Adapter, _In_ ULONG Offset)
{
    PUCHAR Base = (PUCHAR)Adapter->Resources.Base[B2LUfsResourceHci];

    return StorPortReadRegisterUlong(DeviceExtension, (PULONG)(Base + Offset));
}

static VOID
B2LUfsWriteHci(
    _In_ PVOID DeviceExtension,
    _In_ B2LUFS_ADAPTER *Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    PUCHAR Base = (PUCHAR)Adapter->Resources.Base[B2LUfsResourceHci];

    StorPortWriteRegisterUlong(DeviceExtension, (PULONG)(Base + Offset), Value);
}

static VOID
B2LUfsWriteVs(
    _In_ PVOID DeviceExtension,
    _In_ B2LUFS_ADAPTER *Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    PUCHAR Base = (PUCHAR)Adapter->Resources.Base[B2LUfsResourceVsHci];

    StorPortWriteRegisterUlong(DeviceExtension, (PULONG)(Base + Offset), Value);
}

static ULONG
B2LUfsAlignOffset(_In_ ULONGLONG PhysicalAddress, _In_ ULONG Alignment)
{
    ULONGLONG Mask = (ULONGLONG)Alignment - 1ULL;

    return (ULONG)((Alignment - (PhysicalAddress & Mask)) & Mask);
}

static ULONG
B2LUfsMinUlong(_In_ ULONG Left, _In_ ULONG Right)
{
    return (Left < Right) ? Left : Right;
}

static ULONG
B2LUfsSlotMask(_In_ ULONG Count)
{
    if (Count >= 32UL) {
        return MAXULONG;
    }
    return (1UL << Count) - 1UL;
}

static BOOLEAN
B2LUfsCdbIsExplicitlyReadOnly(_In_reads_bytes_(16) const UCHAR *Cdb)
{
    switch (Cdb[0]) {
    case B2LUFS_SCSI_TEST_UNIT_READY:
    case B2LUFS_SCSI_REQUEST_SENSE:
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_INQUIRY:
    case B2LUFS_SCSI_MODE_SENSE_6:
    case B2LUFS_SCSI_READ_FORMAT_CAPACITIES:
    case B2LUFS_SCSI_READ_CAPACITY_10:
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_READ_BUFFER:
    case B2LUFS_SCSI_MODE_SENSE_10:
    case B2LUFS_SCSI_READ_16:
        return TRUE;
    case B2LUFS_SCSI_SERVICE_ACTION_IN_16:
        /* 0x9e has several service actions; only READ CAPACITY(16) is used. */
        return (Cdb[1] & 0x1FU) == B2LUFS_SCSI_SAI_READ_CAPACITY_16;
    case B2LUFS_SCSI_REPORT_LUNS:
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_SECURITY_PROTOCOL_IN:
        return TRUE;
    default:
        return FALSE;
    }
}

static VOID
B2LUfsComplete(_In_ PVOID DeviceExtension, _Inout_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    StorPortNotification(RequestComplete, DeviceExtension, Srb);
}

static BOOLEAN
B2LUfsPnpAddressIsLun0(_In_ const SCSI_PNP_REQUEST_BLOCK *Pnp)
{
    return Pnp->PathId == 0 && Pnp->TargetId == 0 && Pnp->Lun == 0;
}

static VOID
B2LUfsFillDeviceCapabilities(
    _Inout_ PSTOR_DEVICE_CAPABILITIES_EX Capabilities)
{
    USHORT Version = Capabilities->Version;
    USHORT Size = Capabilities->Size;

    /* Version and Size are supplied by StorPort; preserve the caller's ABI. */
    B2LUfsZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Version = Version;
    Capabilities->Size = Size;
    Capabilities->DeviceD1 = FALSE;
    Capabilities->DeviceD2 = FALSE;
    Capabilities->LockSupported = FALSE;
    Capabilities->EjectSupported = FALSE;
    Capabilities->Removable = FALSE;
    Capabilities->DockDevice = FALSE;
    Capabilities->UniqueID = FALSE;
    Capabilities->SilentInstall = TRUE;
    Capabilities->RawDeviceOK = FALSE;
    Capabilities->SurpriseRemovalOK = FALSE;
    Capabilities->NoDisplayInUI = FALSE;
    Capabilities->DefaultWriteCacheEnabled = FALSE;
    Capabilities->Address = 0;
    Capabilities->UINumber = MAXULONG;
}

VOID
B2LUfsHandlePnpSrb(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    PSCSI_PNP_REQUEST_BLOCK Pnp = (PSCSI_PNP_REQUEST_BLOCK)Srb;
    UCHAR SrbStatus = SRB_STATUS_INVALID_REQUEST;
    BOOLEAN AdapterRequest;

    /* StorPort owns this structure because DriverEntry selected legacy SRBs. */
    if (Pnp->Length < sizeof(*Pnp)) {
        Pnp->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
        return;
    }

    AdapterRequest =
        (Pnp->SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) != 0;
    if (AdapterRequest) {
        /*
         * These requests change only StorPort/PnP state.  Do not stop the
         * firmware-owned HCE/link or issue a UFS management command here.
         */
        switch (Pnp->PnPAction) {
        case StorQueryCapabilities:
            /*
             * StorPort supplies the adapter capabilities.  Like StorAHCI,
             * consume the query and preserve its caller-owned buffer; an
             * INVALID_REQUEST here can abort the parent PnP start before
             * StorPort ever scans bus 0 for the disk PDO.
             */
            if (Pnp->DataBuffer != NULL &&
                Pnp->DataTransferLength >=
                    sizeof(STOR_DEVICE_CAPABILITIES_EX)) {
                SrbStatus = SRB_STATUS_SUCCESS;
            }
            break;
        case StorStartDevice:
            SrbStatus = Adapter->TransportReady ? SRB_STATUS_SUCCESS :
                                                  SRB_STATUS_NO_DEVICE;
            break;
        case StorStopDevice:
        case StorRemoveDevice:
        case StorSurpriseRemoval:
            SrbStatus = SRB_STATUS_SUCCESS;
            break;
        default:
            break;
        }
    } else if (B2LUfsPnpAddressIsLun0(Pnp)) {
        switch (Pnp->PnPAction) {
        case StorQueryCapabilities:
            if (Pnp->DataBuffer != NULL &&
                Pnp->DataTransferLength >= sizeof(STOR_DEVICE_CAPABILITIES_EX)) {
                B2LUfsFillDeviceCapabilities(
                    (PSTOR_DEVICE_CAPABILITIES_EX)Pnp->DataBuffer);
                SrbStatus = SRB_STATUS_SUCCESS;
            }
            break;
        case StorStartDevice:
            SrbStatus = Adapter->TransportReady ? SRB_STATUS_SUCCESS :
                                                  SRB_STATUS_NO_DEVICE;
            break;
        case StorStopDevice:
        case StorRemoveDevice:
        case StorSurpriseRemoval:
            SrbStatus = SRB_STATUS_SUCCESS;
            break;
        default:
            break;
        }
    }

    Pnp->SrbStatus = SrbStatus;
    B2LUfsLogPnpTrace(DeviceExtension, Adapter, Pnp);
    StorPortNotification(RequestComplete, DeviceExtension, Srb);
}

BOOLEAN
B2LUfsAllocateTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    ULONG Length;
    ULONG Offset;

    /*
     * The platform reports coherent 64-bit DMA (CAP[24]). Each PRDT entry
     * remains limited to one verified 4 KiB UFS logical block, but Windows
     * buffers need not be physically contiguous: B2LUfsSubmitReadOnlyScsi
     * expands a request into at most 32 such entries (128 KiB total).
     */
    /*
     * Keep this in lockstep with DriverEntry. B2LUfsStartIo receives a
     * SCSI_REQUEST_BLOCK and routes only Path=0, Target=0, Lun=0, so
     * StorPort must enumerate this adapter through the legacy BTL8 address
     * contract rather than an SRBEX/storage-address variant.
     */
    ConfigInfo->SrbType = SRB_TYPE_SCSI_REQUEST_BLOCK;
    ConfigInfo->AddressType = STORAGE_ADDRESS_TYPE_BTL8;
    Adapter->ConfiguredSrbType = (ULONG)ConfigInfo->SrbType;
    Adapter->ConfiguredAddressType = (ULONG)ConfigInfo->AddressType;

    ConfigInfo->MaximumTransferLength = B2LUFS_MAX_TRANSFER;
    ConfigInfo->NumberOfPhysicalBreaks = B2LUFS_MAX_PRDT_ENTRIES;
    ConfigInfo->NumberOfBuses = 1;
    /*
     * BASLATICI KIMLIGI TARAMADAN DISLANIR.
     *
     * ConfigInfo sifirlanmis geliyor, yani InitiatorBusId[0] = 0. Onceki
     * surumde MaximumNumberOfTargets de 1'di: taranabilecek tek hedef
     * kimligi 0, o da baslaticinin kendisi. SCSI port mantiginda baslatici
     * kimligi bus taramasindan DISLANIR, dolayisiyla StorPort hicbir hedefi
     * taramiyor, disk PDO'su uretmiyor ve DiskPart bos cikiyordu.
     *
     * Belirti tam olarak buydu: aygit PnP'de "Started" gorunuyor, kaynaklar
     * haritalaniyor, HwInitialize basariyla donuyor, ama ortada disk yok.
     * BTL8 sozlesmesi, HwUnitControl ve BusChangeDetected eklemeleri de
     * bunu cozemezdi - hepsi hic baslamayan bir taramanin alt asamalari.
     *
     * Iki hedef ilan edip baslaticiyi 1'e almak, dislama kurali uygulansa
     * da uygulanmasa da hedef 0'i taranabilir birakiyor. Hedef 1'e giden
     * her SRB zaten B2LUfsExecuteScsi'de NO_DEVICE ile reddediliyor ve
     * HwUnitControl LUN0 disini kabul etmiyor; yani fazladan hedef UFS'e
     * tek bir komut bile gondermiyor.
     */
    ConfigInfo->MaximumNumberOfTargets = 2;
    ConfigInfo->InitiatorBusId[0] = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;

    /*
     * Bus-master DMA sozlesmesi. NeedPhysicalAddresses DriverEntry'de acik
     * ve PRDT fiziksel adreslerle kuruluyor; StorPort'a bunu ConfigInfo
     * uzerinden de bildirmek gerekiyor. ScatterGather, 32 girisli PRDT'ye
     * bolunmus istekler icin sart.
     */
    /* StorPort initializes Master/ScatterGather/queue capability to TRUE. */
    ConfigInfo->CachesData = FALSE;
    /* UFSHCI PRDT byte counts are DWORD granular. */
    ConfigInfo->AlignmentMask = FILE_LONG_ALIGNMENT;
    /*
     * StorPort otherwise maps DataBuffer only for IOCTL/WMI requests.  The
     * first LUN discovery is ordinary SCSI INQUIRY / READ CAPACITY / READ,
     * and B2LUFS must obtain a physical address for each read buffer before
     * it can place it in the UFS PRDT.  Keep the request policy read-only in
     * B2LUfsExecuteScsi; this setting merely makes those read buffers
     * addressable by the miniport.
     */
    ConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;
    /* CAP[24] reports full 64-bit UFSHCI DMA addressing. */
    ConfigInfo->Dma64BitAddresses =
        SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    /* One borrowed UTRD means StorPort must not queue more than one SRB. */
    ConfigInfo->SynchronizationModel = StorSynchronizeHalfDuplex;
    ConfigInfo->MaxNumberOfIO = 1;
    ConfigInfo->MaxIOsPerLun = 1;
    ConfigInfo->InitialLunQueueDepth = 1;
    ConfigInfo->BusResetHoldTime = 0;
    ConfigInfo->SrbExtensionSize = 0;

    Adapter->ConfiguredNumberOfBuses = ConfigInfo->NumberOfBuses;
    Adapter->ConfiguredMaximumTargets = ConfigInfo->MaximumNumberOfTargets;
    Adapter->ConfiguredInitiatorBusId = (ULONG)(UCHAR)ConfigInfo->InitiatorBusId[0];
    Adapter->ConfiguredScatterGather = ConfigInfo->ScatterGather ? 1UL : 0UL;
    Adapter->ConfiguredMaster = ConfigInfo->Master ? 1UL : 0UL;
    Adapter->ConfiguredTaggedQueuing = ConfigInfo->TaggedQueuing ? 1UL : 0UL;
    Adapter->ConfiguredAutoRequestSense =
        ConfigInfo->AutoRequestSense ? 1UL : 0UL;
    Adapter->ConfiguredMultipleRequestPerLu =
        ConfigInfo->MultipleRequestPerLu ? 1UL : 0UL;
    Adapter->ConfiguredMaxNumberOfIo = ConfigInfo->MaxNumberOfIO;
    Adapter->ConfiguredMaxIoPerLun = ConfigInfo->MaxIOsPerLun;
    Adapter->ConfiguredInitialLunQueueDepth = ConfigInfo->InitialLunQueueDepth;

    Adapter->CommonBuffer = StorPortGetUncachedExtension(
        DeviceExtension,
        ConfigInfo,
        B2LUFS_COMMON_BUFFER_BYTES);
    if (Adapter->CommonBuffer == NULL) {
        return FALSE;
    }

    Length = B2LUFS_COMMON_BUFFER_BYTES;
    Adapter->CommonPhysical = StorPortGetPhysicalAddress(
        DeviceExtension,
        NULL,
        Adapter->CommonBuffer,
        &Length);
    if (Adapter->CommonPhysical.QuadPart == 0) {
        return FALSE;
    }

    /*
     * UTRL base must be 1 KiB aligned.  The following UCD starts another
     * 1 KiB later, so its two 1 KiB UPIUs and the full PRDT table have known
     * offsets.  The common buffer is contiguous; physical and virtual
     * offsets match.
     */
    Offset = B2LUfsAlignOffset(
        (ULONGLONG)Adapter->CommonPhysical.QuadPart,
        B2LUFS_UTRD_ALIGNMENT);
    if (Offset + B2LUFS_UTRD_ALIGNMENT +
        B2LUFS_UCD_BYTES +
        B2LUFS_CAPACITY_16_BYTES + B2LUFS_REPORT_LUNS_BYTES >
        B2LUFS_COMMON_BUFFER_BYTES) {
        return FALSE;
    }

    Adapter->UtrdVirtual = (PUCHAR)Adapter->CommonBuffer + Offset;
    Adapter->UtrdPhysical.QuadPart = Adapter->CommonPhysical.QuadPart + Offset;
    Adapter->UcdVirtual = (PUCHAR)Adapter->UtrdVirtual + B2LUFS_UTRD_ALIGNMENT;
    Adapter->UcdPhysical.QuadPart =
        Adapter->UtrdPhysical.QuadPart + B2LUFS_UTRD_ALIGNMENT;
    Adapter->CapacityProbeVirtual = (PUCHAR)Adapter->UcdVirtual +
        B2LUFS_UCD_BYTES;
    Adapter->CapacityProbePhysical.QuadPart = Adapter->UcdPhysical.QuadPart +
        B2LUFS_UCD_BYTES;
    Adapter->CapacityProbeLength = B2LUFS_CAPACITY_16_BYTES;
    Adapter->LunReportVirtual = (PUCHAR)Adapter->CapacityProbeVirtual +
        B2LUFS_CAPACITY_16_BYTES;
    Adapter->LunReportPhysical.QuadPart = Adapter->CapacityProbePhysical.QuadPart +
        B2LUFS_CAPACITY_16_BYTES;
    Adapter->LunReportLength = B2LUFS_REPORT_LUNS_BYTES;

    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_ALIGNMENT);
    B2LUfsZeroMemory(
        Adapter->UcdVirtual,
        B2LUFS_UCD_BYTES);
    B2LUfsZeroMemory(
        Adapter->CapacityProbeVirtual,
        B2LUFS_CAPACITY_16_BYTES + B2LUFS_REPORT_LUNS_BYTES);
    return TRUE;
}

/*
 * StorPortGetPhysicalAddress returns the contiguous run that starts at the
 * supplied virtual address, not an entire SRB's scatter/gather map.  Build
 * the UFS PRDT one run at a time so a normal Windows 4 KiB GPT read does not
 * become an accidental "must fit in one physical page" requirement.
 *
 * The two pre-PnP probes live in StorPort's common buffer and do not have an
 * SRB.  They are already physically contiguous; all other paths require the
 * SRB mapping supplied by StorPort.
 */
static BOOLEAN
B2LUfsBuildPrdt(
    _In_ PVOID DeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ PVOID DataBuffer,
    _In_ ULONG RequestedLength,
    _Out_writes_(B2LUFS_MAX_PRDT_ENTRIES) B2LUFS_PRDT *Prdt,
    _Out_ PULONG PrdtEntryCount)
{
    STOR_PHYSICAL_ADDRESS Physical;
    ULONG PhysicalLength;
    ULONG Remaining;
    ULONG BufferOffset;
    ULONG EntryLength;
    ULONG EntryCount;
    BOOLEAN CommonBuffer;

    if (DataBuffer == NULL || RequestedLength == 0) {
        return FALSE;
    }

    CommonBuffer = FALSE;
    Physical.QuadPart = 0;
    PhysicalLength = 0;
    if (DataBuffer == Adapter->CapacityProbeVirtual) {
        if (RequestedLength > Adapter->CapacityProbeLength) {
            return FALSE;
        }
        Physical = Adapter->CapacityProbePhysical;
        PhysicalLength = Adapter->CapacityProbeLength;
        CommonBuffer = TRUE;
    } else if (DataBuffer == Adapter->LunReportVirtual) {
        if (RequestedLength > Adapter->LunReportLength) {
            return FALSE;
        }
        Physical = Adapter->LunReportPhysical;
        PhysicalLength = Adapter->LunReportLength;
        CommonBuffer = TRUE;
    } else if (Srb == NULL) {
        return FALSE;
    }

    Remaining = RequestedLength;
    BufferOffset = 0;
    EntryCount = 0;
    while (Remaining != 0) {
        if (PhysicalLength == 0) {
            PhysicalLength = Remaining;
            Physical = StorPortGetPhysicalAddress(
                DeviceExtension,
                Srb,
                (PUCHAR)DataBuffer + BufferOffset,
                &PhysicalLength);
            if (Physical.QuadPart == 0 || PhysicalLength == 0) {
                return FALSE;
            }
        }

        EntryLength = B2LUfsMinUlong(Remaining, PhysicalLength);
        EntryLength = B2LUfsMinUlong(EntryLength, B2LUFS_PRDT_MAX_BYTES);
        if (EntryLength == 0 || EntryCount == B2LUFS_MAX_PRDT_ENTRIES) {
            return FALSE;
        }

        Prdt[EntryCount].BaseAddressLow = Physical.LowPart;
        Prdt[EntryCount].BaseAddressHigh = Physical.HighPart;
        Prdt[EntryCount].DataByteCount = EntryLength - 1UL;
        ++EntryCount;
        Remaining -= EntryLength;
        BufferOffset += EntryLength;
        Physical.QuadPart += EntryLength;
        PhysicalLength -= EntryLength;

        if (CommonBuffer && Remaining != 0 && PhysicalLength == 0) {
            /* The requested probe did not fit in its own DMA allocation. */
            return FALSE;
        }
    }

    *PrdtEntryCount = EntryCount;
    return TRUE;
}

BOOLEAN
B2LUfsStartTransport(_In_ PVOID DeviceExtension, _Inout_ B2LUFS_ADAPTER *Adapter)
{
    ULONG InterruptStatus;
    ULONG Attempts;

    if (Adapter->UtrdVirtual == NULL || Adapter->UcdVirtual == NULL) {
        return FALSE;
    }

    Adapter->ControllerCapabilities = B2LUfsReadHci(
        DeviceExtension, Adapter, B2LUFS_REG_CAP);
    Adapter->ControllerStatus = B2LUfsReadHci(
        DeviceExtension, Adapter, B2LUFS_REG_HCS);
    Adapter->TransferSlotCount =
        (Adapter->ControllerCapabilities & B2LUFS_CAP_NUTRS_MASK) + 1UL;
    Adapter->TaskSlotCount =
        ((Adapter->ControllerCapabilities & B2LUFS_CAP_NUTMRS_MASK) >>
         B2LUFS_CAP_NUTMRS_SHIFT) + 1UL;

    /* Match Linux's UFSHCI readiness gate before touching a request list. */
    if ((Adapter->ControllerStatus & B2LUFS_HCS_READY_MASK) !=
        B2LUFS_HCS_READY_MASK ||
        Adapter->TransferSlotCount == 0 || Adapter->TransferSlotCount > 32UL ||
        Adapter->TaskSlotCount == 0 || Adapter->TaskSlotCount > 32UL) {
        return FALSE;
    }
    if ((Adapter->ControllerCapabilities & B2LUFS_CAP_64_ADDRESSING) == 0 &&
        (Adapter->UtrdPhysical.HighPart != 0 ||
         Adapter->UcdPhysical.HighPart != 0)) {
        return FALSE;
    }
    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_HCE) &
         B2LUFS_HCE_ENABLE) == 0) {
        /* Never enable/reset the controller from Windows if firmware did not. */
        return FALSE;
    }

    if (B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) != 0) {
        /*
         * Firmware may have used any UTR slot, not only the one B2LUFS will
         * later borrow. Stopping the list while any doorbell is set could
         * truncate a final UEFI Block I/O request across ExitBootServices.
         */
        return FALSE;
    }

    /*
     * Firmware's Boot Services UTRD memory cannot survive into Windows.
     * Stop only the transfer-list scheduler after confirming it is idle, then
     * point it at our single uncached slot.  HCE/link/PHY state is untouched.
     */
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLRSR, 0);
    for (Attempts = 0; Attempts < 1000UL; ++Attempts) {
        if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLRSR) &
             B2LUFS_UTRL_RUN_STOP) == 0) {
            break;
        }
        StorPortStallExecution(10);
    }
    if (Attempts == 1000UL) {
        return FALSE;
    }

    B2LUfsWriteHci(
        DeviceExtension, Adapter, B2LUFS_REG_UTRLBA,
        Adapter->UtrdPhysical.LowPart);
    B2LUfsWriteHci(
        DeviceExtension, Adapter, B2LUFS_REG_UTRLBAU,
        Adapter->UtrdPhysical.HighPart);

    /*
     * Match the transfer-only Exynos setup in ufs-exynos: 4 KiB PRDT
     * entries, data reorder 0xa, one SCSI nexus bit per hardware UTR slot,
     * and the platform's AXI read/write burst setting. No PHY, UIC, power
     * mode, clock or media setting is changed here.
     */
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_TXPRDT_ENTRY_SIZE, 12);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_RXPRDT_ENTRY_SIZE, 12);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_DATA_REORDER, 0xAU);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_UTRL_NEXUS_TYPE,
                   B2LUfsSlotMask(Adapter->TransferSlotCount));
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_UTMRL_NEXUS_TYPE,
                   B2LUfsSlotMask(Adapter->TaskSlotCount));
    B2LUfsWriteVs(DeviceExtension, Adapter,
                   B2LUFS_VS_AXIDMA_RWDATA_BURST_LEN, 0xFUL);

    /*
     * Exynos uses software Hibern8 management. We do not send UIC Hibern8
     * commands in this read-only miniport, so disable a firmware-inherited
     * hardware idle timer while the UTP list is borrowed.
     */
    Adapter->AutoHibern8Before = B2LUfsReadHci(
        DeviceExtension, Adapter, B2LUFS_REG_AHIT);
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_AHIT, 0);

    /* Poll completion; do not claim the shared interrupt until a later phase. */
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_IE, 0);
    InterruptStatus = B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_IS);
    if (InterruptStatus != 0) {
        B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_IS, InterruptStatus);
    }
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLRSR,
                    B2LUFS_UTRL_RUN_STOP);
    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLRSR) &
         B2LUFS_UTRL_RUN_STOP) == 0) {
        return FALSE;
    }

    Adapter->TransportReady = TRUE;
    Adapter->TraceBudget = B2LUFS_TRACE_BUDGET;
    Adapter->PnpTraceBudget = B2LUFS_PNP_TRACE_BUDGET;
    return TRUE;
}

static B2LUFS_TRANSFER_RESULT
B2LUfsSubmitReadOnlyScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(16) const UCHAR *Cdb,
    _Inout_opt_ PVOID DataBuffer,
    _In_ ULONG RequestedLength,
    _Out_ PUCHAR ScsiStatus,
    _Out_opt_ PULONG ReturnedLength)
{
    B2LUFS_UTRD *Utrd;
    B2LUFS_UPIU *CommandUpiu;
    B2LUFS_UPIU *ResponseUpiu;
    B2LUFS_PRDT *Prdt;
    ULONG InterruptStatus;
    ULONG Attempts;
    ULONG Direction;
    ULONG PrdtEntryCount;
    ULONG ResidualLength;
    UCHAR TaskTag;

    Adapter->LastUtrdOcs = B2LUFS_OCS_INVALID_COMMAND_STATUS;
    Adapter->LastInterruptStatus = 0;
    *ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
    if (ReturnedLength != NULL) {
        *ReturnedLength = 0;
    }
    if (RequestedLength > B2LUFS_MAX_TRANSFER) {
        return B2LUfsTransferBadDma;
    }

    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_SLOT_BYTES);
    B2LUfsZeroMemory(
        Adapter->UcdVirtual,
        B2LUFS_UCD_BYTES);

    Utrd = (B2LUFS_UTRD *)Adapter->UtrdVirtual;
    CommandUpiu = (B2LUFS_UPIU *)Adapter->UcdVirtual;
    ResponseUpiu = (B2LUFS_UPIU *)((PUCHAR)Adapter->UcdVirtual +
                                    B2LUFS_ALIGNED_UPIU_SIZE);
    Prdt = (B2LUFS_PRDT *)((PUCHAR)Adapter->UcdVirtual + B2LUFS_PRDT_OFFSET_BYTES);

    Utrd->CommandDescriptorBaseLow = Adapter->UcdPhysical.LowPart;
    Utrd->CommandDescriptorBaseHigh = Adapter->UcdPhysical.HighPart;
    /*
     * Exynos UFS implements PRDTO/PRDTL byte granularity (the same quirk
     * used by Linux's ufs-exynos driver), not the default UFSHCI DWORD
     * granularity.  Do not divide these values by four on this controller.
     */
    Utrd->ResponseUpiuOffset = (USHORT)B2LUFS_RESPONSE_UPIU_OFFSET_BYTES;
    Utrd->ResponseUpiuLength = (USHORT)B2LUFS_ALIGNED_UPIU_SIZE;
    Utrd->PrdtOffset = (USHORT)B2LUFS_PRDT_OFFSET_BYTES;
    Utrd->Dw2 = B2LUFS_OCS_INVALID_COMMAND_STATUS;

    Direction = B2LUFS_UTP_NO_DATA_TRANSFER;
    if (RequestedLength != 0) {
        if (!B2LUfsBuildPrdt(
                DeviceExtension,
                Srb,
                Adapter,
                DataBuffer,
                RequestedLength,
                Prdt,
                &PrdtEntryCount)) {
            return B2LUfsTransferBadDma;
        }
        /* Exynos' byte-granular PRDTL includes every 16-byte entry. */
        Utrd->PrdtLength = (USHORT)(PrdtEntryCount * B2LUFS_PRDT_ENTRY_BYTES);
        Direction = B2LUFS_UTP_DEVICE_TO_HOST;
    }

    Utrd->Dw0 = B2LUFS_UTP_CMD_TYPE_UFS_STORAGE | Direction |
                B2LUFS_UTP_SCSI_COMMAND |
                B2LUFS_UTP_REQ_DESC_INT_CMD;
    CommandUpiu->TransactionCode = B2LUFS_UPIU_TRANSACTION_COMMAND;
    CommandUpiu->Flags = (RequestedLength == 0) ? B2LUFS_UPIU_FLAG_NONE :
                                                B2LUFS_UPIU_FLAG_READ;
    CommandUpiu->Lun = 0;
    /*
     * TaggedQueuing is mandatory for StorPort.  Preserve StorPort's tag in
     * the UFS COMMAND/RESPONSE UPIU even though MaxNumberOfIO=1 keeps only
     * one UTRD active.  The two pre-PnP probes have no SRB and use tag 0.
     */
    TaskTag = (Srb != NULL) ? Srb->QueueTag : 0;
    CommandUpiu->TaskTag = TaskTag;
    /*
     * Command data moves through the PRDT, not the command UPIU data
     * segment.  Its zeroed DataSegmentLength is therefore intentional; the
     * big-endian transfer length belongs to command TSF bytes 0..3.
     */
    CommandUpiu->TaskSpecificFields[0] = (UCHAR)(RequestedLength >> 24);
    CommandUpiu->TaskSpecificFields[1] = (UCHAR)(RequestedLength >> 16);
    CommandUpiu->TaskSpecificFields[2] = (UCHAR)(RequestedLength >> 8);
    CommandUpiu->TaskSpecificFields[3] = (UCHAR)RequestedLength;
    B2LUfsCopyMemory(&CommandUpiu->TaskSpecificFields[4], Cdb, 16);

    KeMemoryBarrier();
    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) &
         B2LUFS_UTRL_SLOT_0) != 0) {
        return B2LUfsTransferBusy;
    }

    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_UTRL_NEXUS_TYPE,
                   B2LUfsSlotMask(Adapter->TransferSlotCount));
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR,
                    B2LUFS_UTRL_SLOT_0);

    for (Attempts = 0; Attempts < B2LUFS_COMMAND_TIMEOUT_US; ++Attempts) {
        InterruptStatus = B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_IS);
        Adapter->LastInterruptStatus = InterruptStatus;
        if ((InterruptStatus & B2LUFS_FATAL_INTERRUPT_MASK) != 0) {
            B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_IS, InterruptStatus);
            return B2LUfsTransferFatal;
        }

        if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) &
             B2LUFS_UTRL_SLOT_0) == 0) {
            KeMemoryBarrier();
            if (InterruptStatus != 0) {
                B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_IS,
                                InterruptStatus);
            }
            if ((Utrd->Dw2 & 0x0FUL) != B2LUFS_OCS_SUCCESS) {
                Adapter->LastUtrdOcs = Utrd->Dw2 & 0x0FUL;
                return B2LUfsTransferOcs;
            }
            Adapter->LastUtrdOcs = Utrd->Dw2 & 0x0FUL;
            if (ResponseUpiu->TransactionCode != B2LUFS_UPIU_TRANSACTION_RESPONSE) {
                return B2LUfsTransferOcs;
            }
            if (ResponseUpiu->TaskTag != TaskTag) {
                return B2LUfsTransferOcs;
            }
            *ScsiStatus = ResponseUpiu->Status;
            ResidualLength = B2LUfsReadBe32(
                &ResponseUpiu->TaskSpecificFields[0]);
            if (ResidualLength > RequestedLength) {
                return B2LUfsTransferOcs;
            }
            if (ReturnedLength != NULL) {
                *ReturnedLength = RequestedLength - ResidualLength;
            }
            return B2LUfsTransferOk;
        }
        StorPortStallExecution(1);
    }

    return B2LUfsTransferTimeout;
}

/*
 * AutoRequestSense=TRUE is a StorPort requirement, not just a capability
 * label.  UFS places fixed-format sense bytes directly in a RESPONSE UPIU:
 * the data-segment length is in header bytes 10..11, DW8 starts with the
 * big-endian sense length, and the payload follows at byte 34.  Mirror the
 * Linux UFS core's ufshcd_copy_sense_data path and mark the legacy SRB so
 * StorPort can distinguish a normal CHECK CONDITION (for example, an
 * unsupported optional INQUIRY VPD page) from a transport failure.
 */
static ULONG
B2LUfsCopyAutoSense(
    _In_ const B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    const UCHAR *Response;
    ULONG DataSegmentLength;
    ULONG SenseLength;
    ULONG CopyLength;

    if ((Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) != 0 ||
        Srb->SenseInfoBuffer == NULL || Srb->SenseInfoBufferLength == 0) {
        return 0;
    }

    Response = (const UCHAR *)Adapter->UcdVirtual +
        B2LUFS_RESPONSE_UPIU_OFFSET_BYTES;
    DataSegmentLength = B2LUfsReadBe16(Response + 10UL);
    SenseLength = B2LUfsReadBe16(
        Response + B2LUFS_RESPONSE_SENSE_LENGTH_OFFSET);
    if (DataSegmentLength == 0 || SenseLength == 0) {
        return 0;
    }

    CopyLength = SenseLength;
    if (CopyLength > DataSegmentLength) {
        CopyLength = DataSegmentLength;
    }
    if (CopyLength > B2LUFS_RESPONSE_SENSE_BYTES) {
        CopyLength = B2LUFS_RESPONSE_SENSE_BYTES;
    }
    if (CopyLength > Srb->SenseInfoBufferLength) {
        CopyLength = Srb->SenseInfoBufferLength;
    }
    if (CopyLength == 0) {
        return 0;
    }

    B2LUfsCopyMemory(
        Srb->SenseInfoBuffer,
        Response + B2LUFS_RESPONSE_SENSE_DATA_OFFSET,
        CopyLength);
    return CopyLength;
}

static BOOLEAN
B2LUfsCacheCapacity(
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ ULONGLONG LastLba,
    _In_ ULONG BlockLength)
{
    /* Zero capacity cannot describe this phone's known user LUN. */
    if (LastLba == 0 || BlockLength == 0 || BlockLength > B2LUFS_MAX_TRANSFER ||
        (BlockLength & (BlockLength - 1UL)) != 0) {
        return FALSE;
    }

    Adapter->CapacityLastLba = LastLba;
    Adapter->CapacityBlockLength = BlockLength;
    Adapter->CapacityValid = TRUE;
    return TRUE;
}

/*
 * Probe the device's own capacity exactly once with a physically contiguous
 * StorPort common buffer.  READ CAPACITY(10) is the path Windows is already
 * known to issue on this phone and covers its user-LUN size.  Only an all-FF
 * LBA result needs the 16-byte service action.  Neither command reads a user
 * LBA or partition metadata.
 */
VOID
B2LUfsProbeLun0Capacity(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    UCHAR Cdb[16];
    UCHAR ScsiStatus;
    B2LUFS_TRANSFER_RESULT Result;
    ULONGLONG LastLba;
    ULONG BlockLength;
    ULONG ReturnedLength;

    Adapter->CapacityValid = FALSE;
    Adapter->CapacityProbeResult = (ULONG)B2LUfsTransferBadDma;
    Adapter->CapacityProbeScsiStatus = 0xFFU;
    Adapter->CapacityProbeOpcode = 0;
    Adapter->CapacityProbeBytes = 0;
    Adapter->CapacityLastLba = 0;
    Adapter->CapacityBlockLength = 0;
    if (!Adapter->TransportReady || Adapter->CapacityProbeVirtual == NULL ||
        Adapter->CapacityProbeLength < B2LUFS_CAPACITY_16_BYTES) {
        return;
    }

    B2LUfsZeroMemory(Cdb, sizeof(Cdb));
    B2LUfsZeroMemory(Adapter->CapacityProbeVirtual, B2LUFS_CAPACITY_16_BYTES);
    Cdb[0] = B2LUFS_SCSI_READ_CAPACITY_10;
    Result = B2LUfsSubmitReadOnlyScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        Adapter->CapacityProbeVirtual,
        B2LUFS_CAPACITY_10_BYTES,
        &ScsiStatus,
        &ReturnedLength);
    Adapter->CapacityProbeResult = (ULONG)Result;
    Adapter->CapacityProbeScsiStatus = ScsiStatus;
    Adapter->CapacityProbeOpcode = B2LUFS_SCSI_READ_CAPACITY_10;
    Adapter->CapacityProbeBytes = ReturnedLength;
    if (Result != B2LUfsTransferOk || ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength != B2LUFS_CAPACITY_10_BYTES) {
        return;
    }

    LastLba = (ULONGLONG)B2LUfsReadBe32(
        (const UCHAR *)Adapter->CapacityProbeVirtual);
    BlockLength = B2LUfsReadBe32(
        (const UCHAR *)Adapter->CapacityProbeVirtual + 4UL);
    if (LastLba != MAXULONG) {
        (void)B2LUfsCacheCapacity(Adapter, LastLba, BlockLength);
        return;
    }

    /* Only a >2 TiB logical unit reaches this READ CAPACITY(16) fallback. */
    B2LUfsZeroMemory(Cdb, sizeof(Cdb));
    B2LUfsZeroMemory(Adapter->CapacityProbeVirtual, B2LUFS_CAPACITY_16_BYTES);
    Cdb[0] = B2LUFS_SCSI_SERVICE_ACTION_IN_16;
    Cdb[1] = B2LUFS_SCSI_SAI_READ_CAPACITY_16;
    Cdb[13] = (UCHAR)B2LUFS_CAPACITY_16_BYTES;

    Result = B2LUfsSubmitReadOnlyScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        Adapter->CapacityProbeVirtual,
        B2LUFS_CAPACITY_16_BYTES,
        &ScsiStatus,
        &ReturnedLength);
    Adapter->CapacityProbeResult = (ULONG)Result;
    Adapter->CapacityProbeScsiStatus = ScsiStatus;
    Adapter->CapacityProbeOpcode = B2LUFS_SCSI_SERVICE_ACTION_IN_16;
    Adapter->CapacityProbeBytes = ReturnedLength;
    if (Result != B2LUfsTransferOk || ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength != B2LUFS_CAPACITY_16_BYTES) {
        return;
    }

    LastLba = B2LUfsReadBe64((const UCHAR *)Adapter->CapacityProbeVirtual);
    BlockLength = B2LUfsReadBe32(
        (const UCHAR *)Adapter->CapacityProbeVirtual + 8UL);
    (void)B2LUfsCacheCapacity(Adapter, LastLba, BlockLength);
}

/*
 * REPORT LUNS is discovery metadata, not a read of any LUN's blocks.  Send
 * it once through the same LUN-0 UTP path used for READ CAPACITY and retain
 * only the returned list.  The caller publishes this snapshot to WinPE's
 * RAM-backed registry; it deliberately does not ask StorPort to enumerate
 * boot, RPMB, or any non-user LUN.
 */
VOID
B2LUfsProbeLuns(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    UCHAR Cdb[16];
    UCHAR ScsiStatus;
    B2LUFS_TRANSFER_RESULT Result;
    ULONG ListLength;
    ULONG AvailableBytes;
    ULONG ReturnedLength;

    Adapter->LunReportValid = FALSE;
    Adapter->LunReportResult = (ULONG)B2LUfsTransferBadDma;
    Adapter->LunReportScsiStatus = 0xFFU;
    Adapter->LunReportListLength = 0;
    Adapter->LunReportBytes = 0;
    Adapter->LunReportEntryCount = 0;
    if (!Adapter->TransportReady || Adapter->LunReportVirtual == NULL ||
        Adapter->LunReportLength < B2LUFS_REPORT_LUNS_HEADER_BYTES) {
        return;
    }

    B2LUfsZeroMemory(Cdb, sizeof(Cdb));
    B2LUfsZeroMemory(Adapter->LunReportVirtual, Adapter->LunReportLength);
    Cdb[0] = B2LUFS_SCSI_REPORT_LUNS;
    /* Allocation length is a big-endian 32-bit field at CDB bytes 6..9. */
    Cdb[8] = (UCHAR)(Adapter->LunReportLength >> 8);
    Cdb[9] = (UCHAR)Adapter->LunReportLength;

    Result = B2LUfsSubmitReadOnlyScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        Adapter->LunReportVirtual,
        Adapter->LunReportLength,
        &ScsiStatus,
        &ReturnedLength);
    Adapter->LunReportResult = (ULONG)Result;
    Adapter->LunReportScsiStatus = ScsiStatus;
    if (Result != B2LUfsTransferOk || ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength < B2LUFS_REPORT_LUNS_HEADER_BYTES) {
        return;
    }

    ListLength = B2LUfsReadBe32((const UCHAR *)Adapter->LunReportVirtual);
    Adapter->LunReportListLength = ListLength;
    AvailableBytes = ReturnedLength - B2LUFS_REPORT_LUNS_HEADER_BYTES;
    if (ListLength > AvailableBytes) {
        /* Preserve the returned prefix but do not claim the list is complete. */
        ListLength = AvailableBytes;
    }
    ListLength -= ListLength % B2LUFS_REPORT_LUNS_ENTRY_BYTES;
    Adapter->LunReportBytes = B2LUFS_REPORT_LUNS_HEADER_BYTES + ListLength;
    Adapter->LunReportEntryCount = ListLength / B2LUFS_REPORT_LUNS_ENTRY_BYTES;
    Adapter->LunReportValid = TRUE;
}

/*
 * If the pre-PnP probe saw a transient SCSI condition, retain the first
 * valid capacity answer that Windows obtains through its normal discovery
 * path for diagnostics. Every Windows capacity SRB still travels to UFS;
 * unlike the previous cache shortcut this matches the Linux command path.
 */
static VOID
B2LUfsCacheCapacityFromSrb(
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    const UCHAR *Input;
    ULONGLONG LastLba;
    ULONG BlockLength;

    if (Adapter->CapacityValid || Srb->DataBuffer == NULL) {
        return;
    }

    Input = (const UCHAR *)Srb->DataBuffer;
    if (Srb->Cdb[0] == B2LUFS_SCSI_READ_CAPACITY_10) {
        if (Srb->DataTransferLength < B2LUFS_CAPACITY_10_BYTES) {
            return;
        }
        LastLba = (ULONGLONG)B2LUfsReadBe32(Input);
        if (LastLba == MAXULONG) {
            return;
        }
        BlockLength = B2LUfsReadBe32(Input + 4UL);
    } else if (Srb->Cdb[0] == B2LUFS_SCSI_SERVICE_ACTION_IN_16 &&
               (Srb->Cdb[1] & 0x1FU) == B2LUFS_SCSI_SAI_READ_CAPACITY_16) {
        if (Srb->DataTransferLength < 12UL) {
            return;
        }
        LastLba = B2LUfsReadBe64(Input);
        BlockLength = B2LUfsReadBe32(Input + 8UL);
    } else {
        return;
    }

    (void)B2LUfsCacheCapacity(Adapter, LastLba, BlockLength);
}

VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_TRANSFER_RESULT Result;

    /* BuildIo normally completes PnP; retain this guard for unusual paths. */
    if (Srb->Function == SRB_FUNCTION_PNP) {
        B2LUfsHandlePnpSrb(DeviceExtension, Adapter, Srb);
        return;
    }

    if (!Adapter->TransportReady) {
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferFatal,
                            SRB_STATUS_NO_DEVICE);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_NO_DEVICE);
        return;
    }

    if (Srb->Function == SRB_FUNCTION_FLUSH ||
        Srb->Function == SRB_FUNCTION_SHUTDOWN) {
        /* The read-only transport has no dirty cache and does not issue UFS flush. */
        Srb->DataTransferLength = 0;
        Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferOk,
                            SRB_STATUS_SUCCESS);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
        Srb->DataTransferLength = 0;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferBadDma,
                            SRB_STATUS_INVALID_REQUEST);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    /* User data is LUN 0 / target 0 only; boot and RPMB LUNs are never exposed. */
    if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun != 0) {
        Srb->DataTransferLength = 0;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferBadDma,
                            SRB_STATUS_NO_DEVICE);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_NO_DEVICE);
        return;
    }

    /*
     * Inbox storufs supports START STOP UNIT and disk class can use it while
     * bringing a fixed unit online.  Phase 1 must not change UFS power/media
     * state, so acknowledge this no-data lifecycle command locally instead
     * of forwarding it to the device.  Queue serialization and later reads
     * still pass through the real UFS transport.
     */
    if (Srb->Cdb[0] == B2LUFS_SCSI_START_STOP_UNIT &&
        Srb->DataTransferLength == 0 &&
        (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) == 0) {
        Adapter->LastUtrdOcs = B2LUFS_OCS_SUCCESS;
        Adapter->LastInterruptStatus = 0;
        Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferOk,
                            SRB_STATUS_SUCCESS);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    if (!B2LUfsCdbIsExplicitlyReadOnly(Srb->Cdb) ||
        (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != 0) {
        Srb->DataTransferLength = 0;
        /*
         * Storport has no SRB_STATUS_WRITE_PROTECTED status. Reject the
         * request before it reaches the HCI with a valid SRB status; the
         * adapter therefore remains strictly read-only.
        */
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferBadDma,
                            SRB_STATUS_INVALID_REQUEST);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    Result = B2LUfsSubmitReadOnlyScsi(
        DeviceExtension,
        Adapter,
        Srb,
        Srb->Cdb,
        Srb->DataBuffer,
        Srb->DataTransferLength,
        &Srb->ScsiStatus,
        &Srb->DataTransferLength);
    switch (Result) {
    case B2LUfsTransferOk:
        if (Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD) {
            B2LUfsCacheCapacityFromSrb(Adapter, Srb);
            B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                                SRB_STATUS_SUCCESS);
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        } else {
            UCHAR SrbStatus;

            SrbStatus = (Srb->ScsiStatus == B2LUFS_SCSI_STATUS_BUSY ||
                         Srb->ScsiStatus == B2LUFS_SCSI_STATUS_TASK_SET_FULL) ?
                        SRB_STATUS_BUSY : SRB_STATUS_ERROR;
            if (B2LUfsCopyAutoSense(Adapter, Srb) != 0) {
                SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }
            B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                                SrbStatus);
            B2LUfsComplete(DeviceExtension, Srb, SrbStatus);
        }
        break;
    case B2LUfsTransferBusy:
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                            SRB_STATUS_BUSY);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_BUSY);
        break;
    case B2LUfsTransferTimeout:
        /* Leave the in-flight firmware-owned slot untouched; fail fast later. */
        Adapter->TransportReady = FALSE;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                            SRB_STATUS_TIMEOUT);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_TIMEOUT);
        break;
    default:
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                            SRB_STATUS_ERROR);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_ERROR);
        break;
    }
}
