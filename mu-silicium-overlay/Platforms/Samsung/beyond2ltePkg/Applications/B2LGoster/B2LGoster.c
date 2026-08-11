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

#define OLCEK        4      // her glif pikseli 4x4 blok
#define KENAR       24      // sol/ust bosluk

#define SEH_CANLI   0xCF041000ULL
#define SEH_DURUM   0xCF041080ULL
#define SEH_ONCEKI  0xCF041100ULL
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
  Onceki = (UINT64 *)(UINTN)SEH_ONCEKI;
  RtLog  = (UINT32 *)(UINTN)RT_KAYIT;
  FbLog  = (UINT8  *)(UINTN)FB_KAYIT;
  Rst    = *(volatile UINT32 *)(UINTN)PMU_RST;

  Yaz (L"=== BEYOND2LTE TANI ===", 0x00FFFFFF);
  mSatir += 12;

  //
  // 1) EL3 SEH - bu turun asil sorusu.
  //
  if (Durum[0] != 0x52484553) {
    Yaz (L"SEH SURUCUSU YUKLENMEDI", 0x00FF4040);
  } else {
    Deger (L"SEH KAYIT=", Durum[2],
           (Durum[2] == 0) ? L"KABUL" : L"RET",
           (Durum[2] == 0) ? 0x0040FF40 : 0x00FF4040);
    Deger (L"SEH SANAL=", Durum[3],
           (Durum[3] == 0) ? L"KABUL" : L"YOK",
           0x00C0C0C0);
    Deger (L"DEBUG MEM=", Durum[1], NULL, 0x00808080);
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
