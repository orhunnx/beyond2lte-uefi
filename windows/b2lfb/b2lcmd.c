/*
 * B2LCMD - WinPE'nin cmd.exe/startnet asamasina ulastigini kanitlayan
 * en kucuk ARM64 kullanici programi.
 *
 * Ekrana, diske veya aygit surucusune dokunmaz. Yalnizca adlandirilmis
 * Global\\B2LCmd olayini yaratir ve cikar. b2lfb.sys bu olayi kendi dusuk
 * oncelikli sistem thread'inden gorunce 0xCF030000+0xD0'a
 * "USR=CMD C=001" yazar. Dolayisiyla sonraki UEFI ekraninda bu satir varsa
 * Win32 alt sistemi, cmd.exe ve startnet.cmd gercekten calismistir.
 */

typedef void       *PVOID;
typedef void       *HANDLE;
typedef unsigned long DWORD;
/* Freestanding C hedefinde stddef.h yok; Windows wchar_t 16 bittir. */
typedef unsigned short wchar_t;
typedef const wchar_t *LPCWSTR;

__declspec(dllimport) HANDLE CreateEventW (
  PVOID SecurityAttributes,
  int   ManualReset,
  int   InitialState,
  LPCWSTR Name);
__declspec(dllimport) int  CloseHandle (HANDLE Object);
__declspec(dllimport) void Sleep (DWORD Milliseconds);
__declspec(dllimport) void ExitProcess (DWORD ExitCode);

void mainCRTStartup (void)
{
  HANDLE Olay;

  Olay = CreateEventW (0, 1, 0, L"Global\\B2LCmd");
  if (Olay != 0) {
    /* b2lfb saniyede bir yoklar; olay yok olmadan gorulebilsin. */
    Sleep (3000);
    CloseHandle (Olay);
    ExitProcess (0);
  }

  ExitProcess (1);
}
