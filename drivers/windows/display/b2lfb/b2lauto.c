/*
 * b2lauto - WinPE Service Control Manager siniri icin en kucuk isaretci.
 *
 * Bu ayri bir Start=2 kernel surucusudur. Microsoft'un surucu yukleme
 * sirasinda Start=0/1 suruculerden sonra Service Control Manager'in
 * auto-start asamasinda baslatilir. DriverEntry'de callback, thread,
 * timer, framebuffer veya PnP islemi yoktur: yalnizca b2lfb'nin daha once
 * ayirdigi sicak-sifirlama kaydina bir satir yazar ve basariyla doner.
 *
 * +0xA0:
 *   AUT=REG C=000  b2lfb yazdi; auto asamasi henuz gorulmedi
 *   AUT=SCM C=001  bu surucu Service Control Manager tarafindan baslatildi
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
    Log += 160;
    Log[0]  = 'A'; Log[1]  = 'U'; Log[2]  = 'T'; Log[3]  = '=';
    Log[4]  = 'S'; Log[5]  = 'C'; Log[6]  = 'M'; Log[7]  = ' ';
    Log[8]  = 'C'; Log[9]  = '='; Log[10] = '0'; Log[11] = '0'; Log[12] = '1';
    Log[13] = ' '; Log[14] = ' '; Log[15] = ' ';
    MmUnmapIoSpace ((PVOID)(Log - 160), LOG_BYTES);
  }

  return STATUS_SUCCESS;
}
