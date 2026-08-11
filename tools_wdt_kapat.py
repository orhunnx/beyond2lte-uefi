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
Yama IDEMPOTENT: imza zaten varsa dosyaya dokunulmuyor. Dosya bir ALT
MODULDE oldugu icin "git -C $MU checkout" ona ulasmiyor, o yuzden geri
cagirmaya guvenilmiyor - tekrar tekrar calistirmak guvenli.

Tamamen kapatmak icin:  B2L_WDT=0 bash yap.sh
Elle geri almak icin:   git -C <MdeModulePkg alt modulu> checkout -- \
                          Universal/WatchdogTimerDxe/WatchdogTimer.c
"""

import sys
import pathlib

if len(sys.argv) < 2:
    print("Kullanim: python3 tools_wdt_kapat.py <Mu-Silicium-yolu>")
    sys.exit(1)

MU = pathlib.Path(sys.argv[1])

#
# SABIT YOL KULLANILMIYOR - ARANIYOR.
#
# Ilk surum "$MU/MdeModulePkg/..." diyordu ve dosya bulunamadi. Project Mu
# depolari alt modullere boluyor; MdeModulePkg koke degil MU_BASECORE gibi
# bir alt dizine dusuyor ve bu surumden surume degisebiliyor.
#
# Aramak birkac milisaniye; sabit yol yazmak bir tur.
#
ADAYLAR = sorted(MU.glob("**/Universal/WatchdogTimerDxe/WatchdogTimer.c"))

if not ADAYLAR:
    print(f"WatchdogTimer.c bulunamadi ({MU} altinda arandi).")
    print("Mu-Silicium alt modulleri cekilmemis olabilir:")
    print("  git -C <Mu-Silicium> submodule update --init --recursive")
    sys.exit(1)

if len(ADAYLAR) > 1:
    print("Birden fazla WatchdogTimer.c bulundu, hepsi yamalanacak:")
    for a in ADAYLAR:
        print(f"  {a.relative_to(MU)}")

SRC = ADAYLAR[0]
print(f"  bulundu: {SRC.relative_to(MU)}")

YENI = """  //
  // B2L: WATCHDOG ETKISIZ.
  //
  // Gerekce bu dosyanin basinda. Ozet: bootmgfw acilirken watchdog kuruyor,
  // sure doluyor ve cihaz Windows'un haberi olmadan sifirlaniyor. Periyodu
  // sifirlayinca asagidaki SetTimer cagrisi TimerCancel oluyor, yani
  // zamanlayici hic kurulmuyor.
  //
  // Cagri yine EFI_SUCCESS donuyor; bootmgfw "watchdog kuruldu" cevabini
  // alip yoluna devam ediyor.
  //
  mWatchdogTimerPeriod = 0;
  (VOID)TimerPeriod;"""

ESKI = "  mWatchdogTimerPeriod = TimerPeriod;"

yamalanan = 0
for src in ADAYLAR:
    s = src.read_text()
    if "B2L: WATCHDOG ETKISIZ" in s:
        print(f"  zaten yamali: {src.relative_to(MU)}")
        continue
    if ESKI not in s:
        print(f"HATA: beklenen satir yok: {src.relative_to(MU)}")
        print("Upstream degismis olabilir; yamayi kor kor birakmiyoruz.")
        sys.exit(1)
    src.write_text(s.replace(ESKI, YENI, 1))
    print(f"  yamalandi: {src.relative_to(MU)}")
    yamalanan += 1

print(f"Watchdog etkisiz ({yamalanan} dosya yamalandi, periyot her zaman 0)")
