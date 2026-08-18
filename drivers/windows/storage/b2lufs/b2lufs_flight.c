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

/*
 * Reset gerektirmeyen 0x7B geri-alma kanali.  beyond2lte'nin sabit paneli
 * 1440 piksel genistir ve 0xCA000000 tabanli Display Reserved araliginda
 * BGRA dword olarak cihazda daha once B2LFB ile dogrulanmistir.  Bu esleme
 * yalniz B2LGoster kaydi ARMED yaptiginda kurulur; normal paket no-op kalir.
 */
#define B2LUFS_FLIGHT_FB_BASE         UINT64_C(0xCA000000)
#define B2LUFS_FLIGHT_FB_BYTES        UINT32_C(0x01400000)
#define B2LUFS_FLIGHT_FB_WIDTH        1440U
#define B2LUFS_FLIGHT_FB_HEIGHT       3040U
#define B2LUFS_FLIGHT_PANEL_HEIGHT    288U
#define B2LUFS_FLIGHT_PANEL_BG        UINT32_C(0x00101010)
#define B2LUFS_FLIGHT_PANEL_HEADER    UINT32_C(0x0000FFFF)
#define B2LUFS_FLIGHT_PANEL_LABEL     UINT32_C(0x00FFFFFF)
#define B2LUFS_FLIGHT_PANEL_RECORD    UINT32_C(0x00FFFF00)

static volatile ULONG *B2LUfsFlightFramebuffer;
static KBUGCHECK_CALLBACK_RECORD B2LUfsFlightBugCheckRecord;
static UCHAR B2LUfsFlightBugCheckComponent[] = "b2lufs-flight";

KBUGCHECK_CALLBACK_ROUTINE B2LUfsFlightBugCheckCallback;

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

static BOOLEAN
B2LUfsFlightRecordValid(
    _In_ const volatile B2LUFS_FLIGHT_RECORD *Record,
    _In_ ULONG Sequence)
{
    ULONG Commit;

    if (Record->Sequence != Sequence) {
        return FALSE;
    }
    Commit = B2LUFS_FLIGHT_COMMIT_MAGIC ^ Record->Sequence ^
             ((ULONG)Record->Event << 16) ^
             ((ULONG)Record->Irql << 8) ^ Record->Flags ^
             Record->Argument[0] ^ Record->Argument[1] ^
             Record->Argument[2] ^ Record->Argument[3] ^
             Record->Argument[4];
    return Record->Commit == Commit;
}

static const UCHAR *
B2LUfsFlightGlyph(_In_ CHAR Character)
{
    /* 5x7, satir basina dusuk bes bit. Yalniz resident .rdata okunur. */
    static const UCHAR Font[36][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
        {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 3 */
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
        {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, /* 5 */
        {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
        {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}, /* 9 */
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
        {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, /* C */
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
        {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}, /* G */
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, /* J */
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}  /* Z */
    };

    if (Character >= '0' && Character <= '9') {
        return Font[Character - '0'];
    }
    if (Character >= 'A' && Character <= 'Z') {
        return Font[10 + Character - 'A'];
    }
    return NULL;
}

static VOID
B2LUfsFlightDrawRect(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Color)
{
    ULONG Row;
    ULONG Column;

    if (B2LUfsFlightFramebuffer == NULL ||
        X >= B2LUFS_FLIGHT_FB_WIDTH || Y >= B2LUFS_FLIGHT_FB_HEIGHT) {
        return;
    }
    if (Width > B2LUFS_FLIGHT_FB_WIDTH - X) {
        Width = B2LUFS_FLIGHT_FB_WIDTH - X;
    }
    if (Height > B2LUFS_FLIGHT_FB_HEIGHT - Y) {
        Height = B2LUFS_FLIGHT_FB_HEIGHT - Y;
    }
    for (Row = 0; Row < Height; ++Row) {
        volatile ULONG *Pixel = B2LUfsFlightFramebuffer +
            ((Y + Row) * B2LUFS_FLIGHT_FB_WIDTH) + X;
        for (Column = 0; Column < Width; ++Column) {
            Pixel[Column] = Color;
        }
    }
}

static VOID
B2LUfsFlightDrawChar(
    _In_ CHAR Character,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Scale,
    _In_ ULONG Color)
{
    const UCHAR *Glyph;
    ULONG Row;
    ULONG Column;

    Glyph = B2LUfsFlightGlyph(Character);
    if (Glyph == NULL) {
        return;
    }
    for (Row = 0; Row < 7; ++Row) {
        for (Column = 0; Column < 5; ++Column) {
            if ((Glyph[Row] & (1U << (4U - Column))) != 0) {
                B2LUfsFlightDrawRect(
                    X + Column * Scale, Y + Row * Scale,
                    Scale, Scale, Color);
            }
        }
    }
}

static VOID
B2LUfsFlightDrawText(
    _In_z_ const CHAR *Text,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Scale,
    _In_ ULONG Color)
{
    while (*Text != '\0') {
        B2LUfsFlightDrawChar(*Text, X, Y, Scale, Color);
        X += 6U * Scale;
        ++Text;
    }
}

static VOID
B2LUfsFlightDrawHex(
    _In_ ULONG Value,
    _In_ ULONG Digits,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Scale,
    _In_ ULONG Color)
{
    ULONG Index;

    for (Index = 0; Index < Digits; ++Index) {
        ULONG Shift = (Digits - Index - 1U) * 4U;
        ULONG Digit = (Value >> Shift) & 0xFU;
        CHAR Character = (CHAR)(Digit < 10U ? '0' + Digit : 'A' + Digit - 10U);
        B2LUfsFlightDrawChar(Character, X + Index * 6U * Scale,
                            Y, Scale, Color);
    }
}

static VOID
B2LUfsFlightDrawPanel(VOID)
{
    volatile UCHAR *Base;
    volatile B2LUFS_FLIGHT_RECORD *Record;
    volatile B2LUFS_FLIGHT_RECORD *LastRecord;
    ULONG First;
    ULONG Last;
    ULONG Sequence;
    ULONG Row;

    B2LUfsFlightDrawRect(0, 0, B2LUFS_FLIGHT_FB_WIDTH,
                        B2LUFS_FLIGHT_PANEL_HEIGHT,
                        B2LUFS_FLIGHT_PANEL_BG);
    B2LUfsFlightDrawText("B2LUFS BUGCHECK FLIGHT", 24, 16, 5,
                        B2LUFS_FLIGHT_PANEL_HEADER);
    B2LUfsFlightDrawText("SEQ", 24, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("EVT", 190, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("A0", 290, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("A1", 480, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("A2", 670, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("A3", 860, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);
    B2LUfsFlightDrawText("A4", 1050, 64, 3, B2LUFS_FLIGHT_PANEL_LABEL);

    if (!B2LUfsFlightHeaderValid(B2LUfsFlightHeader) ||
        B2LUfsFlightHeader->WriteIndex <= 0) {
        B2LUfsFlightDrawText("NO VALID RECORD", 24, 112, 5,
                            B2LUFS_FLIGHT_PANEL_HEADER);
        return;
    }

    Last = (ULONG)B2LUfsFlightHeader->WriteIndex;
    First = Last > 6U ? Last - 5U : 1U;
    Base = (volatile UCHAR *)B2LUfsFlightHeader;
    LastRecord = NULL;
    Row = 0;
    for (Sequence = First; Sequence <= Last && Row < 6U; ++Sequence) {
        ULONG Slot = (Sequence - 1U) % B2LUFS_FLIGHT_CAPACITY;
        ULONG Y = 96U + Row * 32U;

        Record = (volatile B2LUFS_FLIGHT_RECORD *)(Base +
            B2LUFS_FLIGHT_HEADER_BYTES +
            Slot * B2LUFS_FLIGHT_RECORD_BYTES);
        if (!B2LUfsFlightRecordValid(Record, Sequence)) {
            continue;
        }
        B2LUfsFlightDrawHex(Record->Sequence, 8, 24, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Event, 4, 190, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Argument[0], 8, 290, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Argument[1], 8, 480, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Argument[2], 8, 670, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Argument[3], 8, 860, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        B2LUfsFlightDrawHex(Record->Argument[4], 8, 1050, Y, 3,
                           B2LUFS_FLIGHT_PANEL_RECORD);
        LastRecord = Record;
        ++Row;
    }
    if (LastRecord == NULL) {
        B2LUfsFlightDrawText("RECORD COMMIT INVALID", 24, 112, 5,
                            B2LUFS_FLIGHT_PANEL_HEADER);
    }
}

_Use_decl_annotations_
VOID
B2LUfsFlightBugCheckCallback(PVOID Buffer, ULONG Length)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);

    /* HIGH_LEVEL: yalniz onceden resident olan bellek okunur ve yazilir. */
    if (B2LUfsFlightHeader != NULL) {
        B2LUfsFlightHeader->Flags |= B2LUFS_FLIGHT_FLAG_CALLBACK_FIRED;
    }
    B2LUfsFlightDrawPanel();
}

static VOID
B2LUfsFlightScreenInitialize(VOID)
{
    PHYSICAL_ADDRESS Physical;

    Physical.QuadPart = B2LUFS_FLIGHT_FB_BASE;
    B2LUfsFlightFramebuffer = (volatile ULONG *)MmMapIoSpace(
        Physical, B2LUFS_FLIGHT_FB_BYTES, MmNonCached);
    if (B2LUfsFlightFramebuffer == NULL) {
        return;
    }
    B2LUfsFlightHeader->Flags |= B2LUFS_FLIGHT_FLAG_FB_MAPPED;
    KeInitializeCallbackRecord(&B2LUfsFlightBugCheckRecord);
    if (KeRegisterBugCheckCallback(
            &B2LUfsFlightBugCheckRecord,
            B2LUfsFlightBugCheckCallback,
            (PVOID)B2LUfsFlightHeader,
            B2LUFS_FLIGHT_HEADER_BYTES,
            B2LUfsFlightBugCheckComponent)) {
        B2LUfsFlightHeader->Flags |=
            B2LUFS_FLIGHT_FLAG_CALLBACK_REGISTERED;
    }
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

    if (!DumpMode) {
        B2LUfsFlightScreenInitialize();
    }

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
