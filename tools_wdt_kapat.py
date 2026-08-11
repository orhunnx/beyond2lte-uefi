#!/usr/bin/env python3
"""
UEFI watchdog'unu etkisiz hale getirir.

    python3 tools_wdt_kapat.py <Mu-Silicium-yolu>

---------------------------------------------------------------------------
NEDEN
---------------------------------------------------------------------------
Olcum zinciri:

  1. Windows cekirdegi ~9 saniye yasiyor, sonra TEMIZ yazilim sifirlamasi.
     Bugcheck yok (dogrulanmis geri cagri), kilitlenme yok (kalp atisi son
     ana kadar duzenli), guc kesilmesi yok (DRAM hayatta).

  2. Surucude 5 saniye bekletince olum otelenmedi; sure dokuzun icinden
     cikti. Yani olum ani SABIT.

  3. Boot menusunde ~30 saniye bekleyince cekirdek 1 saniye yasadi.
     Menude gecen sure de butceden dusuyor - yani sayac cekirdekle DEGIL,
     DAHA ONCE basliyor.

  4. Diskten acilista winload 50 saniye yukleme yapmisti ve cekirdek 2
     saniyede olmustu. O zaman storufs'u suclamistim. YANLISTI: butce
     yukleme sirasinda tukenmis.

  5. EbsTest ExitBootServices'ten sonra 60+ saniye yasiyor - ama o EFI
     Shell'den calisiyor ve SHELL ACILIRKEN WATCHDOG'U KENDISI KAPATIYOR.
     bootmgfw ise yeniden kuruyor.

Bes gozlem tek aciklamaya oturuyor: bootmgfw baslarken UEFI'ye
SetWatchdogTimer cagiriyor (spec'e gore genelde 5 dakika) ve bizim
firmware'de o watchdog ya yanlis kalibre ya da ExitBootServices'te
kapanmiyor. Sure dolunca cihaz sifirlaniyor - Windows'un haberi bile
olmadan, ki gordugumuz tam olarak buydu.

---------------------------------------------------------------------------
NEDEN SURUCUYU TAMAMEN CIKARMIYORUZ
---------------------------------------------------------------------------
WatchdogTimer bir MIMARI PROTOKOL. DxeCore, BDS'e gecmeden once butun
mimari protokollerin kurulmus olmasini bekliyor; suruculeri DXE.inc'ten
cikarmak acilisi bastan bozar.

Onun yerine periyodu sifirliyoruz: protokol duruyor, cagrilar basariyla
donuyor, ama zamanlayici hicbir zaman kurulmuyor. bootmgfw "watchdog
kuruldu" cevabini aliyor ve yoluna devam ediyor.

---------------------------------------------------------------------------
GERI ALMAK
---------------------------------------------------------------------------
yap.sh her derlemede dosyayi git'ten geri cagiriyor, yani bu yamayi
cagirmamak yeterli. Birikmeli hasar olmuyor.
"""

import sys
import pathlib

if len(sys.argv) < 2:
    print("Kullanim: python3 tools_wdt_kapat.py <Mu-Silicium-yolu>")
    sys.exit(1)

MU = pathlib.Path(sys.argv[1])
SRC = MU / "MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.c"

if not SRC.is_file():
    print(f"WatchdogTimer.c bulunamadi: {SRC}")
    sys.exit(1)

s = SRC.read_text()

eski = "  mWatchdogTimerPeriod = TimerPeriod;"
yeni = """  //
  // B2L: WATCHDOG ETKISIZ.
  //
  // Gerekce tools_wdt_kapat.py basinda. Ozet: bootmgfw acilirken watchdog
  // kuruyor, sure doluyor ve cihaz Windows'un haberi olmadan sifirlaniyor.
  // Periyodu sifirlayinca asagidaki SetTimer cagrisi TimerCancel oluyor,
  // yani zamanlayici hic kurulmuyor.
  //
  // Cagri yine EFI_SUCCESS donuyor; bootmgfw "watchdog kuruldu" cevabini
  // alip yoluna devam ediyor.
  //
  mWatchdogTimerPeriod = 0;
  (VOID)TimerPeriod;"""

if "B2L: WATCHDOG ETKISIZ" in s:
    print("WatchdogTimer.c zaten yamali")
    sys.exit(0)

if eski not in s:
    print("HATA: beklenen satir bulunamadi. Upstream degismis olabilir;")
    print("yamayi kor kor birakmiyoruz.")
    sys.exit(1)

s = s.replace(eski, yeni, 1)
SRC.write_text(s)
print("WatchdogTimer.c yamalandi: periyot her zaman 0 (etkisiz)")
