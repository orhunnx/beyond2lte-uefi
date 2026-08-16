/*
 * B2LGPTDUMP - freestanding ARM64 WinPE raw GPT cross-check.
 *
 * Reads through the top of the Windows storage stack, never writes to the
 * physical disk, and stores four binary snapshots on WinPE's X: RAM disk.
 * No CRT is used so the executable runs in the minimal image.
 */
typedef void *PVOID;
typedef void *HANDLE;
typedef unsigned char UCHAR;
typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef unsigned __int64 ULONGLONG;
typedef __int64 LONGLONG;
typedef int BOOL;
/* Freestanding Windows ARM64 ABI: wchar_t is a 16-bit UTF-16 code unit. */
typedef unsigned short wchar_t;
typedef const wchar_t *LPCWSTR;

typedef union B2L_LARGE_INTEGER {
    struct {
        DWORD LowPart;
        long HighPart;
    } Parts;
    LONGLONG QuadPart;
} B2L_LARGE_INTEGER;

typedef struct B2L_GET_LENGTH_INFORMATION {
    B2L_LARGE_INTEGER Length;
} B2L_GET_LENGTH_INFORMATION;

__declspec(dllimport) HANDLE CreateFileW(
    LPCWSTR, DWORD, DWORD, PVOID, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL ReadFile(HANDLE, PVOID, DWORD, DWORD *, PVOID);
__declspec(dllimport) BOOL WriteFile(
    HANDLE, const void *, DWORD, DWORD *, PVOID);
__declspec(dllimport) BOOL SetFilePointerEx(
    HANDLE, B2L_LARGE_INTEGER, B2L_LARGE_INTEGER *, DWORD);
__declspec(dllimport) BOOL DeviceIoControl(
    HANDLE, DWORD, PVOID, DWORD, PVOID, DWORD, DWORD *, PVOID);
__declspec(dllimport) HANDLE GetStdHandle(DWORD);
__declspec(dllimport) BOOL CloseHandle(HANDLE);
__declspec(dllimport) void ExitProcess(DWORD);

#define B2L_GENERIC_READ              0x80000000UL
#define B2L_GENERIC_WRITE             0x40000000UL
#define B2L_FILE_SHARE_READ           0x00000001UL
#define B2L_FILE_SHARE_WRITE          0x00000002UL
#define B2L_CREATE_ALWAYS             2UL
#define B2L_OPEN_EXISTING             3UL
#define B2L_FILE_ATTRIBUTE_NORMAL     0x00000080UL
#define B2L_FILE_BEGIN                0UL
#define B2L_STD_OUTPUT_HANDLE         ((DWORD)-11L)
#define B2L_IOCTL_DISK_GET_LENGTH_INFO 0x0007405CUL
#define B2L_BLOCK_BYTES               4096UL
#define B2L_ENTRY_BYTES              16384UL
#define B2L_SAMPLE_BYTES                64UL
#define B2L_INVALID_HANDLE_VALUE      ((HANDLE)(ULONGLONG)-1)

static __declspec(align(4096)) UCHAR B2LBuffer[B2L_ENTRY_BYTES];

static DWORD
B2LTextLength(const char *Text)
{
    DWORD Length = 0;
    while (Text[Length] != 0) {
        ++Length;
    }
    return Length;
}

static void
B2LPrint(const char *Text)
{
    DWORD Written;
    (void)WriteFile(
        GetStdHandle(B2L_STD_OUTPUT_HANDLE), Text, B2LTextLength(Text),
        &Written, 0);
}

static char
B2LHexDigit(UCHAR Value)
{
    return Value < 10U ? (char)('0' + Value) : (char)('A' + Value - 10U);
}

static void
B2LPrintHex64(ULONGLONG Value)
{
    char Text[19];
    ULONG Index;

    Text[0] = '0';
    Text[1] = 'x';
    for (Index = 0; Index < 16UL; ++Index) {
        Text[2UL + Index] = B2LHexDigit(
            (UCHAR)(Value >> ((15UL - Index) * 4UL)) & 0x0FU);
    }
    Text[18] = 0;
    B2LPrint(Text);
}

static void
B2LPrintSample(const UCHAR *Bytes, DWORD ByteCount)
{
    char Line[(B2L_SAMPLE_BYTES * 3UL) + 3UL];
    DWORD Count;
    DWORD Index;

    Count = ByteCount < B2L_SAMPLE_BYTES ? ByteCount : B2L_SAMPLE_BYTES;
    for (Index = 0; Index < Count; ++Index) {
        Line[Index * 3UL] = B2LHexDigit((UCHAR)(Bytes[Index] >> 4));
        Line[Index * 3UL + 1UL] = B2LHexDigit(Bytes[Index] & 0x0FU);
        Line[Index * 3UL + 2UL] = ' ';
    }
    Line[Count * 3UL] = '\r';
    Line[Count * 3UL + 1UL] = '\n';
    Line[Count * 3UL + 2UL] = 0;
    B2LPrint(Line);
}

static BOOL
B2LWriteSnapshot(LPCWSTR Path, const UCHAR *Bytes, DWORD ByteCount)
{
    HANDLE File;
    DWORD Written;
    BOOL Result;

    File = CreateFileW(
        Path, B2L_GENERIC_WRITE, B2L_FILE_SHARE_READ, 0,
        B2L_CREATE_ALWAYS, B2L_FILE_ATTRIBUTE_NORMAL, 0);
    if (File == B2L_INVALID_HANDLE_VALUE) {
        return 0;
    }
    Written = 0;
    Result = WriteFile(File, Bytes, ByteCount, &Written, 0);
    (void)CloseHandle(File);
    return Result && Written == ByteCount;
}

static BOOL
B2LReadRegion(
    HANDLE Disk,
    ULONGLONG Offset,
    DWORD ByteCount,
    LPCWSTR OutputPath,
    const char *Label)
{
    B2L_LARGE_INTEGER Position;
    DWORD Returned;

    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(Disk, Position, 0, B2L_FILE_BEGIN)) {
        B2LPrint("B2LGPT ERROR seek ");
        B2LPrint(Label);
        B2LPrint("\r\n");
        return 0;
    }
    Returned = 0;
    if (!ReadFile(Disk, B2LBuffer, ByteCount, &Returned, 0) ||
        Returned != ByteCount) {
        B2LPrint("B2LGPT ERROR read ");
        B2LPrint(Label);
        B2LPrint("\r\n");
        return 0;
    }

    B2LPrint("B2LGPT ");
    B2LPrint(Label);
    B2LPrint(" offset=");
    B2LPrintHex64(Offset);
    B2LPrint(" first64=");
    B2LPrintSample(B2LBuffer, Returned);
    if (!B2LWriteSnapshot(OutputPath, B2LBuffer, ByteCount)) {
        B2LPrint("B2LGPT ERROR snapshot write ");
        B2LPrint(Label);
        B2LPrint("\r\n");
        return 0;
    }
    return 1;
}

void
mainCRTStartup(void)
{
    HANDLE Disk;
    B2L_GET_LENGTH_INFORMATION LengthInfo;
    DWORD Returned;
    DWORD Failures;
    ULONGLONG DiskBytes;

    B2LPrint("[B2LGPT raw PhysicalDrive0 cross-check]\r\n");
    Disk = CreateFileW(
        L"\\\\.\\PhysicalDrive0", B2L_GENERIC_READ,
        B2L_FILE_SHARE_READ | B2L_FILE_SHARE_WRITE, 0,
        B2L_OPEN_EXISTING, B2L_FILE_ATTRIBUTE_NORMAL, 0);
    if (Disk == B2L_INVALID_HANDLE_VALUE) {
        B2LPrint("B2LGPT ERROR open PhysicalDrive0\r\n");
        ExitProcess(2);
    }

    Returned = 0;
    if (!DeviceIoControl(
            Disk, B2L_IOCTL_DISK_GET_LENGTH_INFO, 0, 0,
            &LengthInfo, sizeof(LengthInfo), &Returned, 0) ||
        Returned < sizeof(LengthInfo) ||
        LengthInfo.Length.QuadPart < (LONGLONG)(2UL * B2L_BLOCK_BYTES)) {
        B2LPrint("B2LGPT ERROR IOCTL_DISK_GET_LENGTH_INFO\r\n");
        (void)CloseHandle(Disk);
        ExitProcess(3);
    }
    DiskBytes = (ULONGLONG)LengthInfo.Length.QuadPart;
    B2LPrint("B2LGPT disk-bytes=");
    B2LPrintHex64(DiskBytes);
    B2LPrint(" block-bytes=0x0000000000001000\r\n");

    Failures = 0;
    if (!B2LReadRegion(
            Disk, 0, B2L_BLOCK_BYTES, L"X:\\B2LGPT-PMBR.BIN", "PMBR")) {
        ++Failures;
    }
    if (!B2LReadRegion(
            Disk, B2L_BLOCK_BYTES, B2L_BLOCK_BYTES,
            L"X:\\B2LGPT-PRIMARY.BIN", "PRIMARY")) {
        ++Failures;
    }
    if (!B2LReadRegion(
            Disk, 2ULL * B2L_BLOCK_BYTES, B2L_ENTRY_BYTES,
            L"X:\\B2LGPT-ENTRIES.BIN", "ENTRIES")) {
        ++Failures;
    }
    if (!B2LReadRegion(
            Disk, DiskBytes - B2L_BLOCK_BYTES, B2L_BLOCK_BYTES,
            L"X:\\B2LGPT-BACKUP.BIN", "BACKUP")) {
        ++Failures;
    }
    (void)CloseHandle(Disk);
    B2LPrint(Failures == 0 ?
        "B2LGPT RESULT=OK\r\n" : "B2LGPT RESULT=FAILED\r\n");
    ExitProcess(Failures == 0 ? 0 : 4);
}
