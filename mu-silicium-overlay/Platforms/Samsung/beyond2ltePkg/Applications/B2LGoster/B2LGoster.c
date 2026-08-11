/** @file
  B2LGoster - tani kayitlarini BUYUK harflerle ekrana cizer.

  ---------------------------------------------------------------------------
  NEDEN
  ---------------------------------------------------------------------------
  Olcumleri simdiye kadar "dmem" ile okuduk ve her seferinde ayni iki sorun
  cikti: yazi 1440x3040 ekranda 8x19 piksel oldugu icin fotograftan zor
  seciliyor, ve onaltilik dokum satirlari 25 satirlik konsolu doldurup
  aradigimiz degeri yukari kaydiriyor. Ikincisi bize IKI TUR kaybettirdi.

  Konsolun yazi tipi buyutulemez: EFI_GLYPH_WIDTH/HEIGHT MdePkg'de sabit ve
  glif bitmapleri de o boyutta uretiliyor. Makroyu degistirmek yaziyi
  buyutmuyor, sadece harflerin arasini aciyor.

  Cozum: cizimi biz yapiyoruz. Glifleri HII font protokolunden aliyoruz -
  yani konsolun kullandigi yazi tipinin AYNISI, elle yazilmis bir font
  tablosuna gerek yok - ve her pikseli NxN blok olarak framebuffer'a
  basiyoruz.

  Ustelik onaltilik dokum yerine YORUMLANMIS satir yaziyoruz:

      SEH  KAYIT=00000000  DBG=FFFFFFFF
      SEH  YAKALANDI  ESR=96000010
      B2LFB  TIK=0063  BUG=H
      RST_STAT=20000000  YAZILIM

  ---------------------------------------------------------------------------
  ONEMLI: ONCEKI ACILIS KAYDINI OKUYOR
  ---------------------------------------------------------------------------
  B2LSehDxe her acilista canli kaydi (0xCF041000) temizliyor ve temizlemeden
  once bir onceki acilisin kopyasini 0xCF041100'e aliyor. DXE surucileri
  kabuktan ONCE calistigi icin canli kaydi okumak her zaman bos deger verir.
  Burada okunan KOPYA.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiImage.h>
#include <Protocol/HiiFont.h>

//
// B2LSehDxe'deki B2L_SURUM ile AYNI olmali. Onceki acilisin degerleri
// yalnizca damgalar tutuyorsa gosteriliyor.
//
#define B2L_SURUM    7

//
// Ekranin en ustundeki damga. Eski bir kopya calisirsa (birden fazla FAT
// biriminde B2LGOSTR.EFI olabiliyor - bu tur tam olarak oldu ve iki farkli
// ekran cikti) tek bakista anlasilsin diye.
//
#define SURUM_METNI  L"SURUM 7"

#define OLCEK        4      // her glif pikseli 4x4 blok
#define KENAR       24      // sol/ust bosluk

#define SEH_CANLI   0xCF041000ULL
#define SEH_DURUM   0xCF041080ULL
#define SEH_ONCEKI  0xCF041100ULL
#define DUR_ONCEKI  0xCF041180ULL
#define FB_KAYIT    0xCF030000ULL   // b2lfb (Windows surucusu)
#define RT_KAYIT    0xCF040000ULL   // B2LRtHookDxe
#define PMU_RST     0x15860404ULL

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC EFI_HII_FONT_PROTOCOL        *mFont;
STATIC UINT32                       *mFb;
STATIC UINT32                        mSatirBasi;   // piksel/satir (stride)
STATIC UINT32                        mSatir;       // o anki y

/**
  Tek karakter ciz. Glif HII'den geliyor, her piksel OLCEK x OLCEK blok.
**/
STATIC
VOID
Karakter (
  IN CHAR16  Ch,
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Renk)
{
  EFI_IMAGE_OUTPUT *Blt;
  UINT32            Px, Py, I, J;

  Blt = NULL;
  if (EFI_ERROR (mFont->GetGlyph (mFont, Ch, NULL, &Blt, NULL)) || Blt == NULL) {
    return;
  }

  for (Py = 0; Py < Blt->Height; Py++) {
    for (Px = 0; Px < Blt->Width; Px++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *P;

      P = &Blt->Image.Bitmap[Py * Blt->Width + Px];

      //
      // HII glifi on plani beyaz, arka plani siyah veriyor. Sifirdan farkli
      // her sey "harfin bir parcasi" sayiliyor - esik aramaya gerek yok.
      //
      if ((P->Red | P->Green | P->Blue) == 0) {
        continue;
      }

      for (J = 0; J < OLCEK; J++) {
        for (I = 0; I < OLCEK; I++) {
          mFb[(Y + Py * OLCEK + J) * mSatirBasi + (X + Px * OLCEK + I)] = Renk;
        }
      }
    }
  }

  if (Blt->Image.Bitmap != NULL) {
    gBS->FreePool (Blt->Image.Bitmap);
  }
  gBS->FreePool (Blt);
}

/**
  Bir satir yaz ve asagi in.
**/
STATIC
VOID
Yaz (
  IN CHAR16 *Metin,
  IN UINT32  Renk)
{
  UINT32 X;

  X = KENAR;
  while (*Metin != L'\0') {
    Karakter (*Metin, X, mSatir, Renk);
    X += 8 * OLCEK;
    Metin++;
  }

  mSatir += 19 * OLCEK + 4;
}

STATIC CHAR16 mTampon[64];

STATIC
VOID
Hex32 (
  IN OUT CHAR16 *Hedef,
  IN     UINT32  Deger)
{
  INTN I;

  for (I = 7; I >= 0; I--) {
    UINT8 D = (UINT8)(Deger & 0xF);
    Hedef[I] = (CHAR16)((D < 10) ? (L'0' + D) : (L'A' + D - 10));
    Deger >>= 4;
  }
}

/**
  "ONEK=XXXXXXXX" bicimini kurup yazdirir.
**/
STATIC
VOID
Deger (
  IN CHAR16 *Onek,
  IN UINT32  Sayi,
  IN CHAR16 *Not,
  IN UINT32  Renk)
{
  UINTN N;

  N = 0;
  while (Onek[N] != L'\0' && N < 32) {
    mTampon[N] = Onek[N];
    N++;
  }

  Hex32 (&mTampon[N], Sayi);
  N += 8;

  if (Not != NULL) {
    mTampon[N++] = L' ';
    mTampon[N++] = L' ';
    while (*Not != L'\0' && N < 60) {
      mTampon[N++] = *Not++;
    }
  }

  mTampon[N] = L'\0';
  Yaz (mTampon, Renk);
}

EFI_STATUS
EFIAPI
B2LGosterEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS      Status;
  UINT32         *Durum;
  UINT32         *Onc;
  UINT64         *Onceki;
  UINT32         *RtLog;
  UINT8          *FbLog;
  UINT32          Rst;
  EFI_INPUT_KEY   Key;
  UINTN           Index;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status)) {
    Print (L"B2LGoster: GOP yok: %r\n", Status);
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&mFont);
  if (EFI_ERROR (Status)) {
    Print (L"B2LGoster: HII font yok: %r\n", Status);
    return Status;
  }

  mFb        = (UINT32 *)(UINTN)mGop->Mode->FrameBufferBase;
  mSatirBasi = mGop->Mode->Info->PixelsPerScanLine;
  mSatir     = KENAR;

  //
  // Ekrani temizle. dmem ciktisinin ustune yazmak yerine bos sayfa: tek
  // amaci fotograftan tek bakista okunabilmesi.
  //
  SetMem32 (mFb, (UINTN)mSatirBasi * mGop->Mode->Info->VerticalResolution * 4, 0x00000000);

  Durum  = (UINT32 *)(UINTN)SEH_DURUM;
  Onc    = (UINT32 *)(UINTN)DUR_ONCEKI;
  Onceki = (UINT64 *)(UINTN)SEH_ONCEKI;
  RtLog  = (UINT32 *)(UINTN)RT_KAYIT;
  FbLog  = (UINT8  *)(UINTN)FB_KAYIT;
  Rst    = *(volatile UINT32 *)(UINTN)PMU_RST;

  Yaz (L"=== BEYOND2LTE TANI ===", 0x00FFFFFF);
  Yaz (SURUM_METNI, 0x00808080);

  //
  // BEYAN EDILEN RAM. Bir PCD degistirdik diye degistigini varsaymak
  // yeterli degil - Mu-Silicium DRAM boyutunu calisma zamaninda da
  // hesaplayabiliyor ve PCD'yi gormezden gelebilir. Deneyin bagimli
  // degiskeni degismediyse deney yapilmamis demektir.
  //
  {
    EFI_MEMORY_DESCRIPTOR *Harita;
    UINTN                  Boyut, Anahtar, TanimBoy, I;
    UINT32                 Surum2;
    UINT64                 Sayfa;
    UINT64                 Tepe;
    UINT32                 Parca;

    Harita = NULL;
    Boyut  = 0;
    Sayfa  = 0;
    Tepe   = 0;
    Parca  = 0;

    gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum2);
    Boyut += 8 * TanimBoy;

    if (!EFI_ERROR (gBS->AllocatePool (EfiLoaderData, Boyut, (VOID **)&Harita)) &&
        !EFI_ERROR (gBS->GetMemoryMap (&Boyut, Harita, &Anahtar, &TanimBoy, &Surum2))) {
      for (I = 0; I < Boyut / TanimBoy; I++) {
        EFI_MEMORY_DESCRIPTOR *D;

        D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Harita + I * TanimBoy);
        if (D->Type != EfiReservedMemoryType && D->Type != EfiMemoryMappedIO) {
          UINT64 Son;

          Sayfa += D->NumberOfPages;
          Parca++;

          Son = D->PhysicalStart + D->NumberOfPages * 4096;
          if (Son > Tepe) {
            Tepe = Son;
          }
        }
      }

      gBS->FreePool (Harita);
    }

    Deger (L"RAM MB  =", (UINT32)((Sayfa * 4) / 1024), NULL, 0x0060C0FF);

    //
    // EN YUKSEK RAM ADRESI - bu turun asil sorusu.
    //
    //   ~0x000280000000 -> UEFI 0x80000000'dan DUZ devam etmis. DTB'ye gore
    //                      0x100000000-0x880000000 arasi delik, yani
    //                      Windows'a var olmayan ~6 GB beyan ediliyor.
    //                      Ayirici oraya vardiginda veri yolu hatasi olur:
    //                      bugcheck yok, ResetSystem yok, temiz sifirlama.
    //   ~0x000980000000 -> bankalar dogru kurulmus, bellek aklanir.
    //
    Deger (L"TEPE HI =", (UINT32)(Tepe >> 32), NULL, 0x0060C0FF);
    Deger (L"TEPE LO =", (UINT32)Tepe, NULL, 0x0060C0FF);
    Deger (L"PARCA   =", Parca, NULL, 0x0060C0FF);
  }

  //
  // BANKA SINAMASI - BEYAN DEGIL OLCUM.
  //
  // Harita dort banka beyan ediyor (0x80000000, 0x880000000, 0x900000000,
  // 0x980000000; her biri 2 GB, tepe 0xA00000000). Bu beyanin DOGRU oldugunu
  // kimse olcmedi - ne biz ne UEFI. Beyan edilen bellegin bir kismi gercekte
  // yoksa ya da baska bir adrese takla atiyorsa (aliasing), Windows'un
  // ayiricisi oraya vardigi anda veri yolu hatasi olur: bugcheck yok,
  // ResetSystem yok, temiz yazilim sifirlamasi. Tam gordugumuz imza.
  //
  // Her bankanin sonuna FARKLI bir desen yaziyoruz, hepsini yazdiktan SONRA
  // hepsini okuyoruz. Tek tek yazip okumak aliasing'i kacirirdi: takla atan
  // bir adres kendi yazdigini geri verir. Sonuc bit maskesi:
  //
  //   0000000F -> dort banka da gercek ve birbirinden bagimsiz
  //   eksik bit -> o banka yok ya da baska bir yere takla atiyor
  //
  // Eski icerik saklanip geri yaziliyor; bu bellek su an bos olmayabilir.
  //
  {
    STATIC UINT64 CONST Adresler[4] = {
      0x0FFFF0000ULL, 0x8FFFF0000ULL, 0x97FFF0000ULL, 0x9FFFF0000ULL
    };

    volatile UINT64 *P;
    UINT64           Eski[4];
    UINT32           Maske;
    UINTN            K;

    for (K = 0; K < 4; K++) {
      P       = (volatile UINT64 *)(UINTN)Adresler[K];
      Eski[K] = *P;
      *P      = 0xB2B2000000000000ULL | (K + 1);
    }

    Maske = 0;
    for (K = 0; K < 4; K++) {
      P = (volatile UINT64 *)(UINTN)Adresler[K];
      if (*P == (0xB2B2000000000000ULL | (K + 1))) {
        Maske |= (1u << K);
      }
    }

    for (K = 0; K < 4; K++) {
      P  = (volatile UINT64 *)(UINTN)Adresler[K];
      *P = Eski[K];
    }

    Deger (L"BANKA   =", Maske,
           (Maske == 0xF) ? L"HEPSI GERCEK" : L"EKSIK VAR",
           (Maske == 0xF) ? 0x0040FF40 : 0x00FF4040);
  }
  mSatir += 12;

  //
  // 1) EL3 SEH - bu turun asil sorusu.
  //
  if (Durum[0] != 0x52484553) {
    Yaz (L"SEH SURUCUSU YUKLENMEDI", 0x00FF4040);
  } else {
    Yaz (L"SEH KAYDI SANALA BIRAKILDI", 0x00808080);
    //
    // SANAL degeri BU acilistan degil ONCEKINDEN okunuyor. Yazan olay
    // (SetVirtualAddressMap) kabuktan sonra calisiyor; bu acilisin degeri
    // her zaman "henuz calismadi" olur.
    //
    Deger (L"LOCKUP  =", Durum[6],
           (Durum[6] == 0xFFFFFFFFUL) ? L"TANIMIYOR" : L"CEVAP VAR",
           (Durum[6] == 0xFFFFFFFFUL) ? 0x00808080 : 0x0040FF40);
    Deger (L"DEBUG MEM=", Durum[1],
           (Durum[1] == 0x2000) ? L"KABUL" : L"RET",
           (Durum[1] == 0x2000) ? 0x0040FF40 : 0x00808080);

    //
    // ONCEKI ACILIS - yalnizca ayni surum yazdiysa. Damga tutmuyorsa
    // oradaki sayilar temizlenmemis DRAM olabilir ve gosterilmesi
    // olcum sanilir. Bu tur tam olarak o oldu.
    //
    if (Onc[0] == 0x52484553 && Onc[15] == B2L_SURUM) {
      //
      // Olayin calistigi SMC'den BAGIMSIZ damgayla dogrulaniyor. Onceki
      // turda bunu donus degerinden cikarmaya calistik ve nobetci deger
      // EL3'un cevabiyla cakisti.
      //
      if (Onc[12] != 0x4E414C53UL) {
        Yaz (L"SANAL OLAY HIC CALISMADI", 0x00FF4040);
      } else {
        Deger (L"SANAL ONC=", Onc[3],
               (Onc[3] == 0) ? L"KABUL" : L"RET",
               (Onc[3] == 0) ? 0x0040FF40 : 0x00FF4040);
      }
      Deger (L"LOCKUP ONC=", Onc[6], NULL, 0x00C0C0C0);

      //
      // Sanal kayit reddedildiyse SEBEBI burada ayrisiyor:
      //   CONV != 0        -> ceviri hic olmadi, EL3'e ayni fiziksel
      //                       adresi ikinci kez verdik
      //   CONV == 0 ama VA  -> ceviri oldu, adresi EL3 begenmedi
      // Ustteki iki durum bambaska iki isi gerektiriyor.
      //
      Deger (L"CONV LOG =", Onc[10], (Onc[10] == 0) ? L"OK" : L"HATA", 0x00C0C0C0);
      Deger (L"CONV FN  =", Onc[11], (Onc[11] == 0) ? L"OK" : L"HATA", 0x00C0C0C0);
      Deger (L"HANDLR HI=", Onc[5], NULL, 0x00C0C0C0);
      Deger (L"HANDLR LO=", Onc[4], NULL, 0x00C0C0C0);
    } else {
      Yaz (L"ONCEKI ACILIS BASKA SURUM", 0x00808080);
    }
  }

  mSatir += 12;

  //
  // 2) Onceki acilistan kalan EL3 istisnasi. ARADIGIMIZ SEY.
  //
  if (Onceki[0] == 0x0000002145534853ULL) {
    Yaz (L"*** EL3 ISTISNASI YAKALANDI ***", 0x0040FF40);
    Deger (L"ESR EL3 =", (UINT32)Onceki[3], NULL, 0x0040FF40);
    Deger (L"EC      =", (UINT32)(Onceki[3] >> 26) & 0x3F, NULL, 0x0040FF40);
    Deger (L"ELR LO  =", (UINT32)Onceki[2], NULL, 0x00C0C0C0);
    Deger (L"ELR HI  =", (UINT32)(Onceki[2] >> 32), NULL, 0x00C0C0C0);
    Deger (L"ID      =", (UINT32)Onceki[1], NULL, 0x00C0C0C0);
  } else {
    Yaz (L"EL3 ISTISNASI YOK", 0x00808080);
  }

  mSatir += 12;

  //
  // 3) Windows surucusunun olum sonrasi kaydi ve calisma zamani sayaclari.
  //
  if (FbLog[0] == 'B' && FbLog[1] == '2' && FbLog[2] == 'L' && FbLog[3] == 'F') {
    CHAR16 Tik[24];
    UINTN  K;

    for (K = 0; K < 16; K++) {
      Tik[K] = (CHAR16)FbLog[16 + K];
      if (Tik[K] < 0x20 || Tik[K] > 0x7E) {
        Tik[K] = L' ';
      }
    }

    Tik[16] = L'\0';
    Yaz (Tik, 0x00FFFF40);
  } else {
    Yaz (L"B2LFB KAYDI YOK", 0x00808080);
  }

  if (RtLog[0] == 0x524C3242) {
    Deger (L"GETVAR GIR=", RtLog[3], NULL, 0x00808080);
    Deger (L"GETVAR CIK=", RtLog[4], NULL, 0x00808080);
    Deger (L"RESET     =", RtLog[5], NULL, 0x00808080);
  }

  mSatir += 12;

  Deger (L"RST STAT=", Rst,
         (Rst == 0x20000000) ? L"YAZILIM" :
         (Rst == 0x40010000) ? L"GUC" : L"BASKA",
         0x0060C0FF);

  mSatir += 12;
  Yaz (L"FOTOGRAFLA, SONRA TUSA BAS", 0x00FFFFFF);

  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

  return EFI_SUCCESS;
}
