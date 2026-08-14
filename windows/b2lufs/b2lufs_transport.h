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

    ULONG ControllerCapabilities;
    /* Read-only CMD telemetry; copied to the StorPort Operational channel. */
    ULONG TraceSequence;
    ULONG TraceBudget;
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

/* Route a single, serialized SRB to UFS logical unit 0. */
VOID
B2LUfsExecuteScsi(
    _In_ PVOID DeviceExtension,
    _Inout_ B2LUFS_ADAPTER *Adapter,
    _Inout_ PSCSI_REQUEST_BLOCK Srb);

#endif /* B2LUFS_TRANSPORT_H */
