/** @file
  B2LSehDxe - Samsung EL3 monitorunun "Smart Exception Handler" kancasi.

  ---------------------------------------------------------------------------
  NEDEN
  ---------------------------------------------------------------------------
  Kirk turluk elemeden sonra geriye tek aktor kaldi: EL3.

    - Windows sifirlamayi ISTEMIYOR (ResetSystem sayaci 0, PSCI kapaliyken de)
    - Bugcheck degil (dogrulanmis geri cagri, iki yonde de)
    - Cekirdek son ana kadar saglikli (kalp atisi duzenli)
    - Acilisi bloke edince HIC olmuyor
    - Donanim watchdog'lari kapali (WTCON okundu, ikisi de 0)
    - Guc kesilmiyor (DRAM hayatta, RST_STAT 0x20000000)

  Stok boot.img'den ve ianmacd/d2s kaynagindan (d2s = Galaxy S10+ Exynos,
  tam olarak bizim cihaz) mekanizma bulundu:

    drivers/soc/samsung/exynos-el3_mon.c
      static void exynos_smart_exception_handler(
          unsigned int id, unsigned long elr, unsigned long esr,
          unsigned long sctlr, unsigned long ttbr, unsigned long tcr,
          unsigned long x6, unsigned int offset);

      ret = exynos_smc(SMC_CMD_SET_SEH_ADDRESS, addr, 0, 0);
      arch_initcall(exynos_set_seh_address);

  EL3 monitoru BEKLENMEDIK BIR ISTISNA ALDIGINDA, cekirdegin kaydettigi bu
  adrese atliyor ve EL3 baglamini argüman olarak veriyor. Samsung bunu
  arch_initcall'da, acilisin cok erken bir noktasinda kaydediyor.

  Kaydedilmemisse EL3'un atlayacak yeri yok. Bir guvenlik monitorunun baska
  secenegi de yok: SIFIRLAR. Sessizce, isletim sistemine haber vermeden.

  Windows bu SMC'yi asla yapmayacak. Biz yapiyoruz.

  ---------------------------------------------------------------------------
  NEDEN ASSEMBLY GEREKMIYOR
  ---------------------------------------------------------------------------
  Isleyici sekiz argumani x0-x7'de aliyor - AArch64 C cagri kuralinin (AAPCS64)
  tam olarak aynisi. Linux'unki de duz bir C fonksiyonu. Dolayisiyla normal bir
  C fonksiyonu dogru degerleri aliyor.

  Yine de govde MINIMUM tutuluyor: yalnizca global bir isaretci uzerinden
  yazma, fonksiyon cagrisi yok. EL3'ten donuldugunde yigin durumu belirsiz.

  ---------------------------------------------------------------------------
  SANAL ADRES MESELESI
  ---------------------------------------------------------------------------
  Linux SANAL adres kaydediyor; EL3 cekirdegin ceviri duzeninde eret yapiyor.
  Yani adres, o an gecerli EL1 sanal adres uzayinda anlamli olmali.

    UEFI icinde       -> kimlik haritalama, fiziksel adres yeterli
    Windows devralinca -> SetVirtualAddressMap sonrasi bizim calisma zamani
                          kodumuz Windows tarafindan haritalaniyor

  Bu yuzden SEH'i IKI KEZ kaydediyoruz: bir kez DriverEntry'de, bir kez de
  gEfiEventVirtualAddressChange olayinda YENI adresle. Ikinci kayit sart -
  yoksa Windows devraldigi anda EL3'un elindeki adres cope donuyor.

  Bu duzenek B2LRtHookDxe'de zaten kanitlandi (GetVariable 372 girip 372
  donuyor).

  ---------------------------------------------------------------------------
  KAYIT YERLESIMI - hepsi B2L RT Log bolgesinin (0xCF040000, 64 KB) icinde
  ---------------------------------------------------------------------------
    0xCF040000  B2LRtHookDxe'nin kaydi (ilk 0x40 bayt)
    0xCF041000  SEH yakalama kaydi        <- bu surucu
    0xCF041080  kayit durumu (SMC donusleri)
    0xCF042000  EL3'un GPR dokumu icin 2 sayfa (SMC_CMD_SET_DEBUG_MEM)

  SET_DEBUG_MEM FIZIKSEL adres aliyor (Linux virt_to_phys ile veriyor), yani
  orada ceviri derdi yok.
**/

#include <Uefi.h>

#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiDriverEntryPoint.h>

//
// include/linux/smc.h (ianmacd/d2s). Samsung eski usul NEGATIF komut
// kimlikleri kullaniyor; 64 bite isaret genisletiliyor.
//
#define SMC_CMD_SET_DEBUG_MEM    ((UINT64)(INT64)-120)
#define SMC_CMD_SET_SEH_ADDRESS  ((UINT64)(INT64)-123)

#define SEH_LOG      0xCF041000ULL   // yakalanan EL3 baglami
#define SEH_STATUS   0xCF041080ULL   // SMC donusleri
#define SEH_METIN    0xCF0410C0ULL   // SMC donusleri, okunabilir ASCII
#define SEH_DBGMEM   0xCF042000ULL   // EL3'un GPR dokumu, 2 sayfa
#define SEH_DBGSIZE  0x2000ULL

STATIC UINT64   *mSehLog;
STATIC UINT32   *mStatus;
STATIC UINT8    *mMetin;

/**
  SMC donuslerini ASCII yaz.

  NEDEN: ilk denemede kayit alaninda yalnizca "SEHR" gorunuyordu ve asil
  onemli iki sayiyi (SMC donusleri) bulanik bir fotograftan hex olarak
  cozmek gerekiyordu. Ayni dersi TIK= sayacinda da almistik: dmem'in sag
  sutunundaki ASCII gorunumu bedava, kullanmamak sacma.

  Ekranda tam olarak su okunacak:  DBG=00002000 SEH=00000000 SV=00000000
**/
STATIC
VOID
HexYaz (
  IN UINT8  *Hedef,
  IN UINT32  Deger)
{
  INTN I;

  for (I = 7; I >= 0; I--) {
    UINT8 D = (UINT8)(Deger & 0xF);
    Hedef[I] = (UINT8)((D < 10) ? ('0' + D) : ('A' + D - 10));
    Deger >>= 4;
  }
}
STATIC EFI_EVENT mVirtualAddressChangeEvent;

/**
  EL3 monitoru beklenmedik bir istisna alinca BURAYA atliyor.

  Argüman sirasi exynos_smart_exception_handler ile birebir:
    Id     0 ise istisna EL3 monitorunun kendisinden, degilse bir LDFW'den
    Elr    elr_el3   - istisnanin olustugu adres
    Esr    esr_el3   - ISTISNA SINIFI. Aradigimiz asil deger.
    Sctlr  sctlr_el3
    Ttbr   ttbr_el3
    Tcr    tcr_el3
    X6     scr_el3
    Offset GPR dokumunun debug_mem icindeki konumu

  DONMUYOR. Kasitli: cihaz sifirlanmasin, ekranda donup kalsin. O anda
  b2lfb'nin cizdigi seritler ekranda duruyor olacak, yani kacinci saniyede
  oldugunu da goruyoruz.
**/
STATIC
VOID
EFIAPI
SmartExceptionHandler (
  IN UINT64 Id,
  IN UINT64 Elr,
  IN UINT64 Esr,
  IN UINT64 Sctlr,
  IN UINT64 Ttbr,
  IN UINT64 Tcr,
  IN UINT64 X6,
  IN UINT64 Offset)
{
  //
  // Fonksiyon cagrisi YOK, yerel dizi YOK. EL3'ten donuldugunde yigin
  // durumu belirsiz; ne kadar az sey yaparsak o kadar iyi.
  //
  if (mSehLog != NULL) {
    mSehLog[0] = 0x0000002145534853ULL;   // "SEH!" + sifir dolgu
    mSehLog[1] = Id;
    mSehLog[2] = Elr;
    mSehLog[3] = Esr;
    mSehLog[4] = Sctlr;
    mSehLog[5] = Ttbr;
    mSehLog[6] = Tcr;
    mSehLog[7] = X6;
    mSehLog[8] = Offset;
  }

  //
  // Sonsuza kadar dur. Sifirlama olmasin ki ekran ve kayit yerinde kalsin.
  //
  for (;;) {
  }
}

/**
  SEH adresini EL3'e bildir ve donusu kaydet.

  Donus sifir degilse basarisiz. Bilinen degerler (exynos-el3_mon.h):
    1      EXYNOS_ERROR_ALREADY_INITIALIZED  - S-Boot zaten kaydetmis
    0x1000 EXYNOS_ERROR_NOT_VALID_ADDRESS
**/
STATIC
UINT32
SehKaydet (
  IN UINT64 Adres)
{
  ARM_SMC_ARGS Args;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_SET_SEH_ADDRESS;
  Args.Arg1 = Adres;
  Args.Arg2 = 0;
  Args.Arg3 = 0;

  ArmCallSmc (&Args);
  return (UINT32)Args.Arg0;
}

/**
  SetVirtualAddressMap sonrasi: isaretcileri cevir ve SEH'i YENIDEN kaydet.

  Ikinci kayit sart. Windows devraldigi anda EL3'un elindeki fiziksel adres
  artik gecerli degil; isleyicinin yeni sanal adresini bildirmezsek EL3
  istisna aninda copluge atlar - ki bu, olcmek istedigimiz seyi bizim
  uretmemiz olurdu.
**/
STATIC
VOID
EFIAPI
VirtualAddressChange (
  IN EFI_EVENT Event,
  IN VOID     *Context)
{
  VOID   *Handler;
  UINT32 *Status;

  Handler = (VOID *)SmartExceptionHandler;
  Status  = mStatus;

  EfiConvertPointer (0, (VOID **)&mSehLog);
  EfiConvertPointer (0, (VOID **)&mStatus);
  EfiConvertPointer (0, (VOID **)&mMetin);
  EfiConvertPointer (0, (VOID **)&Handler);

  //
  // Durum kaydina eski (henuz cevrilmemis) isaretciyle yaziyoruz cunku bu
  // olay HALA kimlik haritalamasinda calisiyor.
  //
  Status[3] = SehKaydet ((UINT64)(UINTN)Handler);
  Status[4] = (UINT32)((UINT64)(UINTN)Handler);
  Status[5] = (UINT32)(((UINT64)(UINTN)Handler) >> 32);

  HexYaz (mMetin + 26, Status[3]);
}

EFI_STATUS
EFIAPI
B2LSehEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS   Status;
  ARM_SMC_ARGS Args;

  mSehLog = (UINT64 *)(UINTN)SEH_LOG;
  mStatus = (UINT32 *)(UINTN)SEH_STATUS;
  mMetin  = (UINT8  *)(UINTN)SEH_METIN;

  ZeroMem (mSehLog, 0x80);
  ZeroMem (mStatus, 0x40);
  ZeroMem (mMetin, 0x40);
  ZeroMem ((VOID *)(UINTN)SEH_DBGMEM, SEH_DBGSIZE);

  mStatus[0] = 0x52484553;   // "SEHR" - surucu yuklendi

  //
  // ASCII sablon. Rakamlar asagida dolduruluyor.
  //
  CopyMem (mMetin, "DBG=xxxxxxxx SEH=xxxxxxxx SV=xxxxxxxx  ", 39);

  //
  // 1) EL3'e GPR dokumu icin bellek ver. FIZIKSEL adres istiyor, o yuzden
  //    ceviri derdi yok. Linux'ta dogru donus degeri BOYUTUN KENDISI.
  //
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_SET_DEBUG_MEM;
  Args.Arg1 = SEH_DBGMEM;
  Args.Arg2 = SEH_DBGSIZE;
  Args.Arg3 = 0;
  ArmCallSmc (&Args);
  mStatus[1] = (UINT32)Args.Arg0;
  HexYaz (mMetin + 4, mStatus[1]);

  //
  // 2) SEH adresi. Su an kimlik haritalamasindayiz, fiziksel adres gecerli.
  //
  mStatus[2] = SehKaydet ((UINT64)(UINTN)SmartExceptionHandler);
  HexYaz (mMetin + 17, mStatus[2]);

  DEBUG ((EFI_D_ERROR, "B2LSeh: dbgmem=0x%x seh=0x%x\n", mStatus[1], mStatus[2]));

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  VirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddressChangeEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "B2LSeh: sanal adres olayi kurulamadi: %r\n", Status));
  }

  return EFI_SUCCESS;
}
