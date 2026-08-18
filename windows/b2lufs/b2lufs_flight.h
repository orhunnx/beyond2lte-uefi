/*
 * B2LUFS early-boot flight recorder.
 *
 * The normal WinPE ETW/registry diagnostics are unavailable when the boot
 * volume itself cannot be mounted.  This fixed binary ABI lives in firmware-
 * reserved DRAM and survives the software warm reset already measured on
 * beyond2lte.  It contains lifecycle/status words only, never media payload.
 */
#ifndef B2LUFS_FLIGHT_H
#define B2LUFS_FLIGHT_H

#include <ntddk.h>
#include <stdint.h>

#define B2LUFS_FLIGHT_PHYSICAL_BASE UINT64_C(0xCF050000)
#define B2LUFS_FLIGHT_BYTES         UINT32_C(0x00010000)
#define B2LUFS_FLIGHT_MAGIC         UINT32_C(0x46553242) /* "B2UF" */
#define B2LUFS_FLIGHT_ARMED_MAGIC   UINT32_C(0x41553242) /* "B2UA" */
#define B2LUFS_FLIGHT_VERSION       UINT16_C(1)
#define B2LUFS_FLIGHT_HEADER_BYTES  UINT16_C(64)
#define B2LUFS_FLIGHT_RECORD_BYTES  UINT16_C(32)
#define B2LUFS_FLIGHT_CAPACITY \
    ((B2LUFS_FLIGHT_BYTES - B2LUFS_FLIGHT_HEADER_BYTES) / \
     B2LUFS_FLIGHT_RECORD_BYTES)
#define B2LUFS_FLIGHT_COMMIT_MAGIC  UINT32_C(0xC17F10A7)

typedef enum B2LUFS_FLIGHT_EVENT {
    B2LUfsFlightDriverEntry = 0x0001,
    B2LUfsFlightStorPortReturn = 0x0002,

    B2LUfsFlightFindBegin = 0x0100,
    B2LUfsFlightFindFeature = 0x0101,
    B2LUfsFlightFindRange = 0x0102,
    B2LUfsFlightFindAllocate = 0x0103,
    B2LUfsFlightFindResult = 0x010F,

    B2LUfsFlightInitializeBegin = 0x0200,
    B2LUfsFlightTransportResult = 0x0201,
    B2LUfsFlightCapacityProbe = 0x0202,
    B2LUfsFlightLunProbe = 0x0203,
    B2LUfsFlightMediaProbe = 0x0204,
    B2LUfsFlightInitializeOk = 0x020F,
    B2LUfsFlightInitializeFail = 0x0210,
    B2LUfsFlightPassiveInitialize = 0x0211,
    B2LUfsFlightDeferredScan = 0x0212,

    B2LUfsFlightStartIo = 0x0300,
    B2LUfsFlightCachedCapacity = 0x0301,
    B2LUfsFlightCachedGpt = 0x0302,
    B2LUfsFlightSubmitBegin = 0x0310,
    B2LUfsFlightSubmitDone = 0x0311,
    B2LUfsFlightUnitControl = 0x0320,
    B2LUfsFlightAdapterControl = 0x0321,
    B2LUfsFlightResetBus = 0x0322
} B2LUFS_FLIGHT_EVENT;

typedef enum B2LUFS_FLIGHT_TRANSPORT_STAGE {
    B2LUfsFlightTransportOk = 0,
    B2LUfsFlightTransportNoDma = 1,
    B2LUfsFlightTransportNotReady = 2,
    B2LUfsFlightTransportAddressWidth = 3,
    B2LUfsFlightTransportHceOff = 4,
    B2LUfsFlightTransportDoorbellBusy = 5,
    B2LUfsFlightTransportStopTimeout = 6,
    B2LUfsFlightTransportStartFailed = 7
} B2LUFS_FLIGHT_TRANSPORT_STAGE;

typedef struct B2LUFS_FLIGHT_HEADER {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    uint16_t RecordSize;
    uint16_t Reserved0;
    uint32_t Capacity;
    volatile LONG WriteIndex;
    uint32_t BootId;
    uint32_t Flags;
    uint32_t Reserved[9];
} B2LUFS_FLIGHT_HEADER;

typedef struct B2LUFS_FLIGHT_RECORD {
    uint32_t Sequence;
    uint16_t Event;
    uint8_t Irql;
    uint8_t Flags;
    uint32_t Argument[5];
    uint32_t Commit;
} B2LUFS_FLIGHT_RECORD;

BOOLEAN
B2LUfsFlightInitialize(_In_ BOOLEAN DumpMode);

VOID
B2LUfsFlightLog(
    _In_ B2LUFS_FLIGHT_EVENT Event,
    _In_ UCHAR Flags,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1,
    _In_ ULONG Argument2,
    _In_ ULONG Argument3,
    _In_ ULONG Argument4);

#endif /* B2LUFS_FLIGHT_H */
