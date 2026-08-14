/*
 * B2LUFS — minimal, read-only UTP transport for the Exynos 9820 UFSHCI.
 *
 * The phone's firmware has already trained the link and used it to load
 * WinPE.  We intentionally preserve that state: no HCE reset, no UIC/DME,
 * no PHY/CAL sequence, and no UFS query/write command is issued here.
 *
 * The driver owns one transfer-request slot.  StorPort serializes StartIo
 * for this physical, non-tagged miniport, so one slot is enough to make the
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
#define B2LUFS_MAX_TRANSFER                4096UL
#define B2LUFS_COMMON_BUFFER_BYTES         6144UL
#define B2LUFS_COMMAND_TIMEOUT_US       2000000UL

#define B2LUFS_HCE_ENABLE                    0x00000001UL
#define B2LUFS_UTRL_RUN_STOP                 0x00000001UL
#define B2LUFS_UTRL_SLOT_0                   0x00000001UL
#define B2LUFS_UTP_TRANSFER_REQ_COMPL        0x00000001UL
#define B2LUFS_UIC_LINK_LOST                 0x00000080UL
#define B2LUFS_DEVICE_FATAL_ERROR             0x00000800UL
#define B2LUFS_CONTROLLER_FATAL_ERROR         0x00010000UL
#define B2LUFS_SYSTEM_BUS_FATAL_ERROR         0x00020000UL
#define B2LUFS_FATAL_INTERRUPT_MASK \
    (B2LUFS_UIC_LINK_LOST | B2LUFS_DEVICE_FATAL_ERROR | \
     B2LUFS_CONTROLLER_FATAL_ERROR | B2LUFS_SYSTEM_BUS_FATAL_ERROR)

#define B2LUFS_UTP_NO_DATA_TRANSFER          0x00000000UL
#define B2LUFS_UTP_HOST_TO_DEVICE             0x02000000UL
#define B2LUFS_UTP_DEVICE_TO_HOST             0x04000000UL
#define B2LUFS_UTP_SCSI_COMMAND               0x00000000UL
#define B2LUFS_UTP_REQ_DESC_INT_CMD           0x01000000UL
#define B2LUFS_OCS_SUCCESS                    0x00000000UL
#define B2LUFS_OCS_INVALID_COMMAND_STATUS     0x0000000FUL

#define B2LUFS_UPIU_TRANSACTION_COMMAND       0x01U
#define B2LUFS_UPIU_TRANSACTION_RESPONSE      0x21U
#define B2LUFS_UPIU_FLAG_NONE                 0x00U
#define B2LUFS_UPIU_FLAG_WRITE                0x20U
#define B2LUFS_UPIU_FLAG_READ                 0x40U
#define B2LUFS_SCSI_STATUS_GOOD               0x00U

/* Exact read-only SCSI allow-list needed by the Windows disk/GPT stack. */
#define B2LUFS_SCSI_TEST_UNIT_READY           0x00U
#define B2LUFS_SCSI_REQUEST_SENSE              0x03U
#define B2LUFS_SCSI_READ_6                     0x08U
#define B2LUFS_SCSI_INQUIRY                    0x12U
#define B2LUFS_SCSI_MODE_SENSE_6               0x1AU
#define B2LUFS_SCSI_READ_FORMAT_CAPACITIES     0x23U
#define B2LUFS_SCSI_READ_CAPACITY_10           0x25U
#define B2LUFS_SCSI_READ_10                    0x28U
#define B2LUFS_SCSI_READ_BUFFER                0x3CU
#define B2LUFS_SCSI_MODE_SENSE_10              0x5AU
#define B2LUFS_SCSI_READ_16                    0x88U
#define B2LUFS_SCSI_SERVICE_ACTION_IN_16       0x9EU
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
#define B2LUFS_VS_TXPRDT_ENTRY_SIZE            0x0000UL
#define B2LUFS_VS_RXPRDT_ENTRY_SIZE            0x0004UL
#define B2LUFS_VS_UTRL_NEXUS_TYPE              0x0040UL
#define B2LUFS_VS_DATA_REORDER                 0x0060UL

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

typedef enum B2LUFS_TRANSFER_RESULT {
    B2LUfsTransferOk = 0,
    B2LUfsTransferBusy,
    B2LUfsTransferTimeout,
    B2LUfsTransferFatal,
    B2LUfsTransferOcs,
    B2LUfsTransferBadDma
} B2LUFS_TRANSFER_RESULT;

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

static BOOLEAN
B2LUfsCdbIsExplicitlyReadOnly(_In_ UCHAR Operation)
{
    switch (Operation) {
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
    case B2LUFS_SCSI_SERVICE_ACTION_IN_16:
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

BOOLEAN
B2LUfsAllocateTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    ULONG Length;
    ULONG Offset;

    /*
     * The platform reports coherent 64-bit DMA (CAP[24]).  Keep one 4 KiB
     * data PRDT entry only: it matches the phone's 4 KiB logical block size
     * and makes the first GPT reads maximally conservative.
     */
    ConfigInfo->MaximumTransferLength = B2LUFS_MAX_TRANSFER;
    ConfigInfo->NumberOfPhysicalBreaks = 1;
    ConfigInfo->NumberOfBuses = 1;
    /*
     * StorPort otherwise maps DataBuffer only for IOCTL/WMI requests.  The
     * first LUN discovery is ordinary SCSI INQUIRY / READ CAPACITY / READ,
     * and B2LUFS must obtain a physical address for each read buffer before
     * it can place it in the UFS PRDT.  Keep the request policy read-only in
     * B2LUfsExecuteScsi; this setting merely makes those read buffers
     * addressable by the miniport.
     */
    ConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Dma32BitAddresses = TRUE;
    ConfigInfo->Dma64BitAddresses = TRUE;
    ConfigInfo->AutoRequestSense = TRUE;
    ConfigInfo->SrbExtensionSize = 0;

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
     * 1 KiB later, so its two 1 KiB UPIUs and first PRDT have known offsets.
     * The common buffer is contiguous; physical and virtual offsets match.
     */
    Offset = B2LUfsAlignOffset(
        (ULONGLONG)Adapter->CommonPhysical.QuadPart,
        B2LUFS_UTRD_ALIGNMENT);
    if (Offset + B2LUFS_UTRD_ALIGNMENT +
        (B2LUFS_ALIGNED_UPIU_SIZE * 2UL) + B2LUFS_PRDT_ENTRY_BYTES >
        B2LUFS_COMMON_BUFFER_BYTES) {
        return FALSE;
    }

    Adapter->UtrdVirtual = (PUCHAR)Adapter->CommonBuffer + Offset;
    Adapter->UtrdPhysical.QuadPart = Adapter->CommonPhysical.QuadPart + Offset;
    Adapter->UcdVirtual = (PUCHAR)Adapter->UtrdVirtual + B2LUFS_UTRD_ALIGNMENT;
    Adapter->UcdPhysical.QuadPart =
        Adapter->UtrdPhysical.QuadPart + B2LUFS_UTRD_ALIGNMENT;

    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_ALIGNMENT);
    B2LUfsZeroMemory(
        Adapter->UcdVirtual,
        (B2LUFS_ALIGNED_UPIU_SIZE * 2UL) + B2LUFS_PRDT_ENTRY_BYTES);
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
    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_HCE) &
         B2LUFS_HCE_ENABLE) == 0) {
        /* Never enable/reset the controller from Windows if firmware did not. */
        return FALSE;
    }

    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) &
         B2LUFS_UTRL_SLOT_0) != 0) {
        /* A prior owner still has a command in flight; do not steal its ring. */
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
        StorPortStallExecution(1);
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

    /* Exynos UFS v3 uses 4 KiB PRDT chunks; these are controller settings only. */
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_TXPRDT_ENTRY_SIZE, 12);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_RXPRDT_ENTRY_SIZE, 12);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_DATA_REORDER, 0xAU);
    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_UTRL_NEXUS_TYPE, MAXULONG);

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
    return TRUE;
}

static B2LUFS_TRANSFER_RESULT
B2LUfsSubmitReadOnlyScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_UTRD *Utrd;
    B2LUFS_UPIU *CommandUpiu;
    B2LUFS_UPIU *ResponseUpiu;
    B2LUFS_PRDT *Prdt;
    STOR_PHYSICAL_ADDRESS DataPhysical;
    ULONG DataPhysicalLength;
    ULONG RequestedLength;
    ULONG InterruptStatus;
    ULONG Attempts;
    ULONG Direction;

    RequestedLength = Srb->DataTransferLength;
    if (RequestedLength > B2LUFS_MAX_TRANSFER) {
        return B2LUfsTransferBadDma;
    }

    B2LUfsZeroMemory(Adapter->UtrdVirtual, B2LUFS_UTRD_SLOT_BYTES);
    B2LUfsZeroMemory(
        Adapter->UcdVirtual,
        (B2LUFS_ALIGNED_UPIU_SIZE * 2UL) + B2LUFS_PRDT_ENTRY_BYTES);

    Utrd = (B2LUFS_UTRD *)Adapter->UtrdVirtual;
    CommandUpiu = (B2LUFS_UPIU *)Adapter->UcdVirtual;
    ResponseUpiu = (B2LUFS_UPIU *)((PUCHAR)Adapter->UcdVirtual +
                                    B2LUFS_ALIGNED_UPIU_SIZE);
    Prdt = (B2LUFS_PRDT *)((PUCHAR)Adapter->UcdVirtual +
                           (B2LUFS_ALIGNED_UPIU_SIZE * 2UL));

    Utrd->CommandDescriptorBaseLow = Adapter->UcdPhysical.LowPart;
    Utrd->CommandDescriptorBaseHigh = Adapter->UcdPhysical.HighPart;
    Utrd->ResponseUpiuOffset = (USHORT)B2LUFS_ALIGNED_UPIU_SIZE;
    Utrd->ResponseUpiuLength = (USHORT)B2LUFS_ALIGNED_UPIU_SIZE;
    Utrd->Dw2 = B2LUFS_OCS_INVALID_COMMAND_STATUS;

    Direction = B2LUFS_UTP_NO_DATA_TRANSFER;
    if (RequestedLength != 0) {
        if (Srb->DataBuffer == NULL) {
            return B2LUfsTransferBadDma;
        }

        DataPhysicalLength = RequestedLength;
        DataPhysical = StorPortGetPhysicalAddress(
            DeviceExtension,
            Srb,
            Srb->DataBuffer,
            &DataPhysicalLength);
        if (DataPhysical.QuadPart == 0 || DataPhysicalLength < RequestedLength) {
            return B2LUfsTransferBadDma;
        }

        Prdt->BaseAddressLow = DataPhysical.LowPart;
        Prdt->BaseAddressHigh = DataPhysical.HighPart;
        Prdt->DataByteCount = RequestedLength - 1UL;
        Utrd->PrdtOffset = (USHORT)(B2LUFS_ALIGNED_UPIU_SIZE * 2UL);
        Utrd->PrdtLength = (USHORT)B2LUFS_PRDT_ENTRY_BYTES;
        Direction = B2LUFS_UTP_DEVICE_TO_HOST;
    }

    Utrd->Dw0 = Direction | B2LUFS_UTP_SCSI_COMMAND |
                B2LUFS_UTP_REQ_DESC_INT_CMD;
    CommandUpiu->TransactionCode = B2LUFS_UPIU_TRANSACTION_COMMAND;
    CommandUpiu->Flags = (RequestedLength == 0) ? B2LUFS_UPIU_FLAG_NONE :
                                                B2LUFS_UPIU_FLAG_READ;
    CommandUpiu->Lun = 0;
    CommandUpiu->TaskTag = 0;
    /*
     * Command data moves through the PRDT, not the command UPIU data
     * segment.  Its zeroed DataSegmentLength is therefore intentional; the
     * big-endian transfer length belongs to command TSF bytes 0..3.
     */
    CommandUpiu->TaskSpecificFields[0] = (UCHAR)(RequestedLength >> 24);
    CommandUpiu->TaskSpecificFields[1] = (UCHAR)(RequestedLength >> 16);
    CommandUpiu->TaskSpecificFields[2] = (UCHAR)(RequestedLength >> 8);
    CommandUpiu->TaskSpecificFields[3] = (UCHAR)RequestedLength;
    B2LUfsCopyMemory(&CommandUpiu->TaskSpecificFields[4], Srb->Cdb, 16);

    KeMemoryBarrier();
    if ((B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR) &
         B2LUFS_UTRL_SLOT_0) != 0) {
        return B2LUfsTransferBusy;
    }

    B2LUfsWriteVs(DeviceExtension, Adapter, B2LUFS_VS_UTRL_NEXUS_TYPE, MAXULONG);
    B2LUfsWriteHci(DeviceExtension, Adapter, B2LUFS_REG_UTRLDBR,
                    B2LUFS_UTRL_SLOT_0);

    for (Attempts = 0; Attempts < B2LUFS_COMMAND_TIMEOUT_US; ++Attempts) {
        InterruptStatus = B2LUfsReadHci(DeviceExtension, Adapter, B2LUFS_REG_IS);
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
                return B2LUfsTransferOcs;
            }
            if (ResponseUpiu->TransactionCode != B2LUFS_UPIU_TRANSACTION_RESPONSE) {
                return B2LUfsTransferOcs;
            }
            Srb->ScsiStatus = ResponseUpiu->Status;
            return B2LUfsTransferOk;
        }
        StorPortStallExecution(1);
    }

    return B2LUfsTransferTimeout;
}

VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    B2LUFS_TRANSFER_RESULT Result;

    if (!Adapter->TransportReady) {
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_NO_DEVICE);
        return;
    }

    if (Srb->Function == SRB_FUNCTION_FLUSH ||
        Srb->Function == SRB_FUNCTION_SHUTDOWN) {
        /* The read-only transport has no dirty cache and does not issue UFS flush. */
        Srb->DataTransferLength = 0;
        Srb->ScsiStatus = B2LUFS_SCSI_STATUS_GOOD;
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
        Srb->DataTransferLength = 0;
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    /* User data is LUN 0 / target 0 only; boot and RPMB LUNs are never exposed. */
    if (Srb->TargetId != 0 || Srb->Lun != 0) {
        Srb->DataTransferLength = 0;
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_NO_DEVICE);
        return;
    }

    if (!B2LUfsCdbIsExplicitlyReadOnly(Srb->Cdb[0]) ||
        (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != 0) {
        Srb->DataTransferLength = 0;
        /*
         * Storport has no SRB_STATUS_WRITE_PROTECTED status. Reject the
         * request before it reaches the HCI with a valid SRB status; the
         * adapter therefore remains strictly read-only.
         */
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    Result = B2LUfsSubmitReadOnlyScsi(DeviceExtension, Adapter, Srb);
    switch (Result) {
    case B2LUfsTransferOk:
        if (Srb->ScsiStatus == B2LUFS_SCSI_STATUS_GOOD) {
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_SUCCESS);
        } else {
            B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_ERROR);
        }
        break;
    case B2LUfsTransferBusy:
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_BUSY);
        break;
    case B2LUfsTransferTimeout:
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_TIMEOUT);
        break;
    default:
        B2LUfsComplete(DeviceExtension, Srb, SRB_STATUS_ERROR);
        break;
    }
}
