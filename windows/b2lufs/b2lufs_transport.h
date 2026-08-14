/*
 * B2LUFS — UFSHCI/UTP transport surface used by the StorPort miniport.
 *
 * This header deliberately describes only one queue slot and logical unit 0.
 * The initial Windows bring-up is read-only: it exposes the existing Android
 * GPT for discovery but never submits a media-mutating SCSI command.
 */
#ifndef B2LUFS_TRANSPORT_H
#define B2LUFS_TRANSPORT_H

#include <storport.h>

#include "b2lufs_hw.h"

typedef struct B2LUFS_ADAPTER {
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
     * range. The result is diagnostic telemetry only: Windows' own later
     * READ CAPACITY(10/16) SRBs always follow the normal UFS command path.
     * It never contains user data and is never written to UFS.
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
    ULONG ConfiguredMaxNumberOfIo;
    ULONG ConfiguredMaxIoPerLun;
    ULONG ConfiguredInitialLunQueueDepth;
    /* Read-only CMD telemetry; copied to the StorPort Operational channel. */
    ULONG TraceSequence;
    ULONG TraceBudget;
    /* PnP/BTL8 lifecycle telemetry; it never contains UFS media data. */
    ULONG PnpTraceSequence;
    ULONG PnpTraceBudget;
    ULONG LastUtrdOcs;
    ULONG LastInterruptStatus;
    BOOLEAN TransportReady;
} B2LUFS_ADAPTER;

/* Allocate exactly one UTRD/UCD slot while HwFindAdapter may allocate DMA. */
BOOLEAN
B2LUfsAllocateTransport(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo);

/* Adopt the live UFS link without reset, UIC/DME commands, or LUN writes. */
BOOLEAN
B2LUfsStartTransport(
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

/* Complete a legacy PnP SRB without submitting a UFS media command. */
VOID
B2LUfsHandlePnpSrb(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

/* Route a single, serialized SRB to UFS logical unit 0. */
VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

#endif /* B2LUFS_TRANSPORT_H */
