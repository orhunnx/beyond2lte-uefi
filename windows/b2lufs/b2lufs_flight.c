/* See b2lufs_flight.h for the persistent ABI and safety boundary. */
#include <ntddk.h>

#include "b2lufs_flight.h"

static volatile B2LUFS_FLIGHT_HEADER *B2LUfsFlightHeader;
/*
 * This is deliberately a non-atomic, cached best-effort reentrancy gate.
 * MSVC/WDK 28000 leaves every tested Interlocked spelling as an unresolved
 * external in this raw ARM64 /kernel /Zl link, even with #pragma intrinsic.
 * The recorder protects diagnostics only: if two CPUs race through this gate,
 * they can overwrite one ring slot, whose commit check will reject a mixed
 * record.  Losing a trace entry is preferable to changing StorPort timing or
 * making the boot driver depend on a CRT/kernel atomic helper.
 */
static volatile LONG B2LUfsFlightBusy;

C_ASSERT(sizeof(B2LUFS_FLIGHT_HEADER) == B2LUFS_FLIGHT_HEADER_BYTES);
C_ASSERT(sizeof(B2LUFS_FLIGHT_RECORD) == B2LUFS_FLIGHT_RECORD_BYTES);
C_ASSERT(B2LUFS_FLIGHT_CAPACITY == 2046U);

static BOOLEAN
B2LUfsFlightHeaderValid(_In_ const volatile B2LUFS_FLIGHT_HEADER *Header)
{
    return Header != NULL &&
           (Header->Magic == B2LUFS_FLIGHT_MAGIC ||
            Header->Magic == B2LUFS_FLIGHT_ARMED_MAGIC) &&
           Header->Version == B2LUFS_FLIGHT_VERSION &&
           Header->HeaderSize == B2LUFS_FLIGHT_HEADER_BYTES &&
           Header->RecordSize == B2LUFS_FLIGHT_RECORD_BYTES &&
           Header->Capacity == B2LUFS_FLIGHT_CAPACITY;
}

static VOID
B2LUfsFlightResetHeader(
    _Inout_ volatile B2LUFS_FLIGHT_HEADER *Header,
    _In_ ULONG BootId)
{
    ULONG Index;

    /* Magic is the commit word: readers reject a partially reset header. */
    Header->Magic = 0;
    KeMemoryBarrier();
    Header->Version = B2LUFS_FLIGHT_VERSION;
    Header->HeaderSize = B2LUFS_FLIGHT_HEADER_BYTES;
    Header->RecordSize = B2LUFS_FLIGHT_RECORD_BYTES;
    Header->Reserved0 = 0;
    Header->Capacity = B2LUFS_FLIGHT_CAPACITY;
    Header->WriteIndex = 0;
    Header->BootId = BootId;
    Header->Flags = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Header->Reserved); ++Index) {
        Header->Reserved[Index] = 0;
    }
    KeMemoryBarrier();
    Header->Magic = B2LUFS_FLIGHT_MAGIC;
    KeMemoryBarrier();
}

BOOLEAN
B2LUfsFlightInitialize(_In_ BOOLEAN DumpMode)
{
    PHYSICAL_ADDRESS Physical;
    ULONG BootId;
    BOOLEAN Valid;

    if (B2LUfsFlightHeader == NULL) {
        Physical.QuadPart = B2LUFS_FLIGHT_PHYSICAL_BASE;
        B2LUfsFlightHeader = (volatile B2LUFS_FLIGHT_HEADER *)MmMapIoSpace(
            Physical, B2LUFS_FLIGHT_BYTES, MmNonCached);
    }
    if (B2LUfsFlightHeader == NULL) {
        return FALSE;
    }

    Valid = B2LUfsFlightHeaderValid(B2LUfsFlightHeader);
    if (DumpMode) {
        if (!Valid || B2LUfsFlightHeader->Magic != B2LUFS_FLIGHT_MAGIC) {
            MmUnmapIoSpace(
                (PVOID)B2LUfsFlightHeader, B2LUFS_FLIGHT_BYTES);
            B2LUfsFlightHeader = NULL;
            return FALSE;
        }
    } else {
        /* Normal packages stay inert unless B2LGoster explicitly armed RAM. */
        if (!Valid ||
            B2LUfsFlightHeader->Magic != B2LUFS_FLIGHT_ARMED_MAGIC) {
            MmUnmapIoSpace(
                (PVOID)B2LUfsFlightHeader, B2LUFS_FLIGHT_BYTES);
            B2LUfsFlightHeader = NULL;
            return FALSE;
        }
        BootId = B2LUfsFlightHeader->BootId;
        if (BootId == 0) {
            BootId = 1;
        }
        B2LUfsFlightResetHeader(B2LUfsFlightHeader, BootId);
    }
    B2LUfsFlightBusy = 0;
    KeMemoryBarrier();

    B2LUfsFlightLog(
        B2LUfsFlightDriverEntry,
        DumpMode ? 1U : 0U,
        DumpMode ? 1UL : 0UL,
        (ULONG)B2LUFS_FLIGHT_PHYSICAL_BASE,
        (ULONG)(B2LUFS_FLIGHT_PHYSICAL_BASE >> 32),
        B2LUFS_FLIGHT_BYTES,
        B2LUfsFlightHeader->BootId);
    return TRUE;
}

VOID
B2LUfsFlightLog(
    _In_ B2LUFS_FLIGHT_EVENT Event,
    _In_ UCHAR Flags,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1,
    _In_ ULONG Argument2,
    _In_ ULONG Argument3,
    _In_ ULONG Argument4)
{
    volatile B2LUFS_FLIGHT_RECORD *Record;
    volatile UCHAR *Base;
    ULONG Sequence;
    ULONG Slot;
    ULONG EventWord;
    ULONG Commit;

    if (!B2LUfsFlightHeaderValid(B2LUfsFlightHeader) ||
        B2LUfsFlightHeader->Magic != B2LUFS_FLIGHT_MAGIC) {
        return;
    }
    if (B2LUfsFlightBusy != 0) {
        return;
    }
    B2LUfsFlightBusy = 1;
    KeMemoryBarrier();
    if (!B2LUfsFlightHeaderValid(B2LUfsFlightHeader) ||
        B2LUfsFlightHeader->Magic != B2LUFS_FLIGHT_MAGIC) {
        B2LUfsFlightBusy = 0;
        return;
    }

    Sequence = (ULONG)B2LUfsFlightHeader->WriteIndex + 1UL;
    if (Sequence == 0) {
        Sequence = 1;
    }
    Slot = (Sequence - 1UL) % B2LUFS_FLIGHT_CAPACITY;
    Base = (volatile UCHAR *)B2LUfsFlightHeader;
    Record = (volatile B2LUFS_FLIGHT_RECORD *)(Base +
        B2LUFS_FLIGHT_HEADER_BYTES + (Slot * B2LUFS_FLIGHT_RECORD_BYTES));

    Record->Commit = 0;
    Record->Sequence = Sequence;
    Record->Event = (uint16_t)Event;
    /*
     * KeGetCurrentIrql was emitted as an unresolved import by the standalone
     * ARM64 WDK link.  IRQL is diagnostic metadata, not part of correctness;
     * keep the ABI byte and mark it unknown instead of adding an OS-version
     * dependent StorPort DDI or undocumented processor-register read.
     */
    Record->Irql = B2LUFS_FLIGHT_IRQL_UNKNOWN;
    Record->Flags = Flags;
    Record->Argument[0] = Argument0;
    Record->Argument[1] = Argument1;
    Record->Argument[2] = Argument2;
    Record->Argument[3] = Argument3;
    Record->Argument[4] = Argument4;

    EventWord = ((ULONG)Record->Event << 16) |
                ((ULONG)Record->Irql << 8) | Record->Flags;
    Commit = B2LUFS_FLIGHT_COMMIT_MAGIC ^ Sequence ^ EventWord ^
             Argument0 ^ Argument1 ^ Argument2 ^ Argument3 ^ Argument4;
    KeMemoryBarrier();
    Record->Commit = Commit;
    KeMemoryBarrier();
    B2LUfsFlightHeader->WriteIndex = (LONG)Sequence;
    KeMemoryBarrier();
    B2LUfsFlightBusy = 0;
}
