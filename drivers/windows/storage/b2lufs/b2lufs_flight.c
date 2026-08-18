/* See b2lufs_flight.h for the persistent ABI and safety boundary. */
#include <ntddk.h>

#include "b2lufs_flight.h"

static volatile B2LUFS_FLIGHT_HEADER *B2LUfsFlightHeader;
/*
 * Interlocked operations are forbidden on MmNonCached memory.  Serialize the
 * recorder through this ordinary cached image variable, then update the
 * reserved-memory ABI with plain volatile stores.  Never spin here: a lost
 * diagnostic record is preferable to changing StorPort timing or deadlocking
 * at DIRQL during a bugcheck.
 */
static volatile LONG B2LUfsFlightLock;

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
    B2LUfsFlightLock = 0;
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
    if (InterlockedCompareExchange(&B2LUfsFlightLock, 1, 0) != 0) {
        return;
    }
    if (!B2LUfsFlightHeaderValid(B2LUfsFlightHeader) ||
        B2LUfsFlightHeader->Magic != B2LUFS_FLIGHT_MAGIC) {
        (void)InterlockedExchange(&B2LUfsFlightLock, 0);
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
    Record->Irql = (uint8_t)KeGetCurrentIrql();
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
    (void)InterlockedExchange(&B2LUfsFlightLock, 0);
}
