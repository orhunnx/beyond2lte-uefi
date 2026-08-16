/*
 * B2LUFS — serialized UTP transport for the Exynos 9820 UFSHCI.
 *
 * The phone's firmware has already trained the link and used it to load
 * WinPE.  We intentionally preserve that state: no HCE reset, no UIC/DME,
 * no PHY/CAL sequence and no UFS device-management query is issued here.
 * Normal SCSI reads and writes target only the user logical unit (LUN 0).
 *
 * The driver owns one transfer-request slot. StorPort's mandatory tagged and
 * multi-request contracts remain enabled. Full-duplex StorPort serializes
 * HwStartIo with its StartIo lock, and this phase completes each command
 * synchronously before returning, so only slot 0 can be active at a time.
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
#define B2LUFS_PRDT_COMMON_CHUNK_BYTES       4096UL
/*
 * UFSHCI DBC supports 256 KiB per entry. Exynos' vendor register value 12
 * selects a 4 KiB data unit. Keep the architectural ceiling here; the common
 * DMA path below uses the narrower size proven by this inherited controller.
 */
#define B2LUFS_PRDT_MAX_BYTES          (256UL * 1024UL)
/*
 * Keep the conservative 128 KiB StorPort request limit used during bring-up.
 * The 32-entry table can therefore describe the common DMA bounce buffer in
 * the controller's proven 4 KiB data units.
 */
#define B2LUFS_MAX_TRANSFER             (128UL * 1024UL)
#define B2LUFS_RESPONSE_UPIU_OFFSET_BYTES  B2LUFS_ALIGNED_UPIU_SIZE
#define B2LUFS_PRDT_OFFSET_BYTES \
    (B2LUFS_ALIGNED_UPIU_SIZE * 2UL)
#define B2LUFS_UCD_BYTES \
    (B2LUFS_PRDT_OFFSET_BYTES + \
     (B2LUFS_MAX_PRDT_ENTRIES * B2LUFS_PRDT_ENTRY_BYTES))
#define B2LUFS_IO_BOUNCE_ALIGNMENT         4096UL
#define B2LUFS_COMMON_BUFFER_BYTES \
    ((2UL * B2LUFS_UTRD_ALIGNMENT) + B2LUFS_UCD_BYTES + \
     B2LUFS_CAPACITY_16_BYTES + B2LUFS_REPORT_LUNS_BYTES + \
     B2LUFS_IO_BOUNCE_ALIGNMENT + B2LUFS_MAX_TRANSFER)
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
/*
 * This WinPE image is a bring-up environment. Keep every miniport event until
 * the volatile StorPort Operational log fills; startnet.cmd preserves and
 * prints that channel without a count limit.
 */
#define B2LUFS_TRACE_BUDGET          0x7FFFFFFFUL
#define B2LUFS_PNP_TRACE_BUDGET      0x7FFFFFFFUL
#define B2LUFS_TRACE_SAMPLE_BYTES            64UL
#define B2LUFS_ETW_SCSI_TRACE        0x42324C02UL
#define B2LUFS_ETW_PNP_TRACE         0x42324C03UL
#define B2LUFS_ETW_CAPACITY_TRACE    0x42324C05UL

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
#define B2LUFS_UPIU_RSP_FLAG_OVERFLOW         0x40U
#define B2LUFS_UPIU_RESPONSE_SUCCESS          0x00U
#define B2LUFS_SCSI_STATUS_GOOD               0x00U
#define B2LUFS_SCSI_STATUS_CHECK_CONDITION    0x02U
#define B2LUFS_SCSI_STATUS_BUSY               0x08U
#define B2LUFS_SCSI_STATUS_TASK_SET_FULL      0x28U

/* Linux ufs.h: RESPONSE UPIU DW8 has a BE16 length then 18 sense bytes. */
#define B2LUFS_RESPONSE_SENSE_LENGTH_OFFSET     32UL
#define B2LUFS_RESPONSE_SENSE_DATA_OFFSET       34UL
#define B2LUFS_RESPONSE_SENSE_BYTES             18UL

/* SCSI commands used to resolve legacy SRBs with an unspecified direction. */
#define B2LUFS_SCSI_TEST_UNIT_READY           0x00U
#define B2LUFS_SCSI_REQUEST_SENSE              0x03U
#define B2LUFS_SCSI_READ_6                     0x08U
#define B2LUFS_SCSI_WRITE_6                    0x0AU
#define B2LUFS_SCSI_INQUIRY                    0x12U
#define B2LUFS_SCSI_MODE_SELECT_6              0x15U
#define B2LUFS_SCSI_MODE_SENSE_6               0x1AU
#define B2LUFS_SCSI_START_STOP_UNIT             0x1BU
#define B2LUFS_SCSI_READ_FORMAT_CAPACITIES     0x23U
#define B2LUFS_SCSI_READ_CAPACITY_10           0x25U
#define B2LUFS_SCSI_READ_10                    0x28U
#define B2LUFS_SCSI_WRITE_10                   0x2AU
#define B2LUFS_SCSI_WRITE_AND_VERIFY_10        0x2EU
#define B2LUFS_SCSI_VERIFY_10                  0x2FU
#define B2LUFS_SCSI_SYNCHRONIZE_CACHE_10       0x35U
#define B2LUFS_SCSI_WRITE_BUFFER               0x3BU
#define B2LUFS_SCSI_READ_BUFFER                0x3CU
#define B2LUFS_SCSI_WRITE_SAME_10              0x41U
#define B2LUFS_SCSI_UNMAP                      0x42U
#define B2LUFS_SCSI_LOG_SENSE                  0x4DU
#define B2LUFS_SCSI_MODE_SELECT_10             0x55U
#define B2LUFS_SCSI_MODE_SENSE_10              0x5AU
#define B2LUFS_SCSI_PERSISTENT_RESERVE_IN      0x5EU
#define B2LUFS_SCSI_PERSISTENT_RESERVE_OUT     0x5FU
#define B2LUFS_SCSI_READ_16                    0x88U
#define B2LUFS_SCSI_COMPARE_AND_WRITE          0x89U
#define B2LUFS_SCSI_WRITE_16                   0x8AU
#define B2LUFS_SCSI_VERIFY_16                  0x8FU
#define B2LUFS_SCSI_SYNCHRONIZE_CACHE_16       0x91U
#define B2LUFS_SCSI_WRITE_SAME_16              0x93U
#define B2LUFS_SCSI_SERVICE_ACTION_IN_16       0x9EU
#define B2LUFS_SCSI_SERVICE_ACTION_OUT_16      0x9FU
#define B2LUFS_SCSI_SAI_READ_CAPACITY_16       0x10U
#define B2LUFS_SCSI_REPORT_LUNS                0xA0U
#define B2LUFS_SCSI_READ_12                    0xA8U
#define B2LUFS_SCSI_WRITE_12                   0xAAU
#define B2LUFS_SCSI_WRITE_AND_VERIFY_12        0xAEU
#define B2LUFS_SCSI_SECURITY_PROTOCOL_IN       0xA2U
#define B2LUFS_SCSI_MAINTENANCE_IN             0xA3U
#define B2LUFS_SCSI_MAINTENANCE_OUT            0xA4U
#define B2LUFS_SCSI_SECURITY_PROTOCOL_OUT      0xB5U

/* Windows GPT acceptance limits and on-media offsets (all little-endian). */
#define B2LUFS_MBR_PARTITION_TABLE_OFFSET       446UL
#define B2LUFS_MBR_PARTITION_ENTRY_BYTES         16UL
#define B2LUFS_MBR_PARTITION_COUNT                 4UL
#define B2LUFS_MBR_PARTITION_TYPE_OFFSET           4UL
#define B2LUFS_MBR_PARTITION_START_LBA_OFFSET      8UL
#define B2LUFS_MBR_PARTITION_SECTORS_OFFSET       12UL
#define B2LUFS_MBR_PROTECTIVE_TYPE              0xEEU
#define B2LUFS_GPT_HEADER_MIN_BYTES               92UL
#define B2LUFS_GPT_REVISION_1_0            0x00010000UL
#define B2LUFS_GPT_MAX_WINDOWS_ENTRIES           128UL

/* UFSHCI offsets, all relative to the first (HCI) or second (VS) resource. */
#define B2LUFS_REG_IE                          0x0024UL
#define B2LUFS_REG_HCS                         0x0030UL
#define B2LUFS_REG_UTRLBA                      0x0050UL
#define B2LUFS_REG_UTRLBAU                     0x0054UL
#define B2LUFS_REG_UTRLDBR                     0x0058UL
#define B2LUFS_REG_UTRLCLR                     0x005CUL
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
    B2LUfsTransferBadDma,
    B2LUfsTransferShortData
} B2LUFS_TRANSFER_RESULT;

typedef enum B2LUFS_DATA_DIRECTION {
    B2LUfsDataNone = 0,
    B2LUfsDataIn,
    B2LUfsDataOut,
    B2LUfsDataInvalid
} B2LUFS_DATA_DIRECTION;

#define B2LUFS_PRDT_BUILD_NOT_ATTEMPTED      0UL
#define B2LUFS_PRDT_BUILD_SUCCESS            1UL
#define B2LUFS_PRDT_BUILD_NO_SGL             2UL
#define B2LUFS_PRDT_BUILD_TOO_MANY_ENTRIES   3UL
#define B2LUFS_PRDT_BUILD_SHORT_SGL          4UL
#define B2LUFS_PRDT_BUILD_BAD_ELEMENT        5UL

C_ASSERT(SRB_FLAGS_UNSPECIFIED_DIRECTION ==
         (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT));

static B2LUFS_DATA_DIRECTION
B2LUfsResolveDataDirection(_In_ const SCSI_REQUEST_BLOCK *Srb);

/*
 * The miniport cannot safely print to the WinPE console from HwStartIo.
 * Store the first bytes returned by a bounded number of data-in SRBs in
 * StorPort's Operational channel instead. startnet.cmd renders that channel
 * after DiskPart has caused the discovery reads. Write payloads are omitted.
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
    ULONG OriginalLength;
    ULONG ScatterGatherElements;
    ULONG PrdtBuildStatus;
    ULONG PrdtEntryCount;
    ULONG FirstSglLength;
    ULONGLONG FirstSglPhysical;
    UCHAR Sample[B2LUFS_TRACE_SAMPLE_BYTES];
} B2LUFS_SCSI_TRACE_DATA;

C_ASSERT(sizeof(B2LUFS_SCSI_TRACE_DATA) == 128);

/*
 * Legacy StorPort sends SRB_FUNCTION_PNP as SCSI_PNP_REQUEST_BLOCK, not as
 * a SCSI CDB.  Keep a separate record so normal BLS2 tracing never reads a
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

typedef struct B2LUFS_CAPACITY_TRACE_DATA {
    ULONG Magic;
    ULONG Outcome;
    ULONG SrbFlags;
    ULONG RequestedLength;
    ULONG MapStatus;
    ULONG MapSource;
    ULONG ReturnedLength;
    ULONG CompletionPhase;
    ULONG CdbAllocationLength;
    UCHAR CdbLength;
    UCHAR CdbOperation;
    UCHAR CdbServiceAction;
    UCHAR ScsiStatus;
    UCHAR ReturnedData[16];
} B2LUFS_CAPACITY_TRACE_DATA;

C_ASSERT(sizeof(B2LUFS_CAPACITY_TRACE_DATA) == 56);

#define B2LUFS_CAPACITY_PHASE_BUILD_IO       1UL

#define B2LUFS_CAPACITY_OUTCOME_NO_SNAPSHOT  1UL
#define B2LUFS_CAPACITY_OUTCOME_ZERO_LENGTH  2UL
#define B2LUFS_CAPACITY_OUTCOME_SHORT_BUFFER 3UL
#define B2LUFS_CAPACITY_OUTCOME_MAP_FAILED   4UL
#define B2LUFS_CAPACITY_OUTCOME_COMPLETED    5UL

#define B2LUFS_CAPACITY_MAP_NONE             0UL
#define B2LUFS_CAPACITY_MAP_SRB_DATA_BUFFER  1UL
#define B2LUFS_CAPACITY_MAP_SYSTEM_ADDRESS   2UL

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

static ULONG
B2LUfsReadLe32(_In_reads_bytes_(4) const UCHAR *Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static ULONGLONG
B2LUfsReadLe64(_In_reads_bytes_(8) const UCHAR *Buffer)
{
    return (ULONGLONG)B2LUfsReadLe32(Buffer) |
           ((ULONGLONG)B2LUfsReadLe32(Buffer + 4UL) << 32);
}

static VOID
B2LUfsWriteLe32(_Out_writes_bytes_(4) UCHAR *Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static ULONG
B2LUfsCrc32(_In_reads_bytes_(ByteCount) const UCHAR *Buffer,
            _In_ ULONG ByteCount)
{
    ULONG Crc = MAXULONG;
    ULONG Index;
    ULONG Bit;

    for (Index = 0; Index < ByteCount; ++Index) {
        Crc ^= Buffer[Index];
        for (Bit = 0; Bit < 8UL; ++Bit) {
            Crc = (Crc >> 1) ^
                  ((Crc & 1UL) != 0 ? 0xEDB88320UL : 0UL);
        }
    }
    return ~Crc;
}

static ULONG
B2LUfsGptHeaderCrc32(_In_reads_bytes_(HeaderSize) const UCHAR *Header,
                     _In_ ULONG HeaderSize)
{
    ULONG Crc = MAXULONG;
    ULONG Index;
    ULONG Bit;
    UCHAR Value;

    for (Index = 0; Index < HeaderSize; ++Index) {
        /* The HeaderCRC32 field itself is treated as zero. */
        Value = (Index >= 16UL && Index < 20UL) ? 0 : Header[Index];
        Crc ^= Value;
        for (Bit = 0; Bit < 8UL; ++Bit) {
            Crc = (Crc >> 1) ^
                  ((Crc & 1UL) != 0 ? 0xEDB88320UL : 0UL);
        }
    }
    return ~Crc;
}

static VOID
B2LUfsWriteBe32(_Out_writes_bytes_(4) UCHAR *Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 24);
    Buffer[1] = (UCHAR)(Value >> 16);
    Buffer[2] = (UCHAR)(Value >> 8);
    Buffer[3] = (UCHAR)Value;
}

static VOID
B2LUfsWriteBe64(_Out_writes_bytes_(8) UCHAR *Buffer, _In_ ULONGLONG Value)
{
    ULONG Index;

    for (Index = 0; Index < 8UL; ++Index) {
        Buffer[7UL - Index] = (UCHAR)Value;
        Value >>= 8;
    }
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
    PVOID SampleBuffer;
    ULONG SampleLength;
    ULONG MapStatus;
    PB2LUFS_SRB_EXTENSION SrbExtension;

    if (Adapter->TraceBudget == 0) {
        return;
    }
    Adapter->TraceBudget--;

    B2LUfsZeroMemory(&Data, sizeof(Data));
    Data.Magic = 0x32534C42UL; /* "BLS2" little-endian */
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
    SrbExtension = (PB2LUFS_SRB_EXTENSION)Srb->SrbExtension;
    if (SrbExtension != NULL &&
        SrbExtension->Signature == B2LUFS_SRB_EXTENSION_SIGNATURE) {
        Data.OriginalLength = SrbExtension->OriginalTransferLength;
        Data.ScatterGatherElements = SrbExtension->ScatterGatherElements;
    } else {
        Data.OriginalLength = Srb->DataTransferLength;
    }
    Data.PrdtBuildStatus = Adapter->LastPrdtBuildStatus;
    Data.PrdtEntryCount = Adapter->LastPrdtEntryCount;
    Data.FirstSglLength = Adapter->LastFirstSglLength;
    Data.FirstSglPhysical = Adapter->LastFirstSglPhysical;

    SampleBuffer = NULL;
    SrbExtension = (PB2LUFS_SRB_EXTENSION)Srb->SrbExtension;
    if (SrbExtension != NULL &&
        SrbExtension->Signature == B2LUFS_SRB_EXTENSION_SIGNATURE &&
        SrbExtension->SystemAddressStatus == STOR_STATUS_SUCCESS &&
        SrbExtension->SystemAddress != NULL) {
        SampleBuffer = SrbExtension->SystemAddress;
    } else if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
               Srb->DataTransferLength != 0 &&
               B2LUfsResolveDataDirection(Srb) == B2LUfsDataIn) {
        MapStatus = StorPortGetSystemAddress(
            DeviceExtension, Srb, &SampleBuffer);
        if (MapStatus != STOR_STATUS_SUCCESS || SampleBuffer == NULL) {
            SampleBuffer = Srb->DataBuffer;
        }
    }

    if (Result == B2LUfsTransferOk &&
        Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD &&
        B2LUfsResolveDataDirection(Srb) == B2LUfsDataIn &&
        SampleBuffer != NULL && Srb->DataTransferLength != 0) {
        SampleLength = Srb->DataTransferLength;
        if (SampleLength > B2LUFS_TRACE_SAMPLE_BYTES) {
            SampleLength = B2LUFS_TRACE_SAMPLE_BYTES;
        }
        B2LUfsCopyMemory(Data.Sample, SampleBuffer, SampleLength);
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
        L"B2LUFS SCSI/UFS trace; binary data is BLS2 header, SGL/PRDT metadata, then read/sense bytes",
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
     * ne UFS'e komut gonderiyor ne de bir kaynagi koruyor. PnP izleri yalnız
     * tam çift yönlü modelin StorPort tarafından kilitlenen HwStartIo yolunda
     * üretilir; eşzamansız HwBuildIo bu sayaca dokunmaz.
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

static BOOLEAN
B2LUfsIsBlockReadWrite(_In_ UCHAR Operation)
{
    switch (Operation) {
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_WRITE_6:
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_WRITE_10:
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_WRITE_12:
    case B2LUFS_SCSI_READ_16:
    case B2LUFS_SCSI_WRITE_16:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOLEAN
B2LUfsIsBlockRead(_In_ UCHAR Operation)
{
    switch (Operation) {
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_READ_16:
        return TRUE;
    default:
        return FALSE;
    }
}

static ULONG
B2LUfsCdbBlockCount(_In_reads_bytes_(16) const UCHAR *Cdb)
{
    switch (Cdb[0]) {
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_WRITE_6:
        return Cdb[4] == 0 ? 256UL : (ULONG)Cdb[4];
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_WRITE_10:
        return ((ULONG)Cdb[7] << 8) | (ULONG)Cdb[8];
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_WRITE_12:
        return B2LUfsReadBe32(&Cdb[6]);
    case B2LUFS_SCSI_READ_16:
    case B2LUFS_SCSI_WRITE_16:
        return B2LUfsReadBe32(&Cdb[10]);
    default:
        return 0;
    }
}

static ULONGLONG
B2LUfsCdbStartLba(_In_reads_bytes_(16) const UCHAR *Cdb)
{
    switch (Cdb[0]) {
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_WRITE_6:
        return ((ULONGLONG)(Cdb[1] & 0x1FU) << 16) |
               ((ULONGLONG)Cdb[2] << 8) | Cdb[3];
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_WRITE_10:
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_WRITE_12:
        return (ULONGLONG)B2LUfsReadBe32(&Cdb[2]);
    case B2LUFS_SCSI_READ_16:
    case B2LUFS_SCSI_WRITE_16:
        return B2LUfsReadBe64(&Cdb[2]);
    default:
        return (ULONGLONG)-1;
    }
}

static BOOLEAN
B2LUfsBlockTransferLengthIsValid(
    _In_ const B2LUFS_ADAPTER *Adapter,
    _In_reads_bytes_(16) const UCHAR *Cdb,
    _In_ ULONG RequestedLength)
{
    ULONGLONG ExpectedLength;

    if (!B2LUfsIsBlockReadWrite(Cdb[0]) || !Adapter->CapacityValid) {
        return TRUE;
    }
    ExpectedLength = (ULONGLONG)B2LUfsCdbBlockCount(Cdb) *
                     (ULONGLONG)Adapter->CapacityBlockLength;
    return ExpectedLength == (ULONGLONG)RequestedLength;
}

static ULONG
B2LUfsSlotMask(_In_ ULONG Count)
{
    if (Count >= 32UL) {
        return MAXULONG;
    }
    return (1UL << Count) - 1UL;
}

static B2LUFS_DATA_DIRECTION
B2LUfsDirectionFromCdb(_In_reads_bytes_(16) const UCHAR *Cdb)
{
    switch (Cdb[0]) {
    case B2LUFS_SCSI_TEST_UNIT_READY:
    case B2LUFS_SCSI_START_STOP_UNIT:
    case B2LUFS_SCSI_SYNCHRONIZE_CACHE_10:
    case B2LUFS_SCSI_SYNCHRONIZE_CACHE_16:
        return B2LUfsDataNone;
    case B2LUFS_SCSI_REQUEST_SENSE:
    case B2LUFS_SCSI_READ_6:
    case B2LUFS_SCSI_INQUIRY:
    case B2LUFS_SCSI_MODE_SENSE_6:
    case B2LUFS_SCSI_READ_FORMAT_CAPACITIES:
    case B2LUFS_SCSI_READ_CAPACITY_10:
    case B2LUFS_SCSI_READ_10:
    case B2LUFS_SCSI_READ_BUFFER:
    case B2LUFS_SCSI_LOG_SENSE:
    case B2LUFS_SCSI_MODE_SENSE_10:
    case B2LUFS_SCSI_PERSISTENT_RESERVE_IN:
    case B2LUFS_SCSI_READ_16:
    case B2LUFS_SCSI_REPORT_LUNS:
    case B2LUFS_SCSI_READ_12:
    case B2LUFS_SCSI_SECURITY_PROTOCOL_IN:
    case B2LUFS_SCSI_MAINTENANCE_IN:
        return B2LUfsDataIn;
    case B2LUFS_SCSI_WRITE_6:
    case B2LUFS_SCSI_MODE_SELECT_6:
    case B2LUFS_SCSI_WRITE_10:
    case B2LUFS_SCSI_WRITE_AND_VERIFY_10:
    case B2LUFS_SCSI_WRITE_BUFFER:
    case B2LUFS_SCSI_WRITE_SAME_10:
    case B2LUFS_SCSI_UNMAP:
    case B2LUFS_SCSI_MODE_SELECT_10:
    case B2LUFS_SCSI_PERSISTENT_RESERVE_OUT:
    case B2LUFS_SCSI_COMPARE_AND_WRITE:
    case B2LUFS_SCSI_WRITE_16:
    case B2LUFS_SCSI_WRITE_SAME_16:
    case B2LUFS_SCSI_SERVICE_ACTION_OUT_16:
    case B2LUFS_SCSI_WRITE_12:
    case B2LUFS_SCSI_WRITE_AND_VERIFY_12:
    case B2LUFS_SCSI_MAINTENANCE_OUT:
    case B2LUFS_SCSI_SECURITY_PROTOCOL_OUT:
        return B2LUfsDataOut;
    case B2LUFS_SCSI_VERIFY_10:
    case B2LUFS_SCSI_VERIFY_16:
        /* BYTCHK != 0 adds a host-to-device comparison payload. */
        return (Cdb[1] & 0x06U) != 0 ? B2LUfsDataOut : B2LUfsDataNone;
    case B2LUFS_SCSI_SERVICE_ACTION_IN_16:
        return B2LUfsDataIn;
    default:
        return B2LUfsDataInvalid;
    }
}

static B2LUFS_DATA_DIRECTION
B2LUfsResolveDataDirection(_In_ const SCSI_REQUEST_BLOCK *Srb)
{
    ULONG Direction;

    if (Srb->DataTransferLength == 0) {
        return B2LUfsDataNone;
    }

    Direction = Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION;
    if (Direction == SRB_FLAGS_DATA_IN) {
        return B2LUfsDataIn;
    }
    if (Direction == SRB_FLAGS_DATA_OUT) {
        return B2LUfsDataOut;
    }

    /*
     * SRB_FLAGS_UNSPECIFIED_DIRECTION is DATA_IN | DATA_OUT. Microsoft
     * requires the miniport to resolve it from the target command. The same
     * CDB fallback also covers a legacy request that omitted both bits.
     */
    return B2LUfsDirectionFromCdb(Srb->Cdb);
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
    /* Do not claim an enabled volatile cache before MODE SENSE confirms it. */
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
            SrbStatus = SRB_STATUS_SUCCESS;
            break;
        case StorRemoveDevice:
        case StorSurpriseRemoval:
            Adapter->AdapterRemoved = TRUE;
            Adapter->TransportReady = FALSE;
            B2LUfsReleaseBusScanTimer(DeviceExtension, Adapter);
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
     * The platform reports coherent 64-bit DMA (CAP[24]). Windows buffers
     * need not be physically contiguous; StorPort supplies the DMA SGL and
     * every element becomes one standard UFSHCI PRDT entry.
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
    /* StorPort treats this historical "breaks" field as the SG entry count. */
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
    /* StorPort owns ScatterGather/Master; current DDI forbids changing them. */
    /* The HBA has no private cache; UFS cache policy is reported by the LUN. */
    ConfigInfo->CachesData = FALSE;
    /* UFSHCI PRDT byte counts are DWORD granular. */
    ConfigInfo->AlignmentMask = FILE_LONG_ALIGNMENT;
    /*
     * Normal I/O is serialized through one common DMA bounce buffer. Ask
     * StorPort to map READ/WRITE buffers so StorPortGetSystemAddress can
     * supply the CPU-visible address used by that copy path.
     */
    ConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;
    /* CAP[24] reports full 64-bit UFSHCI DMA addressing. */
    ConfigInfo->Dma64BitAddresses =
        SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    /*
     * StorPort requires tagged/multiple-request capability, but these three
     * values describe requests actually outstanding in this phase. StartIo
     * completes synchronously and owns only slot 0, so the truthful limit is
     * one until the multi-slot interrupt transport is implemented.
     */
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->MaxNumberOfIO = 1;
    ConfigInfo->MaxIOsPerLun = 1;
    ConfigInfo->InitialLunQueueDepth = 1;
    ConfigInfo->BusResetHoldTime = 0;
    ConfigInfo->SrbExtensionSize = sizeof(B2LUFS_SRB_EXTENSION);

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
    Adapter->ConfiguredPhysicalBreaks = ConfigInfo->NumberOfPhysicalBreaks;
    Adapter->ConfiguredSynchronizationModel =
        (ULONG)ConfigInfo->SynchronizationModel;
    Adapter->ConfiguredMaxNumberOfIo = ConfigInfo->MaxNumberOfIO;
    Adapter->ConfiguredMaxIoPerLun = ConfigInfo->MaxIOsPerLun;
    Adapter->ConfiguredInitialLunQueueDepth = ConfigInfo->InitialLunQueueDepth;

    /* StorPort preserves the extension across stop/restart; allocate once. */
    if (Adapter->CommonBuffer == NULL) {
        Adapter->CommonBuffer = StorPortGetUncachedExtension(
            DeviceExtension,
            ConfigInfo,
            B2LUFS_COMMON_BUFFER_BYTES);
        if (Adapter->CommonBuffer == NULL) {
            return FALSE;
        }
    }

    Length = B2LUFS_COMMON_BUFFER_BYTES;
    Adapter->CommonPhysical = StorPortGetPhysicalAddress(
        DeviceExtension,
        NULL,
        Adapter->CommonBuffer,
        &Length);
    if (Adapter->CommonPhysical.QuadPart == 0 ||
        Length < B2LUFS_COMMON_BUFFER_BYTES) {
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
        B2LUFS_CAPACITY_16_BYTES + B2LUFS_REPORT_LUNS_BYTES +
        B2LUFS_IO_BOUNCE_ALIGNMENT + B2LUFS_MAX_TRANSFER >
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
    Offset = B2LUfsAlignOffset(
        Adapter->LunReportPhysical.QuadPart + Adapter->LunReportLength,
        B2LUFS_IO_BOUNCE_ALIGNMENT);
    Adapter->IoBounceVirtual = (PUCHAR)Adapter->LunReportVirtual +
        Adapter->LunReportLength + Offset;
    Adapter->IoBouncePhysical.QuadPart =
        Adapter->LunReportPhysical.QuadPart + Adapter->LunReportLength + Offset;
    Adapter->IoBounceLength = B2LUFS_MAX_TRANSFER;

    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_ALIGNMENT);
    B2LUfsZeroMemory(
        Adapter->UcdVirtual,
        B2LUFS_UCD_BYTES);
    B2LUfsZeroMemory(
        Adapter->CapacityProbeVirtual,
        B2LUFS_CAPACITY_16_BYTES + B2LUFS_REPORT_LUNS_BYTES);
    B2LUfsZeroMemory(Adapter->IoBounceVirtual, Adapter->IoBounceLength);
    return TRUE;
}

/*
 * Build the PRDT for one of the three StorPort common-DMA areas. Normal SRBs
 * use IoBounceVirtual; the two pre-PnP probes use their smaller dedicated
 * ranges. The SGL fallback remains diagnostic-only and is never selected by
 * the serialized normal-I/O path.
 */
static BOOLEAN
B2LUfsBuildPrdt(
    _In_ PVOID DeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_opt_ PVOID DataBuffer,
    _In_ ULONG RequestedLength,
    _Out_writes_(B2LUFS_MAX_PRDT_ENTRIES) B2LUFS_PRDT *Prdt,
    _Out_ PULONG PrdtEntryCount)
{
    STOR_PHYSICAL_ADDRESS Physical;
    ULONG PhysicalLength;
    ULONG Remaining;
    ULONG EntryLength;
    ULONG EntryCount;
    ULONG ElementIndex;
    PSTOR_SCATTER_GATHER_LIST ScatterGather;
    BOOLEAN CommonBuffer;
    PB2LUFS_SRB_EXTENSION SrbExtension;

    if (RequestedLength == 0) {
        Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_BAD_ELEMENT;
        return FALSE;
    }

    Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_NOT_ATTEMPTED;
    Adapter->LastPrdtEntryCount = 0;
    Adapter->LastFirstSglLength = 0;
    Adapter->LastFirstSglPhysical = 0;

    Physical.QuadPart = 0;
    PhysicalLength = 0;
    CommonBuffer = FALSE;
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
    } else if (DataBuffer == Adapter->IoBounceVirtual) {
        if (RequestedLength > Adapter->IoBounceLength) {
            return FALSE;
        }
        Physical = Adapter->IoBouncePhysical;
        PhysicalLength = Adapter->IoBounceLength;
        CommonBuffer = TRUE;
    }

    if (CommonBuffer) {
        if (Physical.QuadPart == 0) {
            return FALSE;
        }

        Remaining = RequestedLength;
        EntryCount = 0;
        while (Remaining != 0) {
            EntryLength = B2LUfsMinUlong(Remaining, PhysicalLength);
            /*
             * Exynos was proven on-device with one 4 KiB data unit per PRDT
             * entry.  Larger DBC values are legal UFSHCI, but the failed GPT
             * array read showed that this inherited controller path does not
             * complete a 16 KiB common-buffer entry reliably.  Multiple PRDT
             * entries preserve one SCSI transfer and remain standard UFSHCI.
             */
            EntryLength = B2LUfsMinUlong(
                EntryLength, B2LUFS_PRDT_COMMON_CHUNK_BYTES);
            EntryLength = B2LUfsMinUlong(
                EntryLength, B2LUFS_PRDT_MAX_BYTES);
            if (EntryLength == 0 ||
                (EntryLength & 3UL) != 0) {
                Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_BAD_ELEMENT;
                return FALSE;
            }
            if (EntryCount == B2LUFS_MAX_PRDT_ENTRIES) {
                Adapter->LastPrdtBuildStatus =
                    B2LUFS_PRDT_BUILD_TOO_MANY_ENTRIES;
                return FALSE;
            }
            Prdt[EntryCount].BaseAddressLow = Physical.LowPart;
            Prdt[EntryCount].BaseAddressHigh = Physical.HighPart;
            Prdt[EntryCount].DataByteCount = EntryLength - 1UL;
            ++EntryCount;
            Remaining -= EntryLength;
            Physical.QuadPart += EntryLength;
            PhysicalLength -= EntryLength;
        }
        *PrdtEntryCount = EntryCount;
        Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_SUCCESS;
        Adapter->LastPrdtEntryCount = EntryCount;
        Adapter->LastFirstSglLength = RequestedLength;
        Adapter->LastFirstSglPhysical = Physical.QuadPart - RequestedLength;
        return TRUE;
    }

    if (Srb == NULL) {
        Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_NO_SGL;
        return FALSE;
    }
    UNREFERENCED_PARAMETER(DeviceExtension);
    SrbExtension = (PB2LUFS_SRB_EXTENSION)Srb->SrbExtension;
    ScatterGather = NULL;
    if (SrbExtension != NULL &&
        SrbExtension->Signature == B2LUFS_SRB_EXTENSION_SIGNATURE) {
        ScatterGather = SrbExtension->ScatterGather;
    }
    if (ScatterGather == NULL || ScatterGather->NumberOfElements == 0) {
        Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_NO_SGL;
        return FALSE;
    }

    Adapter->LastFirstSglPhysical =
        ScatterGather->List[0].PhysicalAddress.QuadPart;
    Adapter->LastFirstSglLength = ScatterGather->List[0].Length;

    Remaining = RequestedLength;
    EntryCount = 0;
    for (ElementIndex = 0;
         ElementIndex < ScatterGather->NumberOfElements && Remaining != 0;
         ++ElementIndex) {
        Physical = ScatterGather->List[ElementIndex].PhysicalAddress;
        PhysicalLength = B2LUfsMinUlong(
            Remaining, ScatterGather->List[ElementIndex].Length);
        while (PhysicalLength != 0) {
            EntryLength = B2LUfsMinUlong(
                PhysicalLength, B2LUFS_PRDT_MAX_BYTES);
            if (EntryLength == 0 || (EntryLength & 3UL) != 0 ||
                (Physical.QuadPart & 3ULL) != 0) {
                Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_BAD_ELEMENT;
                return FALSE;
            }
            if (EntryCount == B2LUFS_MAX_PRDT_ENTRIES) {
                Adapter->LastPrdtBuildStatus =
                    B2LUFS_PRDT_BUILD_TOO_MANY_ENTRIES;
                return FALSE;
            }
            Prdt[EntryCount].BaseAddressLow = Physical.LowPart;
            Prdt[EntryCount].BaseAddressHigh = Physical.HighPart;
            Prdt[EntryCount].DataByteCount = EntryLength - 1UL;
            ++EntryCount;
            Remaining -= EntryLength;
            Physical.QuadPart += EntryLength;
            PhysicalLength -= EntryLength;
        }
    }
    if (Remaining != 0) {
        Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_SHORT_SGL;
        return FALSE;
    }
    *PrdtEntryCount = EntryCount;
    Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_SUCCESS;
    Adapter->LastPrdtEntryCount = EntryCount;
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
     * Match the transfer-only Exynos setup in ufs-exynos: 4 KiB DMA data-unit
     * size, data reorder 0xa, one SCSI nexus bit per hardware UTR slot, and
     * the platform's AXI read/write burst setting. The data-unit register is
     * not a PRDT segment-length limit. No PHY, UIC, power mode, clock or media
     * setting is changed here.
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
     * commands in this miniport, so disable a firmware-inherited
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
    Adapter->AdapterRemoved = FALSE;
    /* Dump callbacks may execute at HIGH_LEVEL; ETW is runtime-only. */
    Adapter->TraceBudget = Adapter->DumpMode ? 0 : B2LUFS_TRACE_BUDGET;
    Adapter->PnpTraceBudget = Adapter->DumpMode ? 0 : B2LUFS_PNP_TRACE_BUDGET;
    return TRUE;
}

BOOLEAN
B2LUfsStopTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    ULONG Attempts;
    ULONG Doorbell;

    Adapter->TransportReady = FALSE;
    if (Adapter->AdapterRemoved ||
        Adapter->Resources.Base[B2LUfsResourceHci] == NULL) {
        return FALSE;
    }

    /* Stop interrupt production before halting the borrowed transfer list. */
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_IE, 0);
    Doorbell = B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR);
    if (Doorbell != 0) {
        /* UFSHCI clears selected slots with zero bits; ones mean no change. */
        B2LUfsWriteHci(
            DeviceExtension, Adapter, B2LUFS_REG_UTRLCLR, ~Doorbell);
        for (Attempts = 0; Attempts < 5000UL; ++Attempts) {
            if (B2LUfsReadHci(
                    DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) == 0) {
                break;
            }
            StorPortStallExecution(1);
        }
        if (Attempts == 5000UL) {
            return FALSE;
        }
    }

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

    /* Restore firmware's idle policy while Windows is not using the HBA. */
    B2LUfsWriteHci(
        DeviceExtension, Adapter, B2LUFS_REG_AHIT,
        Adapter->AutoHibern8Before);
    return TRUE;
}

BOOLEAN
B2LUfsResetTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    if (!B2LUfsStopTransport(DeviceExtension, Adapter)) {
        return FALSE;
    }

    /*
     * Multi-tier reset recovery is intentionally limited to UTP queue state:
     * HCE, UniPro, PHY and the UFS logical units are never reset or written.
     */
    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_ALIGNMENT);
    B2LUfsZeroMemory(Adapter->UcdVirtual, B2LUFS_UCD_BYTES);
    return B2LUfsStartTransport(DeviceExtension, Adapter);
}

static B2LUFS_TRANSFER_RESULT
B2LUfsSubmitScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(16) const UCHAR *Cdb,
    _In_range_(1, 16) UCHAR CdbLength,
    _In_ B2LUFS_DATA_DIRECTION DataDirection,
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
    Adapter->LastPrdtBuildStatus = B2LUFS_PRDT_BUILD_NOT_ATTEMPTED;
    Adapter->LastPrdtEntryCount = 0;
    Adapter->LastFirstSglLength = 0;
    Adapter->LastFirstSglPhysical = 0;
    *ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
    if (ReturnedLength != NULL) {
        *ReturnedLength = 0;
    }
    if (CdbLength == 0 || CdbLength > 16 ||
        RequestedLength > B2LUFS_MAX_TRANSFER ||
        DataDirection == B2LUfsDataInvalid ||
        ((RequestedLength == 0) != (DataDirection == B2LUfsDataNone))) {
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
        Direction = (DataDirection == B2LUfsDataOut) ?
                    B2LUFS_UTP_HOST_TO_DEVICE :
                    B2LUFS_UTP_DEVICE_TO_HOST;
    }

    Utrd->Dw0 = B2LUFS_UTP_CMD_TYPE_UFS_STORAGE | Direction |
                B2LUFS_UTP_SCSI_COMMAND |
                B2LUFS_UTP_REQ_DESC_INT_CMD;
    CommandUpiu->TransactionCode = B2LUFS_UPIU_TRANSACTION_COMMAND;
    CommandUpiu->Flags = B2LUFS_UPIU_FLAG_NONE;
    if (DataDirection == B2LUfsDataIn) {
        CommandUpiu->Flags = B2LUFS_UPIU_FLAG_READ;
    } else if (DataDirection == B2LUfsDataOut) {
        CommandUpiu->Flags = B2LUFS_UPIU_FLAG_WRITE;
    }
    CommandUpiu->Lun = 0;
    /*
     * TaggedQueuing is mandatory for StorPort.  Preserve StorPort's tag in
     * the UFS COMMAND/RESPONSE UPIU. StartIo completes synchronously, so only
     * one UTRD is active. The two pre-PnP probes have no SRB and use tag 0.
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
    /* The UPIU CDB field is always 16 bytes; zeroed tail bytes stay zero. */
    B2LUfsCopyMemory(&CommandUpiu->TaskSpecificFields[4], Cdb, CdbLength);

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
            if (ResponseUpiu->Response != B2LUFS_UPIU_RESPONSE_SUCCESS) {
                return B2LUfsTransferOcs;
            }
            *ScsiStatus = ResponseUpiu->Status;
            ResidualLength = B2LUfsReadBe32(
                &ResponseUpiu->TaskSpecificFields[0]);
            if (ResidualLength > RequestedLength) {
                return B2LUfsTransferOcs;
            }
            if ((ResponseUpiu->Flags & B2LUFS_UPIU_RSP_FLAG_OVERFLOW) != 0) {
                return B2LUfsTransferOcs;
            }
            if (ReturnedLength != NULL) {
                /*
                 * Linux accepts a nonzero residual even when a device omits
                 * UNDERFLOW, but never treats OVERFLOW as a short success.
                 */
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
    Result = B2LUfsSubmitScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        10,
        B2LUfsDataIn,
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

    Result = B2LUfsSubmitScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        16,
        B2LUfsDataIn,
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

    Result = B2LUfsSubmitScsi(
        DeviceExtension,
        Adapter,
        NULL,
        Cdb,
        12,
        B2LUfsDataIn,
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

static B2LUFS_TRANSFER_RESULT
B2LUfsProbeRead10(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ ULONG StartLba,
    _In_range_(1, MAXUSHORT) USHORT BlockCount,
    _Out_ PUCHAR ScsiStatus,
    _Out_ PULONG ReturnedLength)
{
    UCHAR Cdb[16];
    ULONG RequestedLength;

    if (BlockCount == 0 || Adapter->CapacityBlockLength == 0 ||
        (ULONG)BlockCount > Adapter->IoBounceLength /
                            Adapter->CapacityBlockLength) {
        *ScsiStatus = 0xFFU;
        *ReturnedLength = 0;
        return B2LUfsTransferBadDma;
    }

    RequestedLength = (ULONG)BlockCount * Adapter->CapacityBlockLength;
    B2LUfsZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = B2LUFS_SCSI_READ_10;
    B2LUfsWriteBe32(&Cdb[2], StartLba);
    Cdb[7] = (UCHAR)(BlockCount >> 8);
    Cdb[8] = (UCHAR)BlockCount;
    B2LUfsZeroMemory(Adapter->IoBounceVirtual, RequestedLength);
    return B2LUfsSubmitScsi(
        DeviceExtension, Adapter, NULL, Cdb, 10, B2LUfsDataIn,
        Adapter->IoBounceVirtual, RequestedLength, ScsiStatus,
        ReturnedLength);
}

static VOID
B2LUfsRecoverProbeQueue(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ B2LUFS_TRANSFER_RESULT Result)
{
    /*
     * A timed-out polled command leaves UTRLDBR set.  If that diagnostic
     * request is allowed to escape into the deferred BusChange, every normal
     * StorPort SRB sees slot 0 busy and the disk PDO disappears.  Reset only
     * the driver-owned UTP list; B2LUfsResetTransport deliberately preserves
     * HCE, UniPro, PHY and all media contents.
     */
    if (Result == B2LUfsTransferTimeout ||
        Result == B2LUfsTransferBusy ||
        Result == B2LUfsTransferFatal) {
        (void)B2LUfsResetTransport(DeviceExtension, Adapter);
    }
}

static BOOLEAN
B2LUfsProtectiveMbrIsValid(
    _In_reads_bytes_(512) const UCHAR *Mbr,
    _In_ ULONGLONG LastLba)
{
    ULONG Index;
    const UCHAR *Entry;
    ULONG ExpectedSectors;

    if (Mbr[510] != 0x55U || Mbr[511] != 0xAAU) {
        return FALSE;
    }
    ExpectedSectors = LastLba > MAXULONG ? MAXULONG : (ULONG)LastLba;
    for (Index = 0; Index < B2LUFS_MBR_PARTITION_COUNT; ++Index) {
        Entry = Mbr + B2LUFS_MBR_PARTITION_TABLE_OFFSET +
                (Index * B2LUFS_MBR_PARTITION_ENTRY_BYTES);
        if (Entry[B2LUFS_MBR_PARTITION_TYPE_OFFSET] ==
                B2LUFS_MBR_PROTECTIVE_TYPE &&
            B2LUfsReadLe32(Entry + B2LUFS_MBR_PARTITION_START_LBA_OFFSET) ==
                1UL &&
            B2LUfsReadLe32(Entry + B2LUFS_MBR_PARTITION_SECTORS_OFFSET) ==
                ExpectedSectors) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
B2LUfsBuildProtectiveMbr(
    _Inout_updates_bytes_(512) UCHAR *Mbr,
    _In_ ULONGLONG LastLba)
{
    UCHAR *Entry;
    ULONG Sectors;

    B2LUfsZeroMemory(
        Mbr + B2LUFS_MBR_PARTITION_TABLE_OFFSET,
        B2LUFS_MBR_PARTITION_ENTRY_BYTES * B2LUFS_MBR_PARTITION_COUNT);
    Entry = Mbr + B2LUFS_MBR_PARTITION_TABLE_OFFSET;
    /* Conventional CHS sentinels; Windows uses the following LBA fields. */
    Entry[1] = 0x00U;
    Entry[2] = 0x02U;
    Entry[3] = 0x00U;
    Entry[B2LUFS_MBR_PARTITION_TYPE_OFFSET] = B2LUFS_MBR_PROTECTIVE_TYPE;
    Entry[5] = 0xFFU;
    Entry[6] = 0xFFU;
    Entry[7] = 0xFFU;
    B2LUfsWriteLe32(
        Entry + B2LUFS_MBR_PARTITION_START_LBA_OFFSET, 1UL);
    Sectors = LastLba > MAXULONG ? MAXULONG : (ULONG)LastLba;
    B2LUfsWriteLe32(
        Entry + B2LUFS_MBR_PARTITION_SECTORS_OFFSET, Sectors);
    Mbr[510] = 0x55U;
    Mbr[511] = 0xAAU;
}

static VOID
B2LUfsNormalizeWindowsGptRead(
    _In_ const B2LUFS_ADAPTER *Adapter,
    _In_reads_bytes_(16) const UCHAR *Cdb,
    _Inout_updates_bytes_(ReturnedLength) PVOID Buffer,
    _In_ ULONG ReturnedLength)
{
    if (!Adapter->MediaNormalizeProtectiveMbr ||
        Buffer == NULL || !B2LUfsIsBlockRead(Cdb[0]) ||
        B2LUfsCdbStartLba(Cdb) != 0 ||
        ReturnedLength < Adapter->CapacityBlockLength ||
        Adapter->CapacityBlockLength < 512UL) {
        return;
    }

    /*
     * Samsung's media remains untouched.  Only a standards-valid GPT whose
     * sole incompatibility is its PMBR is presented to partmgr with the one
     * 0xEE entry required by Windows.  Subsequent writes still reach UFS.
     */
    B2LUfsBuildProtectiveMbr((UCHAR *)Buffer, Adapter->CapacityLastLba);
}

VOID
B2LUfsProbeMediaHeaders(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter)
{
    const UCHAR *Media;
    UCHAR ScsiStatus;
    ULONG ReturnedLength;
    ULONG BlockLength;
    ULONG HeaderRevision;
    ULONG HeaderSize;
    ULONG EntryCount;
    ULONG EntrySize;
    ULONG EntryBytes;
    ULONG EntryBlocks;
    ULONGLONG EntryLba;
    ULONGLONG FirstUsableLba;
    ULONGLONG LastUsableLba;
    B2LUFS_TRANSFER_RESULT Result;

    Adapter->MediaProbeResult = (ULONG)B2LUfsTransferBadDma;
    Adapter->MediaProbeScsiStatus = 0xFFU;
    Adapter->MediaProbeBytes = 0;
    Adapter->MediaProbeValid = FALSE;
    Adapter->MediaProbeMbrSignature = FALSE;
    Adapter->MediaProbeGptSignature = FALSE;
    Adapter->MediaProbeProtectiveMbrValid = FALSE;
    Adapter->MediaProbeGptHeaderValid = FALSE;
    Adapter->MediaProbeGptEntriesValid = FALSE;
    Adapter->MediaNormalizeProtectiveMbr = FALSE;
    Adapter->MediaProbeGptHeaderSize = 0;
    Adapter->MediaProbeGptHeaderCrc = 0;
    Adapter->MediaProbeGptHeaderCrcCalculated = 0;
    Adapter->MediaProbeGptEntryCount = 0;
    Adapter->MediaProbeGptEntrySize = 0;
    Adapter->MediaProbeGptEntriesBytes = 0;
    Adapter->MediaProbeGptEntriesCrc = 0;
    Adapter->MediaProbeGptEntriesCrcCalculated = 0;
    B2LUfsZeroMemory(Adapter->MediaProbeLba0,
                     sizeof(Adapter->MediaProbeLba0));
    B2LUfsZeroMemory(Adapter->MediaProbeLba1,
                     sizeof(Adapter->MediaProbeLba1));

    if (!Adapter->CapacityValid || Adapter->IoBounceVirtual == NULL) {
        return;
    }
    BlockLength = Adapter->CapacityBlockLength;
    if (BlockLength < 512UL || BlockLength > Adapter->IoBounceLength) {
        return;
    }

    Result = B2LUfsProbeRead10(
        DeviceExtension, Adapter, 0, 1, &ScsiStatus, &ReturnedLength);
    Adapter->MediaProbeResult = (ULONG)Result;
    Adapter->MediaProbeScsiStatus = ScsiStatus;
    Adapter->MediaProbeBytes = ReturnedLength;
    if (Result != B2LUfsTransferOk ||
        ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength != BlockLength) {
        B2LUfsRecoverProbeQueue(DeviceExtension, Adapter, Result);
        return;
    }
    Media = (const UCHAR *)Adapter->IoBounceVirtual;
    B2LUfsCopyMemory(Adapter->MediaProbeLba0, Media,
                     sizeof(Adapter->MediaProbeLba0));
    Adapter->MediaProbeMbrSignature =
        Media[510] == 0x55U && Media[511] == 0xAAU;
    Adapter->MediaProbeProtectiveMbrValid =
        B2LUfsProtectiveMbrIsValid(Media, Adapter->CapacityLastLba);

    Result = B2LUfsProbeRead10(
        DeviceExtension, Adapter, 1, 1, &ScsiStatus, &ReturnedLength);
    Adapter->MediaProbeResult = (ULONG)Result;
    Adapter->MediaProbeScsiStatus = ScsiStatus;
    Adapter->MediaProbeBytes += ReturnedLength;
    if (Result != B2LUfsTransferOk ||
        ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength != BlockLength) {
        B2LUfsRecoverProbeQueue(DeviceExtension, Adapter, Result);
        return;
    }
    Media = (const UCHAR *)Adapter->IoBounceVirtual;
    B2LUfsCopyMemory(Adapter->MediaProbeLba1, Media,
                     sizeof(Adapter->MediaProbeLba1));
    Adapter->MediaProbeGptSignature =
        Media[0] == 'E' && Media[1] == 'F' && Media[2] == 'I' &&
        Media[3] == ' ' && Media[4] == 'P' && Media[5] == 'A' &&
        Media[6] == 'R' && Media[7] == 'T';
    Adapter->MediaProbeValid =
        Adapter->MediaProbeMbrSignature && Adapter->MediaProbeGptSignature;
    /*
     * A missing 55-AA marker is itself one of the PMBR defects repaired in
     * the Windows-facing read.  Validate the GPT independently once LBA 1
     * has its signature; MediaProbeValid keeps its older dual-signature
     * diagnostic meaning.
     */
    if (!Adapter->MediaProbeGptSignature) {
        return;
    }

    HeaderRevision = B2LUfsReadLe32(Media + 8UL);
    HeaderSize = B2LUfsReadLe32(Media + 12UL);
    Adapter->MediaProbeGptHeaderSize = HeaderSize;
    Adapter->MediaProbeGptHeaderCrc = B2LUfsReadLe32(Media + 16UL);
    if (HeaderRevision != B2LUFS_GPT_REVISION_1_0 ||
        HeaderSize < B2LUFS_GPT_HEADER_MIN_BYTES ||
        HeaderSize > BlockLength || B2LUfsReadLe32(Media + 20UL) != 0 ||
        B2LUfsReadLe64(Media + 24UL) != 1ULL ||
        B2LUfsReadLe64(Media + 32UL) != Adapter->CapacityLastLba) {
        return;
    }
    Adapter->MediaProbeGptHeaderCrcCalculated =
        B2LUfsGptHeaderCrc32(Media, HeaderSize);
    if (Adapter->MediaProbeGptHeaderCrcCalculated !=
        Adapter->MediaProbeGptHeaderCrc) {
        return;
    }

    FirstUsableLba = B2LUfsReadLe64(Media + 40UL);
    LastUsableLba = B2LUfsReadLe64(Media + 48UL);
    EntryLba = B2LUfsReadLe64(Media + 72UL);
    EntryCount = B2LUfsReadLe32(Media + 80UL);
    EntrySize = B2LUfsReadLe32(Media + 84UL);
    Adapter->MediaProbeGptEntryCount = EntryCount;
    Adapter->MediaProbeGptEntrySize = EntrySize;
    Adapter->MediaProbeGptEntriesCrc = B2LUfsReadLe32(Media + 88UL);
    if (FirstUsableLba > LastUsableLba ||
        LastUsableLba >= Adapter->CapacityLastLba ||
        EntryLba < 2ULL || EntryLba > MAXULONG ||
        EntryCount == 0 || EntryCount > B2LUFS_GPT_MAX_WINDOWS_ENTRIES ||
        EntrySize < 128UL || (EntrySize & 7UL) != 0 ||
        EntryCount > MAXULONG / EntrySize) {
        return;
    }
    EntryBytes = EntryCount * EntrySize;
    if (EntryBytes > Adapter->IoBounceLength ||
        EntryBytes > MAXULONG - (BlockLength - 1UL)) {
        return;
    }
    EntryBlocks = (EntryBytes + BlockLength - 1UL) / BlockLength;
    if (EntryBlocks == 0 || EntryBlocks > MAXUSHORT ||
        EntryLba + EntryBlocks > FirstUsableLba) {
        return;
    }
    Adapter->MediaProbeGptHeaderValid = TRUE;

    Result = B2LUfsProbeRead10(
        DeviceExtension, Adapter, (ULONG)EntryLba, (USHORT)EntryBlocks,
        &ScsiStatus, &ReturnedLength);
    Adapter->MediaProbeResult = (ULONG)Result;
    Adapter->MediaProbeScsiStatus = ScsiStatus;
    Adapter->MediaProbeGptEntriesBytes = ReturnedLength;
    if (Result != B2LUfsTransferOk ||
        ScsiStatus != B2LUFS_SCSI_STATUS_GOOD ||
        ReturnedLength != EntryBlocks * BlockLength) {
        B2LUfsRecoverProbeQueue(DeviceExtension, Adapter, Result);
        return;
    }
    Adapter->MediaProbeGptEntriesCrcCalculated = B2LUfsCrc32(
        (const UCHAR *)Adapter->IoBounceVirtual, EntryBytes);
    Adapter->MediaProbeGptEntriesValid =
        Adapter->MediaProbeGptEntriesCrcCalculated ==
        Adapter->MediaProbeGptEntriesCrc;
    Adapter->MediaNormalizeProtectiveMbr =
        Adapter->MediaProbeGptEntriesValid &&
        !Adapter->MediaProbeProtectiveMbrValid;
}

/*
 * If the pre-PnP probe saw a transient SCSI condition, retain the first
 * valid capacity answer that Windows obtains through its normal discovery
 * path for diagnostics and later class-driver replies.
 */
static VOID
B2LUfsCacheCapacityFromSrb(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVOID SystemAddress;
    const UCHAR *Input;
    ULONGLONG LastLba;
    ULONG BlockLength;
    ULONG MapStatus;

    if (Adapter->CapacityValid) {
        return;
    }

    SystemAddress = NULL;
    MapStatus = StorPortGetSystemAddress(
        DeviceExtension, Srb, &SystemAddress);
    if (MapStatus != STOR_STATUS_SUCCESS || SystemAddress == NULL) {
        SystemAddress = Srb->DataBuffer;
        if (SystemAddress == NULL) {
            return;
        }
    }
    Input = (const UCHAR *)SystemAddress;
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

/*
 * The pre-enumeration probe has already obtained and validated the real UFS
 * LUN-0 capacity through a StorPort common DMA buffer. Complete subsequent
 * disk-class READ CAPACITY requests from that immutable snapshot.
 *
 * Microsoft's StorAHCI sample generates fixed-media capacity data in
 * HwBuildIo and completes the SRB there. Do the same: this is a software
 * response, not a DMA request that should be queued to HwStartIo. Resolve its
 * CPU address with StorPortGetSystemAddress just like normal program-
 * controlled I/O; the MapBuffers-provided DataBuffer is only a fallback.
 */
BOOLEAN
B2LUfsTryCompleteCachedCapacity(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_CAPACITY_TRACE_DATA Trace;
    PVOID SystemAddress;
    UCHAR *Output;
    ULONG ResponseLength;
    ULONG TransferLength;
    ULONG Status;

    B2LUfsZeroMemory(&Trace, sizeof(Trace));
    Trace.Magic = 0x33434C42UL; /* "BLC3" little-endian */
    Trace.SrbFlags = Srb->SrbFlags;
    Trace.RequestedLength = Srb->DataTransferLength;
    Trace.MapStatus = MAXULONG;
    Trace.MapSource = B2LUFS_CAPACITY_MAP_NONE;
    Trace.CompletionPhase = B2LUFS_CAPACITY_PHASE_BUILD_IO;
    Trace.CdbLength = Srb->CdbLength;
    Trace.CdbOperation = Srb->Cdb[0];
    Trace.CdbServiceAction = (UCHAR)(Srb->Cdb[1] & 0x1FU);
    if (Srb->CdbLength >= 14 &&
        Srb->Cdb[0] == B2LUFS_SCSI_SERVICE_ACTION_IN_16) {
        Trace.CdbAllocationLength = B2LUfsReadBe32(&Srb->Cdb[10]);
    }

    if (Srb->Cdb[0] != B2LUFS_SCSI_READ_CAPACITY_10 &&
        !(Srb->Cdb[0] == B2LUFS_SCSI_SERVICE_ACTION_IN_16 &&
          (Srb->Cdb[1] & 0x1FU) ==
              B2LUFS_SCSI_SAI_READ_CAPACITY_16)) {
        return FALSE;
    }

    if (!Adapter->CapacityValid) {
        Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_NO_SNAPSHOT;
        goto LogAndFallback;
    }
    if (Srb->DataTransferLength == 0) {
        Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_ZERO_LENGTH;
        goto LogAndFallback;
    }

    if (Srb->Cdb[0] == B2LUFS_SCSI_READ_CAPACITY_10) {
        ResponseLength = B2LUFS_CAPACITY_10_BYTES;
        if (Srb->DataTransferLength < ResponseLength) {
            Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_SHORT_BUFFER;
            goto LogAndFallback;
        }
    } else {
        ResponseLength = B2LUFS_CAPACITY_16_BYTES;
        if (Srb->DataTransferLength < 12UL) {
            Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_SHORT_BUFFER;
            goto LogAndFallback;
        }
    }

    SystemAddress = NULL;
    Status = StorPortGetSystemAddress(
        DeviceExtension, Srb, &SystemAddress);
    Trace.MapStatus = Status;
    Trace.MapSource = B2LUFS_CAPACITY_MAP_SYSTEM_ADDRESS;
    if (Status != STOR_STATUS_SUCCESS || SystemAddress == NULL) {
        SystemAddress = Srb->DataBuffer;
        if (SystemAddress == NULL) {
            Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_MAP_FAILED;
            goto LogAndFallback;
        }
        Trace.MapStatus = STOR_STATUS_SUCCESS;
        Trace.MapSource = B2LUFS_CAPACITY_MAP_SRB_DATA_BUFFER;
    }

    TransferLength = B2LUfsMinUlong(
        Srb->DataTransferLength, ResponseLength);
    Output = (UCHAR *)SystemAddress;
    B2LUfsZeroMemory(Output, TransferLength);
    if (Srb->Cdb[0] == B2LUFS_SCSI_READ_CAPACITY_10) {
        ULONG LastLba10 = Adapter->CapacityLastLba > MAXULONG ?
                          MAXULONG : (ULONG)Adapter->CapacityLastLba;

        B2LUfsWriteBe32(Output, LastLba10);
        B2LUfsWriteBe32(Output + 4UL, Adapter->CapacityBlockLength);
    } else {
        B2LUfsWriteBe64(Output, Adapter->CapacityLastLba);
        B2LUfsWriteBe32(Output + 8UL, Adapter->CapacityBlockLength);
    }

    Adapter->LastUtrdOcs = B2LUFS_OCS_SUCCESS;
    Adapter->LastInterruptStatus = 0;
    Srb->DataTransferLength = TransferLength;
    Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
    KeMemoryBarrier();
    Trace.Outcome = B2LUFS_CAPACITY_OUTCOME_COMPLETED;
    Trace.ReturnedLength = TransferLength;
    Trace.ScsiStatus = Srb->ScsiStatus;
    B2LUfsCopyMemory(
        Trace.ReturnedData,
        Output,
        B2LUfsMinUlong(
            TransferLength, (ULONG)sizeof(Trace.ReturnedData)));
    if (!Adapter->DumpMode) {
        (void)StorPortEtwLogError(
            DeviceExtension,
            NULL,
            B2LUFS_ETW_CAPACITY_TRACE,
            L"B2LUFS cached UFS capacity completed in HwBuildIo; binary data is BLC3",
            sizeof(Trace),
            &Trace);
    }
    /* BLC3 already contains the complete software response. Do not touch the
     * StartIo-only BLS2 sequence/budget from unsynchronized HwBuildIo. */
    B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
    return TRUE;

LogAndFallback:
    if (!Adapter->DumpMode) {
        (void)StorPortEtwLogError(
            DeviceExtension,
            NULL,
            B2LUFS_ETW_CAPACITY_TRACE,
            L"B2LUFS cached UFS capacity fallback in HwBuildIo; binary data is BLC3",
            sizeof(Trace),
            &Trace);
    }
    return FALSE;
}

VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_TRANSFER_RESULT Result;
    B2LUFS_DATA_DIRECTION DataDirection;
    PB2LUFS_SRB_EXTENSION SrbExtension;
    PVOID SystemAddress;
    PVOID TransportBuffer;
    ULONG RequestedLength;
    ULONG ReturnedLength;
    ULONG MapStatus;
    BOOLEAN ResetOk;

    /* Lifecycle requests are serialized here after unsynchronized BuildIo. */
    if (Srb->Function == SRB_FUNCTION_PNP) {
        B2LUfsHandlePnpSrb(DeviceExtension, Adapter, Srb);
        return;
    }

    if (Srb->Function == SRB_FUNCTION_POWER) {
        PSCSI_POWER_REQUEST_BLOCK Power = (PSCSI_POWER_REQUEST_BLOCK)Srb;
        BOOLEAN AdapterRequest;
        BOOLEAN ValidRequest;

        if (Power->Length < (USHORT)sizeof(*Power)) {
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
            return;
        }
        AdapterRequest =
            (Power->SrbPowerFlags & SRB_POWER_FLAGS_ADAPTER_REQUEST) != 0;
        ValidRequest = AdapterRequest ||
            (Power->PathId == 0 && Power->TargetId == 0 && Power->Lun == 0);
        /*
         * StorPort follows a down notification with ScsiStopAdapter and a
         * power-up notification with ScsiRestartAdapter. Those serialized
         * callbacks own the actual UTRL stop/start operation.
         */
        B2LUfsComplete(
            DeviceExtension,
            Srb,
            ValidRequest ? SRB_STATUS_SUCCESS : SRB_STATUS_NO_DEVICE);
        return;
    }

    if (Srb->Function == SRB_FUNCTION_RESET_BUS ||
        Srb->Function == SRB_FUNCTION_RESET_DEVICE ||
        Srb->Function == SRB_FUNCTION_RESET_LOGICAL_UNIT) {
        ResetOk = Srb->PathId == 0;
        if (Srb->Function != SRB_FUNCTION_RESET_BUS) {
            ResetOk = ResetOk && Srb->TargetId == 0;
        }
        if (Srb->Function == SRB_FUNCTION_RESET_LOGICAL_UNIT) {
            ResetOk = ResetOk && Srb->Lun == 0;
        }
        if (ResetOk && !Adapter->DumpMode) {
            ResetOk = B2LUfsResetTransport(DeviceExtension, Adapter);
        }
        Srb->DataTransferLength = 0;
        B2LUfsComplete(
            DeviceExtension,
            Srb,
            ResetOk ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR);
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
        /* The HBA has no private cache; disk class sends target SYNCHRONIZE CACHE. */
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

    if (Srb->CdbLength == 0 ||
        Srb->CdbLength > (UCHAR)sizeof(Srb->Cdb)) {
        Srb->DataTransferLength = 0;
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    /*
     * Inbox storufs supports START STOP UNIT and disk class can use it while
     * bringing a fixed unit online.  Do not change UFS power/media
     * state, so acknowledge this no-data lifecycle command locally instead
     * of forwarding it to the device. Queue serialization and later I/O
     * still pass through the real UFS transport.
     */
    if (Srb->Cdb[0] == B2LUFS_SCSI_START_STOP_UNIT &&
        Srb->DataTransferLength == 0) {
        Adapter->LastUtrdOcs = B2LUFS_OCS_SUCCESS;
        Adapter->LastInterruptStatus = 0;
        Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferOk,
                            SRB_STATUS_SUCCESS);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    DataDirection = B2LUfsResolveDataDirection(Srb);
    if (DataDirection == B2LUfsDataInvalid) {
        Srb->DataTransferLength = 0;
        /*
         * An unspecified legacy SRB needs a known CDB data phase. Explicit
         * DATA_IN/DATA_OUT requests are otherwise passed through unchanged.
         */
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, B2LUfsTransferBadDma,
                            SRB_STATUS_INVALID_REQUEST);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    /* SrbFlags is read-only for this bus-master HBA; use the resolved value
     * only in our UTRD/UPIU direction fields. Microsoft permits rewriting
     * an unspecified direction solely for subordinate-DMA adapters before
     * ScsiPortIoMapTransfer, neither of which applies to this StorPort HBA.
     */

    RequestedLength = Srb->DataTransferLength;
    ReturnedLength = 0;
    SystemAddress = NULL;
    TransportBuffer = NULL;
    SrbExtension = (PB2LUFS_SRB_EXTENSION)Srb->SrbExtension;
    MapStatus = STOR_STATUS_INVALID_PARAMETER;
    if (!B2LUfsBlockTransferLengthIsValid(
            Adapter, Srb->Cdb, RequestedLength)) {
        Srb->DataTransferLength = 0;
        Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb,
                            B2LUfsTransferShortData,
                            SRB_STATUS_INVALID_REQUEST);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }
    if (RequestedLength != 0) {
        if (SrbExtension != NULL &&
            SrbExtension->Signature == B2LUFS_SRB_EXTENSION_SIGNATURE &&
            SrbExtension->SystemAddressStatus == STOR_STATUS_SUCCESS &&
            SrbExtension->SystemAddress != NULL) {
            SystemAddress = SrbExtension->SystemAddress;
            MapStatus = STOR_STATUS_SUCCESS;
        } else {
            MapStatus = StorPortGetSystemAddress(
                DeviceExtension, Srb, &SystemAddress);
            if ((MapStatus != STOR_STATUS_SUCCESS ||
                 SystemAddress == NULL) &&
                Srb->DataBuffer != NULL) {
                SystemAddress = Srb->DataBuffer;
                MapStatus = STOR_STATUS_SUCCESS;
            }
        }

        if (MapStatus != STOR_STATUS_SUCCESS || SystemAddress == NULL ||
            Adapter->IoBounceVirtual == NULL ||
            RequestedLength > Adapter->IoBounceLength) {
            Srb->DataTransferLength = 0;
            B2LUfsLogScsiTrace(
                DeviceExtension,
                Adapter,
                Srb,
                B2LUfsTransferBadDma,
                SRB_STATUS_ERROR);
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_ERROR);
            return;
        }

        TransportBuffer = Adapter->IoBounceVirtual;
        if (DataDirection == B2LUfsDataOut) {
            B2LUfsCopyMemory(
                TransportBuffer, SystemAddress, RequestedLength);
            KeMemoryBarrier();
        } else {
            /* Prevent stale bytes from masquerading as a short device read. */
            B2LUfsZeroMemory(TransportBuffer, RequestedLength);
        }
    }

    Result = B2LUfsSubmitScsi(
        DeviceExtension,
        Adapter,
        Srb,
        Srb->Cdb,
        Srb->CdbLength,
        DataDirection,
        TransportBuffer,
        RequestedLength,
        &Srb->ScsiStatus,
        &ReturnedLength);
    Srb->DataTransferLength = ReturnedLength;
    if (Result == B2LUfsTransferOk &&
        Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD &&
        B2LUfsIsBlockReadWrite(Srb->Cdb[0]) &&
        ReturnedLength != RequestedLength) {
        Result = B2LUfsTransferShortData;
    }
    if (Result == B2LUfsTransferOk &&
        Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD &&
        DataDirection == B2LUfsDataIn && ReturnedLength != 0) {
        KeMemoryBarrier();
        B2LUfsNormalizeWindowsGptRead(
            Adapter, Srb->Cdb, TransportBuffer, ReturnedLength);
        B2LUfsCopyMemory(SystemAddress, TransportBuffer, ReturnedLength);
        KeMemoryBarrier();
    }
    switch (Result) {
    case B2LUfsTransferOk:
        if (Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD) {
            B2LUfsCacheCapacityFromSrb(DeviceExtension, Adapter, Srb);
            B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                                SRB_STATUS_SUCCESS);
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        } else {
            UCHAR SrbStatus;

            SrbStatus = (Srb->ScsiStatus == B2LUFS_SCSI_STATUS_BUSY ||
                         Srb->ScsiStatus == B2LUFS_SCSI_STATUS_TASK_SET_FULL) ?
                        SRB_STATUS_BUSY : SRB_STATUS_ERROR;
            if (SrbStatus == SRB_STATUS_BUSY) {
                /* Microsoft requires the original length on BUSY/retry. */
                Srb->DataTransferLength = RequestedLength;
            }
            if (B2LUfsCopyAutoSense(Adapter, Srb) != 0) {
                SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }
            B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                                SrbStatus);
            B2LUfsComplete(DeviceExtension, Srb, SrbStatus);
        }
        break;
    case B2LUfsTransferBusy:
        /* StorPort rebuilds the SRB extension; preserve the request length. */
        Srb->DataTransferLength = RequestedLength;
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                            SRB_STATUS_BUSY);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_BUSY);
        break;
    case B2LUfsTransferShortData:
        B2LUfsLogScsiTrace(DeviceExtension, Adapter, Srb, Result,
                            SRB_STATUS_DATA_OVERRUN);
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_DATA_OVERRUN);
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
