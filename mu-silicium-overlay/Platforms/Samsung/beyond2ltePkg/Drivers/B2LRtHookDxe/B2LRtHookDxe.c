/** @file
  B2LRtHookDxe - UEFI calisma zamani servislerine giris/cikis isareti koyar.

  ---------------------------------------------------------------------------
  NEDEN
  ---------------------------------------------------------------------------
  Kirk turluk elemeden sonra geriye cok dar bir alan kaldi. Sifirlama:

    - Windows'un acilis surucilerinden SONRAKI ilerlemesine bagli
      (DriverEntry'de bloke edince hic olmuyor)
    - Birakildiktan sonra TAM 10 SANIYE surüyor (REL=0053 / TIK=0063)
    - Temiz bir yazilim sifirlamasi (RST_STAT 0x20000000)
    - Bugcheck DEGIL (dogrulanmis geri cagri, hem olumlu hem olumsuz yonde)
    - Kullanici moduna hic cikilmiyor

  Elenenler: ACPI ad alani, acpi.sys, butun Start=0 surucileri, Start=1
  surucileri, storufs, BasicDisplay, bellek carveout'lari, watchdog, LPS0,
  ErrorControl, SMP tablolari, WinPE kabugu, boot menusu, depolama hizi.

  ---------------------------------------------------------------------------
  GOZDEN KACAN SATIR
  ---------------------------------------------------------------------------
  Cihazdaki "memmap" ciktisinda su vardi:

      MMIO : 0 Pages (0 Bytes)

  Hicbir aygit bolgemiz UEFI bellek haritasinda yok. ExitBootServices'e kadar
  sorun degil - orada her sey birebir haritali. Ama Windows
  SetVirtualAddressMap cagirdiktan sonra CALISMA ZAMANI SERVISLERI sanal
  adreslerde calisiyor, ve RUNTIME isaretlenmemis bir MMIO bolgesine dokunan
  bir calisma zamani surucusu artik bosluga yaziyor.

  Windows Faz 1'de sistem saatini kurmak icin GetTime() cagiriyor. Exynos'ta
  RTC PMIC'in icinde ve SPEEDY veriyolundan erisiliyor. Bozuk bir PMIC islemi
  cihazi ISLETIM SISTEMINE HABER VERMEDEN sifirlar - aradigimiz imzanin
  tamami.

  ---------------------------------------------------------------------------
  NE YAPIYOR
  ---------------------------------------------------------------------------
  gRT->GetTime ve gRT->GetVariable sarmalaniyor. Her cagrida once GIRIS
  isareti, orijinal cagri, sonra CIKIS isareti yaziliyor:

      0xCF040000  "B2LR"        surucu yuklendi
      +0x04       GetTime giris sayaci
      +0x08       GetTime cikis sayaci
      +0x0C       GetVariable giris sayaci
      +0x10       GetVariable cikis sayaci

  Sifirlamadan sonra okundugunda:

      giris > cikis   ->  SIFIRLAMA O CAGRININ ICINDE OLDU. Bulduk.
      giris == cikis  ->  cagrilar donuyor, sifirlama baska yerde
      hepsi sifir     ->  Windows bu servisleri hic cagirmiyor

  ---------------------------------------------------------------------------
  KAYIT NEDEN 0xCF040000
  ---------------------------------------------------------------------------
  b2lfb kaydi 0xCF030000'da ve onun ustune yazmak istemiyoruz - ikisini yan
  yana okumak istiyoruz. "B2L EL2" bolgesi 0xCF000000 + 0x40000, yani
  0xCF040000 onun HEMEN DISINDA kaliyor; bolge 0x10000 buyutuluyor.

  ---------------------------------------------------------------------------
  SANAL ADRES DONUSUMU
  ---------------------------------------------------------------------------
  SetVirtualAddressMap'ten sonra isaretcilerimiz gecersizlesir. Kayit
  bolgesini ve orijinal fonksiyon isaretcilerini gEfiEventVirtualAddressChange
  olayinda EfiConvertPointer ile cevirmek ZORUNDA - yoksa ilk cagrida bizim
  kancamiz cokerdi ve olcumu biz uretmis olurduk.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiDriverEntryPoint.h>

#define B2L_RT_LOG  0xCF040000ULL

STATIC UINT32                      *mLog;
STATIC EFI_GET_TIME                 mOldGetTime;
STATIC EFI_GET_VARIABLE             mOldGetVariable;
STATIC EFI_EVENT                    mVirtualAddressChangeEvent;

STATIC
EFI_STATUS
EFIAPI
HookGetTime (
  OUT EFI_TIME              *Time,
  OUT EFI_TIME_CAPABILITIES *Capabilities OPTIONAL)
{
  EFI_STATUS Status;

  //
  // GIRIS. Once sayaci artiriyoruz ki cagri geri donmezse bile iz kalsin.
  // Sirasi kritik: once yaz, sonra cagir.
  //
  mLog[1]++;

  Status = mOldGetTime (Time, Capabilities);

  mLog[2]++;
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
HookGetVariable (
  IN     CHAR16   *VariableName,
  IN     EFI_GUID *VendorGuid,
  OUT    UINT32   *Attributes OPTIONAL,
  IN OUT UINTN    *DataSize,
  OUT    VOID     *Data OPTIONAL)
{
  EFI_STATUS Status;

  mLog[3]++;

  Status = mOldGetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);

  mLog[4]++;
  return Status;
}

/**
  SetVirtualAddressMap sonrasi isaretcileri cevir.

  Bu adim atlanirsa ilk cagrida kancanin kendisi coker ve olcumu biz
  uretmis oluruz. B2LResetHookDxe'de ayni tuzak vardi.
**/
STATIC
VOID
EFIAPI
VirtualAddressChange (
  IN EFI_EVENT Event,
  IN VOID     *Context)
{
  EfiConvertPointer (0, (VOID **)&mLog);
  EfiConvertPointer (0, (VOID **)&mOldGetTime);
  EfiConvertPointer (0, (VOID **)&mOldGetVariable);
}

EFI_STATUS
EFIAPI
B2LRtHookEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  mLog = (UINT32 *)(UINTN)B2L_RT_LOG;

  //
  // Kaydi temizle ve imzayi koy. "B2LR" = 0x524C3242 (kucuk endian).
  //
  ZeroMem (mLog, 0x40);
  mLog[0] = 0x524C3242;

  mOldGetTime     = gRT->GetTime;
  mOldGetVariable = gRT->GetVariable;

  gRT->GetTime     = HookGetTime;
  gRT->GetVariable = HookGetVariable;

  //
  // CRC32 YENIDEN HESAPLANMALI. Runtime tablosu degistirildiginde basligin
  // CRC'si gecersiz kaliyor; bazi isletim sistemleri bunu dogruluyor ve
  // tabloyu reddediyor.
  //
  gRT->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &gRT->Hdr.CRC32);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  VirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddressChangeEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "B2LRtHook: sanal adres olayi kurulamadi: %r\n", Status));
  }

  DEBUG ((EFI_D_ERROR, "B2LRtHook: GetTime ve GetVariable kancalandi\n"));
  return EFI_SUCCESS;
}
