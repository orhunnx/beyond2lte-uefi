/*
 * b2lsys - WinPE SERVICE_SYSTEM_START siniri icin en kucuk isaretci.
 *
 * Bu ayri bir Start=1 kernel surucusudur. Windows'un belgelenmis yukleme
 * sirasinda PnP aygit suruculerinden sonra, kalan SERVICE_SYSTEM_START
 * suruculerin yuklendigi sistem ilklendirmesi asamasinda DriverEntry'ye
 * girer. Start=2 / Service Control Manager asamasindan ONCEDIR.
 *
 * DriverEntry callback, thread, timer, framebuffer veya PnP islemi
 * kullanmaz: b2lfb'nin ayirdigi kayda bir satir yazar ve basariyla doner.
 *
 * +0xB0:
 *   SYS=REG C=000  b2lfb yazdi; Start=1 asamasi henuz gorulmedi
 *   SYS=INI C=001  bu surucu sistem ilklendirmesinde baslatildi
 */

typedef long               NTSTATUS;
typedef unsigned char      UINT8;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef void              *PVOID;

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define MmNonCached     0
#define LOG_PHYS        0xCF030000ULL
#define LOG_BYTES       0x100ULL

typedef union _PHYSICAL_ADDRESS {
  struct {
    UINT32 LowPart;
    int    HighPart;
  } u;
  long long QuadPart;
} PHYSICAL_ADDRESS;

__declspec(dllimport) PVOID MmMapIoSpace (
  PHYSICAL_ADDRESS PhysicalAddress,
  UINT64           NumberOfBytes,
  UINT32           CacheType);
__declspec(dllimport) void MmUnmapIoSpace (
  PVOID  BaseAddress,
  UINT64 NumberOfBytes);

NTSTATUS DriverEntry (PVOID DriverObject, PVOID RegistryPath)
{
  PHYSICAL_ADDRESS Pa;
  volatile UINT8  *Log;

  (void)DriverObject;
  (void)RegistryPath;

  Pa.QuadPart = (long long)LOG_PHYS;
  Log = (volatile UINT8 *)MmMapIoSpace (Pa, LOG_BYTES, MmNonCached);
  if (Log != 0) {
    Log += 176;
    Log[0]  = 'S'; Log[1]  = 'Y'; Log[2]  = 'S'; Log[3]  = '=';
    Log[4]  = 'I'; Log[5]  = 'N'; Log[6]  = 'I'; Log[7]  = ' ';
    Log[8]  = 'C'; Log[9]  = '='; Log[10] = '0'; Log[11] = '0'; Log[12] = '1';
    Log[13] = ' '; Log[14] = ' '; Log[15] = ' ';
    MmUnmapIoSpace ((PVOID)(Log - 176), LOG_BYTES);
  }

  return STATUS_SUCCESS;
}
