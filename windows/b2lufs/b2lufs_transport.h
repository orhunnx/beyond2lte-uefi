/*
 * B2LUFS — UFSHCI/UTP transport surface used by the StorPort miniport.
 *
 * This header deliberately describes only one queue slot and logical unit 0.
 * Normal StorPort read/write SCSI requests are transferred to the existing
 * UFS user LUN; boot and RPMB LUNs remain outside the Windows address space.
 */
#ifndef B2LUFS_TRANSPORT_H
#define B2LUFS_TRANSPORT_H

#include <storport.h>

#include "b2lufs_hw.h"

#define B2LUFS_SRB_EXTENSION_SIGNATURE 0x58455342UL /* "BSEX" */
#define B2LUFS_WINDOWS_GPT_BLOCK_BYTES       4096UL
#define B2LUFS_WINDOWS_GPT_ENTRIES_BYTES    16384UL

/*
 * Microsoft'in StorAHCI ornegindeki gibi, StorPort'un HwBuildIo sirasinda
 * hazirladigi istege ozel adresleri SrbExtension'da tut. Sistem adresi ortak
 * DMA bounce kopyasi icin, scatter/gather listesi ise tani icin kullanilir.
 * HwBuildIo eszamansiz olabilir; bu nedenle bunlar adapter extension'a
 * yazilmaz ve yalniz SRB tamamlanana kadar gecerlidir.
 */
typedef struct B2LUFS_SRB_EXTENSION {
    ULONG Signature;
    ULONG OriginalTransferLength;
    ULONG ScatterGatherElements;
    ULONG SystemAddressStatus;
    PSTOR_SCATTER_GATHER_LIST ScatterGather;
    PVOID SystemAddress;
} B2LUFS_SRB_EXTENSION, *PB2LUFS_SRB_EXTENSION;

typedef struct B2LUFS_ADAPTER {
    ULONG Signature;
    B2LUFS_RESOURCES Resources;

    /* StorPort-owned, uncached common DMA allocation. */
    PVOID CommonBuffer;
    STOR_PHYSICAL_ADDRESS CommonPhysical;
    PVOID UtrdVirtual;
    STOR_PHYSICAL_ADDRESS UtrdPhysical;
    PVOID UcdVirtual;
    STOR_PHYSICAL_ADDRESS UcdPhysical;

    /*
     * A 32-byte, DMA-coherent scratch area used once before BusChange for
     * READ CAPACITY.  The broadly compatible 10-byte form is tried first;
     * the 16-byte form is used only for a device larger than its address
     * range. It never contains user data and is never written to UFS. The
     * validated result is both diagnostic telemetry and the authoritative
     * reply to disk class's later READ CAPACITY SRB. The software-only reply
     * is completed from serialized HwStartIo because it reads adapter-owned
     * state and issues RequestComplete.
     */
    PVOID CapacityProbeVirtual;
    STOR_PHYSICAL_ADDRESS CapacityProbePhysical;
    ULONG CapacityProbeLength;
    ULONG CapacityProbeResult;
    UCHAR CapacityProbeScsiStatus;
    UCHAR CapacityProbeOpcode;
    ULONG CapacityProbeBytes;
    ULONGLONG CapacityLastLba;
    ULONG CapacityBlockLength;
    BOOLEAN CapacityValid;

    /*
     * REPORT LUNS is a standard, device-to-host SCSI discovery command sent
     * only to LUN 0.  This DMA-coherent snapshot is exposed to WinPE solely
     * as registry telemetry; no additional LUN is enumerated or accessed.
     */
    PVOID LunReportVirtual;
    STOR_PHYSICAL_ADDRESS LunReportPhysical;
    ULONG LunReportLength;
    ULONG LunReportListLength;
    ULONG LunReportBytes;
    ULONG LunReportEntryCount;
    ULONG LunReportResult;
    UCHAR LunReportScsiStatus;
    BOOLEAN LunReportValid;

    /*
     * The same READ(10) + common-DMA path later used by disk class is
     * exercised before BusChange. Raw signatures/CRCs remain diagnostic.
     * Once both GPT CRCs validate, an immutable, CPU-only Windows view is
     * also retained. HwStartIo can then answer partmgr's metadata reads from
     * one coherent snapshot instead of mixing separate live UFS commands.
     */
    ULONG MediaProbeResult;
    UCHAR MediaProbeScsiStatus;
    ULONG MediaProbeBytes;
    BOOLEAN MediaProbeValid;
    BOOLEAN MediaProbeMbrSignature;
    BOOLEAN MediaProbeGptSignature;
    BOOLEAN MediaProbeProtectiveMbrValid;
    BOOLEAN MediaProbeGptHeaderValid;
    BOOLEAN MediaProbeGptEntriesValid;
    BOOLEAN MediaNormalizeProtectiveMbr;
    ULONG MediaProbeGptHeaderSize;
    ULONG MediaProbeGptHeaderCrc;
    ULONG MediaProbeGptHeaderCrcCalculated;
    ULONG MediaProbeGptEntryCount;
    ULONG MediaProbeGptEntrySize;
    ULONG MediaProbeGptEntriesBytes;
    ULONG MediaProbeGptEntriesCrc;
    ULONG MediaProbeGptEntriesCrcCalculated;
    UCHAR MediaProbeLba0[64];
    UCHAR MediaProbeLba1[64];
    BOOLEAN MediaWindowsGptSnapshotValid;
    BOOLEAN MediaWindowsCacheEntryFound;
    ULONG MediaWindowsGptEntryLba;
    ULONG MediaWindowsGptEntryBlocks;
    ULONG MediaWindowsGptEntriesCrc;
    ULONG MediaWindowsCacheEntryIndex;
    UCHAR MediaWindowsGptHeader[B2LUFS_WINDOWS_GPT_BLOCK_BYTES];
    UCHAR MediaWindowsGptEntries[B2LUFS_WINDOWS_GPT_ENTRIES_BYTES];

    /*
     * Serialized normal I/O uses one StorPort common DMA bounce buffer.
     * The firmware-proven contiguous path is used for both reads and writes;
     * CPU copies connect it to the current SRB's system virtual address.
     */
    PVOID IoBounceVirtual;
    STOR_PHYSICAL_ADDRESS IoBouncePhysical;
    ULONG IoBounceLength;

    ULONG ControllerCapabilities;
    /* Live HCI state captured before B2LUFS borrows the firmware link. */
    ULONG ControllerStatus;
    ULONG AutoHibern8Before;
    ULONG TransferSlotCount;
    ULONG TaskSlotCount;
    /* The StorPort protocol negotiated by DriverEntry/HwFindAdapter. */
    ULONG ConfiguredSrbType;
    ULONG ConfiguredAddressType;
    ULONG ConfiguredNumberOfBuses;
    ULONG ConfiguredMaximumTargets;
    ULONG ConfiguredInitiatorBusId;
    ULONG ConfiguredScatterGather;
    ULONG ConfiguredMaster;
    ULONG ConfiguredTaggedQueuing;
    ULONG ConfiguredAutoRequestSense;
    ULONG ConfiguredMultipleRequestPerLu;
    ULONG ConfiguredPhysicalBreaks;
    ULONG ConfiguredSynchronizationModel;
    ULONG ConfiguredMaxNumberOfIo;
    ULONG ConfiguredMaxIoPerLun;
    ULONG ConfiguredInitialLunQueueDepth;
    /* StorPort unit bus-type feature/query contract (BusTypeUfs). */
    ULONG BusTypeFeatureStatus;
    ULONG ConfiguredUnitBusType;
    ULONG BusTypeQueryCount;
    /* Explicit StorPort timer used for the post-start LUN scan. */
    PVOID BusScanTimer;
    ULONG BusScanTimerInitializeStatus;
    ULONG BusScanTimerRequestStatus;
    BOOLEAN DumpMode;
    BOOLEAN AdapterRemoved;
    /* Bounded CMD telemetry; write payloads are never copied to ETW. */
    ULONG TraceSequence;
    ULONG TraceBudget;
    /* PnP/BTL8 lifecycle telemetry; it never contains UFS media data. */
    ULONG PnpTraceSequence;
    ULONG PnpTraceBudget;
    ULONG LastUtrdOcs;
    ULONG LastInterruptStatus;
    ULONG LastPrdtBuildStatus;
    ULONG LastPrdtEntryCount;
    ULONG LastFirstSglLength;
    ULONGLONG LastFirstSglPhysical;
    BOOLEAN TransportReady;
} B2LUFS_ADAPTER;

/* Allocate exactly one UTRD/UCD slot while HwFindAdapter may allocate DMA. */
BOOLEAN
B2LUfsAllocateTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo);

/* Adopt the live UFS link without reset or UIC/DME reinitialization. */
BOOLEAN
B2LUfsStartTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Stop/restart only the borrowed UTP transfer list; never reset UFS media. */
BOOLEAN
B2LUfsStopTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

BOOLEAN
B2LUfsResetTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Read and validate the live LUN-0 capacity reply before scanning. */
VOID
B2LUfsProbeLun0Capacity(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Take one standard, read-only REPORT LUNS snapshot for WinPE diagnostics. */
VOID
B2LUfsProbeLuns(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Verify normal block DMA by reading LBA 0 and LBA 1 before enumeration. */
VOID
B2LUfsProbeMediaHeaders(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Complete a cached READ CAPACITY in HwStartIo; TRUE means SRB completed. */
BOOLEAN
B2LUfsTryCompleteCachedCapacity(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

/* Complete an all-metadata GPT READ in HwStartIo from the validated view. */
BOOLEAN
B2LUfsTryCompleteCachedGptRead(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

/* Complete a legacy PnP SRB without submitting a UFS media command. */
VOID
B2LUfsHandlePnpSrb(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

/* Release the explicit bus-scan timer when PnP removes the adapter. */
VOID
B2LUfsReleaseBusScanTimer(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter);

/* Route a single, serialized SRB to UFS logical unit 0. */
VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

#endif /* B2LUFS_TRANSPORT_H */
