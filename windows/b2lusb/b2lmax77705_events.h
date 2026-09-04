#ifndef B2LMAX77705_EVENTS_H
#define B2LMAX77705_EVENTS_H

// Shared REG_BINARY layout. Include after ntddk.h or windows.h.
#define B2L_MAX_EVENT_VERSION 1UL
#define B2L_MAX_EVENT_FIRST   16UL
#define B2L_MAX_EVENT_TAIL    48UL
#define B2L_MAX_EVENT_CAPACITY (B2L_MAX_EVENT_FIRST + B2L_MAX_EVENT_TAIL)
#define B2L_MAX_EVENT_BASELINE 1UL
#define B2L_MAX_EVENT_DRAIN    2UL
#define B2L_MAX_EVENT_WAIT     3UL
#define B2L_MAX_EVENT_POLL     4UL
#define B2L_MAX_EVENT_CC_FAILED 5UL
#define B2L_MAX_EVENT_VALUE L"B2LMaxEventLog"

typedef struct _B2L_MAX_EVENT {
    ULONGLONG Time;       // KeQueryInterruptTime(), 100 ns units
    ULONGLONG Sequence;
    ULONG Origin;
    ULONG Opcode;        // 0xff when not in an opcode exchange
    ULONG Interrupts;    // UIC | CC<<8 | PD<<16 | VDM<<24; only UIC for opcode
    ULONG PolicyCc;      // CC0 | CC1<<8 from policy sample; ~0UL if unavailable
    ULONG ReadStatus;    // snapshot NTSTATUS; original policy failure for o=5
    ULONG Valid;         // Status[] valid only when this is 1
    UCHAR Status[8];     // Registers 0x06..0x0d, including SYSMSG, CC0/1, PD0/1
} B2L_MAX_EVENT;

typedef struct _B2L_MAX_EVENT_LOG {
    ULONG Version;
    ULONG Size;
    ULONG Count;
    ULONG NextTail;      // relative to tail region, oldest when full
    ULONG OpcodeUicSeen;
    ULONG SnapshotFailures; // invalid snapshots, including o=5 skipped reads
    ULONG SysmsgEvents;  // observed UIC SYSMSG bits, not unique firmware resets
    ULONG AttachErrorEvents; // observed raw CC interrupt bit 5 (may be masked)
    ULONGLONG StartTime;
    ULONGLONG Total;
    ULONGLONG Overwritten;
    B2L_MAX_EVENT Events[B2L_MAX_EVENT_CAPACITY];
} B2L_MAX_EVENT_LOG;

C_ASSERT(sizeof(B2L_MAX_EVENT) == 48);
C_ASSERT(sizeof(B2L_MAX_EVENT_LOG) == 3128);

#endif
