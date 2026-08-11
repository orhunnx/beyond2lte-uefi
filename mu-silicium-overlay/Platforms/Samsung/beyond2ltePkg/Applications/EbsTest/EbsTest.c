/** @file
  EbsTest - ExitBootServices'ten sonra hicbir sey yapmayan bir kalp atisi.

  ----------------------------------------------------------------------------
  NEDEN
  ----------------------------------------------------------------------------
  Windows ~12-15 saniye sonra sessizce sifirlaniyor. Bugune kadar sorunun
  Windows'ta oldugunu varsaydik ve ACPI'yi didik didik ettik: MADT, GTDT,
  FADT, PPTT, DSDT - hepsi stock DTB'ye karsi dogrulandi, hepsi dogru cikti.
  Kanca hic atesmedi, hicbir log kanali calismadi, bilinen iyi noktaya
  donmek bile davranisi geri getirmedi.

  Hic sormadigimiz soru su: SIFIRLAMA WINDOWS'LA ILGILI MI?

  Windows'un yaptigi ozel sey ExitBootServices cagirip platformu devralmak.
  Linux da bunu yapiyor ve 120 saniye yasiyor - AMA Linux zamanlayiciyi,
  GIC'i, kesme yonlendirmesini, hepsini kuruyor. Arada ucuncu bir durum var
  ve hic denenmedi:

      ExitBootServices cagir, SONRA HICBIR SEY KURMA.

  Bu uygulama tam olarak onu yapiyor. Boot servisleri kapandiktan sonra
  geriye yalnizca framebuffer'a yazan bir dongu kaliyor. Ne zamanlayici, ne
  kesme, ne surucu.

  ----------------------------------------------------------------------------
  OKUMA
  ----------------------------------------------------------------------------
  Cihaz 15 saniyede SIFIRLARSA -> sorun Windows'ta DEGIL. Platform,
      ExitBootServices'ten sonra servis edilmeyen bir seye takiliyor ve
      Linux onu tesadufen tatmin ediyor. Bu, projenin yonunu tamamen
      degistirir: ACPI'de arayacak bir sey kalmaz.

  Dakikalarca YASARSA -> sifirlama gercekten Windows'a ozel. ACPI ve surucu
      tarafina odaklanmaya devam ederiz, ama artik bunu BILEREK.

  ----------------------------------------------------------------------------
  KENDI KENDINI ZAMANLIYOR
  ----------------------------------------------------------------------------
  ExitBootServices'ten sonra Print() yok, gBS yok, konsol yok. O yuzden
  sayaci EKRANA CIZIYORUZ: her tik bir kare. Cihaz sifirlandiginda ekranda
  kalan kare sayisi, gecen saniyeyi veriyor. Fotografini cekip saymak
  yetiyor - kronometreye gerek yok ve "sanirim 15 saniye" belirsizligi
  ortadan kalkiyor.

  Ust serit ayrica her tikte renk degistiriyor: ekran donarsa (sifirlama
  degil, kilitlenme) bunu da ayirt edebiliyoruz.
**/

#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/GraphicsOutput.h>

#define KARE      24    // kare kenari, piksel
#define BOSLUK     4
#define UST_SERIT 40

STATIC UINT32 *mFb;
STATIC UINTN   mPps;
STATIC UINTN   mW;
STATIC UINTN   mH;

STATIC
VOID
Doldur (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Genislik,
  IN UINTN  Yukseklik,
  IN UINT32 Renk)
{
  UINTN Sx, Sy;

  for (Sy = Y; (Sy < Y + Yukseklik) && (Sy < mH); Sy++) {
    for (Sx = X; (Sx < X + Genislik) && (Sx < mW); Sx++) {
      mFb[Sy * mPps + Sx] = Renk;
    }
  }
}

/**
  Kaba gecikme. Zamanlayici yok, kesme yok - elimizde yalnizca mesgul dongu
  var. Sabit deneysel: cihazda bir kare yaklasik bir saniye surecek sekilde
  ayarlanacak. Tam olmasi gerekmiyor; onemli olan kare SAYISININ denemeler
  arasinda karsilastirilabilir olmasi.
**/
STATIC
VOID
Bekle (
  VOID)
{
  volatile UINT64 Index;

  for (Index = 0; Index < 8000000ULL; Index++) {
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop      = NULL;
  EFI_MEMORY_DESCRIPTOR        *Harita   = NULL;
  UINTN                         Boyut    = 0;
  UINTN                         Anahtar  = 0;
  UINTN                         TanimBoy = 0;
  UINT32                        Surum    = 0;
  EFI_STATUS                    Status;
  UINTN                         Tik;
  UINTN                         Sutun;
  UINTN                         Satir;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL)) {
    Print (L"GOP bulunamadi, test yapilamaz.\n");
    return Status;
  }

  mFb  = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  mPps = Gop->Mode->Info->PixelsPerScanLine;
  mW   = Gop->Mode->Info->HorizontalResolution;
  mH   = Gop->Mode->Info->VerticalResolution;

  Print (L"\n");
  Print (L"EbsTest: ExitBootServices cagrilacak, sonra HICBIR SEY kurulmayacak.\n");
  Print (L"Her kare yaklasik bir saniye. Cihaz sifirlanirsa kareleri say.\n");
  Print (L"Framebuffer %ux%u @ 0x%lx\n", (UINT32)mW, (UINT32)mH, (UINT64)(UINTN)mFb);
  Print (L"\n");

  //
  // Bellek haritasini al ve cik. Iki kez deniyoruz: ilk GetMemoryMap'ten
  // sonra AllocatePool haritayi degistirebiliyor ve MapKey bayatliyor.
  //
  Status = gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum);
  Boyut += 8 * TanimBoy;
  Status = gBS->AllocatePool (EfiLoaderData, Boyut, (VOID **)&Harita);
  if (EFI_ERROR (Status)) {
    Print (L"AllocatePool basarisiz: %r\n", Status);
    return Status;
  }

  Status = gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum);
  if (EFI_ERROR (Status)) {
    Print (L"GetMemoryMap basarisiz: %r\n", Status);
    return Status;
  }

  Status = gBS->ExitBootServices (ImageHandle, Anahtar);
  if (EFI_ERROR (Status)) {
    //
    // Anahtar bayatladiysa bir kez daha. Basarisiz olursa Print hala
    // calisiyor, cunku boot servisleri kapanmadi.
    //
    Boyut  = 0;
    Harita = NULL;
    gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum);
    Boyut += 8 * TanimBoy;
    gBS->AllocatePool (EfiLoaderData, Boyut, (VOID **)&Harita);
    gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum);
    Status = gBS->ExitBootServices (ImageHandle, Anahtar);
    if (EFI_ERROR (Status)) {
      Print (L"ExitBootServices basarisiz: %r\n", Status);
      return Status;
    }
  }

  //
  // BURADAN SONRA HICBIR SERVIS YOK. Ne gBS, ne Print, ne konsol.
  // Yalnizca framebuffer'a yaziyoruz.
  //
  Doldur (0, 0, mW, mH, 0x00000000);

  Tik = 0;
  for (;;) {
    // Ust serit her tikte renk degistiriyor: dongu donuyor mu, gorunur olsun.
    Doldur (0, 0, mW, UST_SERIT, ((Tik & 1) != 0) ? 0x00202020 : 0x00808080);

    // Sayac: her tik bir kare, soldan saga, satir satir.
    Sutun = Tik % ((mW - BOSLUK) / (KARE + BOSLUK));
    Satir = Tik / ((mW - BOSLUK) / (KARE + BOSLUK));
    Doldur (
      BOSLUK + Sutun * (KARE + BOSLUK),
      UST_SERIT + BOSLUK + Satir * (KARE + BOSLUK),
      KARE,
      KARE,
      0x0000FF00);

    Bekle ();
    Tik++;
  }
}
