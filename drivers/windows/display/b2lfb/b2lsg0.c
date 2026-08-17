/*
 * b2lsg0 - sirali SERVICE_SYSTEM_START grubu icin kucuk isaretci.
 *
 * Hizmet kaydi Start=1 ve Group="System Bus Extender" yapilir. Bu grup
 * b2lfb'nin Start=0 kaydinda zaten kullanilan yerlesik, erken bir Windows
 * yukleme grubudur. b2lsys grupsuz oldugu icin ondan onceki grup
 * suruculerinin yuklenmesi sirasinda reset olup olmadigini ayirt etmek icin
 * ayri tutulur.
 *
 * DriverEntry callback, thread, timer, framebuffer veya PnP islemi
 * kullanmaz: sicak-sifirlama kaydina yazar ve STATUS_SUCCESS doner.
 *
 * +0xC0:
 *   SYG=REG C=000  b2lfb yazdi; bu sirali Start=1 grubuna gelinmedi
 *   SYG=INI C=001  System Bus Extender grubundeki bu surucu baslatildi
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
    Log += 192;
    Log[0]  = 'S'; Log[1]  = 'Y'; Log[2]  = 'G'; Log[3]  = '=';
    Log[4]  = 'I'; Log[5]  = 'N'; Log[6]  = 'I'; Log[7]  = ' ';
    Log[8]  = 'C'; Log[9]  = '='; Log[10] = '0'; Log[11] = '0'; Log[12] = '1';
    Log[13] = ' '; Log[14] = ' '; Log[15] = ' ';
    MmUnmapIoSpace ((PVOID)(Log - 192), LOG_BYTES);
  }

  return STATUS_SUCCESS;
}
