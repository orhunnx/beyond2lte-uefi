#!/usr/bin/env bash
#
# CACHE bolumu icin ESP imaji uretir. esp/ klasorundeki her seyi iceri koyar
# (startup.nsh, kernel, initramfs...). Iki cikti verir:
#
#   out/cache.img       ham imaj   -> recovery + adb dd yolu
#   out/cache-esp.tar   sparse tar -> Odin, AP slotu
#
#   bash cache_esp.sh [iceri-konacak-dosya.efi]
#
# Argüman verilmezse Mu-Silicium build'indeki Shell.efi aranir.
#
# ---------------------------------------------------------------------------
# UC KISIT VAR, ucu de zorunlu. Her biri bir tur kaybettirdi:
#
# 1) -S 4096 : UFS'in mantiksal blok boyutu 4096 (log: "BlockSize : 4096").
#    FAT'in BPB'sindeki sektor boyutu BlockIo'nunkiyle esitlesmeli, yoksa
#    FatPkg birimi baglamaz.
#
# 2) -F 16   : FAT32 DEGIL. FatPkg/EnhancedFatDxe/Init.c:
#         if (Volume->MaxCluster < FAT_MAX_FAT16_CLUSTER) return EFI_VOLUME_CORRUPTED;
#    FAT_MAX_FAT16_CLUSTER = 65525. CACHE 75 MB; 4K sektorle 19200 cluster
#    cikiyor -> FAT32 sessizce reddediliyor. 4K sektorle FAT32 icin en az
#    256 MB lazim. FAT16'nin gecerli araligi 4085-65525; 19200 tam ortasinda.
#
# 3) sparse  : Odin ham FAT imajini reddediyor. Android sparse formatina
#    (0xED26FF3A) cevirince kabul ediyor. dd yolu icin ham imaj gerekli,
#    o yuzden ikisini birden uretiyoruz.
# ---------------------------------------------------------------------------
set -euo pipefail
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MU="${MU_SILICIUM:-$HOME/Mu-Silicium}"
OUT="$PROJ/out"; mkdir -p "$OUT"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

#
# --win : sadece Windows dosyalarini koy (Linux kernel/initramfs haric).
#
# 600 MiB'lik bolume hem 570 MB'lik boot.wim hem 68 MB'lik Linux dosyalari
# sigmiyor - bir tur mcopy "Disk full" verdi ve sources/ bos kaldi.
# Iki mod ayri ayri calisiyor, ikisi ayni anda degil.
#
#   --win  -> Windows: bootmgfw + BCD + boot.sdi + boot.wim, Linux dosyalari YOK
#   (bayraksiz) -> Linux: kernel + initramfs, WINDOWS DOSYALARI DA YOK
#
# Ikincisi onemli: onceden bayraksiz mod Windows dosyalarini da koyuyordu ve
# startup.nsh once bootmgfw'yi gordugu icin Linux'a hic sira gelmiyordu.
# Yani "Linux modu" diye bir sey yoktu.
#
WINONLY=0
BIGPART=0
BOSALT=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --win)    WINONLY=1 ;;
    --system) BIGPART=1 ;;
    --bos)    BOSALT=1 ;;
    *)        ARGS+=("$a") ;;
  esac
done
if [ ${#ARGS[@]} -gt 0 ]; then set -- "${ARGS[@]}"; else set --; fi
if [ "$WINONLY" = 1 ]; then say "MOD: Windows"; else say "MOD: Linux"; fi

#
# --system : CACHE (600 MiB) yerine SYSTEM (5.57 GB) bolumunu hedefle.
#
# NEDEN GEREKTI: Windows 11 23H2'nin boot.wim'i 742 MB. LZX ile yeniden
# sikistirmak HIC kazanc vermedi (742 -> 742) cunku ISO'nunki zaten LZX;
# acilmis icerik 1650 MiB. Yani sikistirmayla cozulecek bir sey yok, 600
# MiB'lik bolume sigmiyor.
#
# ODIN HEDEFI DOSYA ADIYLA BELIRLENIYOR: tar icindeki "cache.img" CACHE'e,
# "system.img" SYSTEM'e yaziliyor. Ikisini karistirma.
#
# FAT32'YE GECIYORUZ: FatPkg FAT32 icin en az 65525 kume istiyor
# (Init.c: MaxCluster < FAT_MAX_FAT16_CLUSTER -> VOLUME_CORRUPTED).
# 600 MiB'de 4K kumeyle 153600 kume FAT16'nin ust sinirini asiyordu, bu
# yuzden orada FAT16 + 32 KiB kume kullanmistik. 2 GiB'de 4K kumeyle
# 524288 kume cikiyor - FAT32 icin fazlasiyla gecerli.
#
# BOLUMUN TAMAMINI DOLDURMUYORUZ: 2 GiB'lik bir dosya sistemi 5.57 GB'lik
# bolumun basina yaziliyor, gerisi bos kaliyor. FatPkg birimin boyutunu
# BPB'den okuyor, bolum tablosundan degil - sorun cikmiyor. Sparse imaj
# oldugu icin aktarilan veri yine kucuk.
#
if [ "$BIGPART" = 1 ]; then
  PART="system"
  SZ=2147483648
  FATOPT="-F 32 -S 4096 -s 1"
  FATDESC="FAT32 (2 GiB, 4096 bayt sektor, 4 KiB kume)"
  LIMIT_MB=1950
  say "HEDEF BOLUM: SYSTEM (5.57 GB) - imaj 2 GiB"
else
  PART="cache"
  SZ=629145600
  FATOPT="-F 16 -S 4096 -s 8"
  FATDESC="FAT16 (600 MiB, 4096 bayt sektor, 32 KiB kume)"
  LIMIT_MB=588
fi

APP="${1:-}"
if [ -z "$APP" ]; then
  APP="$(find "$MU/Build" -name 'Shell.efi' -path '*ShellPkg*' 2>/dev/null | head -1)"
  [ -n "$APP" ] || APP="$(find "$MU/Build" -maxdepth 5 -name 'Shell.efi' 2>/dev/null | head -1)"
fi
[ -f "$APP" ] || { echo "EFI uygulamasi bulunamadi. Yolu argüman olarak ver."; exit 1; }
say "Icerik: $APP ($(du -h "$APP" | cut -f1))"

#
# CACHE = 153600 blok x 4096 = 629145600 bayt = 600 MiB.
#
# Onceden 512 bayt varsayip 75 MB imaj yaziyorduk; bolumun geri kalani bos
# kaliyordu. Cihazdan geri okununca gercek boyut 600 MiB cikti (PARTITIONS.md).
# Windows'un boot.wim'i icin bu alana ihtiyacimiz var.
#
# KUME BOYUTU: 600 MiB'i 4K kumeyle bicimlendirirsek 153600 kume cikar ve bu
# FAT16'nin ust siniri olan 65525'i asar -> FatPkg birimi baglamaz. "-s 8"
# ile kume 32 KiB oluyor, kume sayisi 19200'e dusuyor ve FAT16'nin gecerli
# araligi olan 4085-65525'in tam ortasinda kaliyor. (FAT32'ye gecemiyoruz,
# bkz. yukaridaki 2 numarali kisit.)
#
#
# ---------------------------------------------------------------------------
# --bos : BOLUMU SIL, ICINE HICBIR SEY KOYMA
# ---------------------------------------------------------------------------
# NEDEN GEREKTI - BES TURLUK KAFA KARISIKLIGININ SEBEBI BUYDU:
#
# Bir noktada ESP'yi SYSTEM bolumune flasladik (23H2'nin 742 MB'lik WIM'i
# 600 MiB'lik CACHE'e sigmiyordu). Sonra CACHE'e geri dondük - ama SYSTEM'de
# birakt.gimiz TAM ESP orada durmaya devam etti, icindeki ESKI startup.nsh
# ile birlikte.
#
# EFI Shell startup.nsh'i hangi FAT biriminde ONCE bulursa onu calistirir.
# Iki birim vardi ve eskisini buldu. Sonuc: depoda dogru dosya, cihaza dogru
# imaj gidiyor, ama calisan script eski. Yaptigimiz hicbir degisiklik test
# edilmedi - eski map ciktisi, kaybolmayan B2L UFS satirlari, bulunamayan
# EBSTEST.EFI, degismeyen 15 saniye, hepsi bundan.
#
# Bu mod bolumun basina 16 MiB sifir yaziyor. FAT onyukleme sektoru gidince
# birim hic taninmiyor ve ortada tek bir startup.nsh kaliyor.
#
if [ "$BOSALT" = 1 ]; then
  IMG="$OUT/$PART.img"
  say "BOLUM SILINIYOR: $(echo "$PART" | tr a-z A-Z)  (basina 16 MiB sifir)"
  rm -f "$IMG"
  dd if=/dev/zero of="$IMG" bs=1M count=16 status=none

  WORK="$(mktemp -d)"
  if command -v img2simg >/dev/null; then
    img2simg "$IMG" "$WORK/$PART.img"
  else
    cp "$IMG" "$WORK/$PART.img"
  fi
  ( cd "$WORK" && tar -H ustar -cf "$OUT/$PART-esp.tar" "$PART.img" )
  rm -rf "$WORK"

  ls -lh "$OUT/$PART-esp.tar"
  for p in /mnt/c/Users/*/OneDrive/Masa*st* /mnt/c/Users/*/OneDrive/Desktop \
           /mnt/c/Users/*/Masa*st* /mnt/c/Users/*/Desktop; do
    [ -d "$p" ] || continue
    mkdir -p "$p/beyond2lte-flash"
    cp -f "$OUT/$PART-esp.tar" "$p/beyond2lte-flash/"
    echo
    echo "Masaustune kopyalandi: $p/beyond2lte-flash/$PART-esp.tar"
    echo "  Odin, AP slotu (Re-Partition ISARETSIZ)"
    break
  done
  exit 0
fi

IMG="$OUT/$PART.img"

say "$FATDESC imaji olusturuluyor"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=$((SZ/1048576)) status=none
mkfs.vfat $FATOPT -n UEFIESP "$IMG" >/dev/null

# startup.nsh: Shell acilista bunu otomatik calistirir. Klavye olmadan
# komut vermenin tek yolu. Iki yere de koyuyoruz cunku Shell'in arama sirasi
# surume gore degisiyor: once kendi dizini, sonra kok.
NSH="$PROJ/esp/startup.nsh"

# Bir tur, cihazdan geri okunan imajda ESKI startup.nsh cikti: kernel'ler
# eklendikten sonra ama script duzenlenmeden once uretilmis bir imaj
# flaslanmisti ve hicbir log uretilmedi. Boyut+ozet basiyoruz ki hangi surumun
# gittigi belli olsun; cihazdan geri okuyunca bununla karsilastir.
if [ -f "$NSH" ]; then
  say "startup.nsh: $(stat -c%s "$NSH") bayt  sha1=$(sha1sum "$NSH" | cut -c1-12)"
fi

if command -v mcopy >/dev/null; then
  export MTOOLS_SKIP_CHECK=1
  mmd   -i "$IMG" ::/EFI ::/EFI/BOOT
  mcopy -i "$IMG" "$APP" ::/EFI/BOOT/BOOTAA64.EFI
  [ -f "$NSH" ] && { mcopy -i "$IMG" "$NSH" ::/startup.nsh
                     mcopy -i "$IMG" "$NSH" ::/EFI/BOOT/startup.nsh; }
  # esp/ agacinin tamamini al: dosyalar VE alt klasorler.
  # Alt klasor destegi Windows icin gerekli - bootmgfw BCD'yi
  # \EFI\Microsoft\Boot\BCD yolunda ariyor.
  COPYFAIL=0
  for f in "$PROJ"/esp/*; do
    b="$(basename "$f")"
    [ "$b" = "startup.nsh" ] && continue
    [ -e "$f" ] || continue
    if [ "$WINONLY" = 1 ]; then
      case "$b" in
        vmlinuz-*|initramfs-*) echo "  - $b (atlandi: Windows modu)"; continue ;;
      esac
    else
      # EFI/BOOT zaten yukarida ayrica dolduruldu; buradaki EFI klasoru
      # sadece Microsoft/ agacini tasiyor, Linux modunda istemiyoruz.
      case "$b" in
        EFI|sources|boot) echo "  - $b (atlandi: Linux modu)"; continue ;;
      esac
    fi
    # Buyukten kucuge degil, sirayla kopyaliyoruz; hata SESSIZ GECMESIN.
    if [ -d "$f" ]; then
      if mcopy -i "$IMG" -s "$f" "::/"; then echo "  + $b/ (klasor)"
      else echo "  ! $b/ KOPYALANAMADI"; COPYFAIL=1; fi
    else
      if mcopy -i "$IMG" "$f" "::/$b"; then echo "  + $b ($(du -h "$f" | cut -f1))"
      else echo "  ! $b KOPYALANAMADI ($(du -h "$f" | cut -f1))"; COPYFAIL=1; fi
    fi
  done
  if [ "$COPYFAIL" = 1 ]; then
    echo
    echo "  DIKKAT: en az bir dosya imaja giremedi (buyuk ihtimalle yer yok)."
    echo "  Windows icin:  bash cache_esp.sh --win"
    echo "  esp/ toplami: $(du -sh "$PROJ/esp" | cut -f1) / imaj ${LIMIT_MB} MB"
  fi
  # Windows icin boot.wim gerekiyor; sessizce eksik kalmasin.
  if mdir -i "$IMG" ::/sources >/dev/null 2>&1; then
    echo "--- sources/ (WinPE):"
    mdir -i "$IMG" ::/sources | sed 's/^/  /'
  else
    echo "--- sources/boot.wim YOK  (WinPE acilmaz; tools_winboot.sh ciktisina bak)"
  fi
  echo "--- bos alan:"
  mdir -i "$IMG" ::/ | tail -1
  echo "--- imaj icerigi:"
  mdir -i "$IMG" ::/
  mdir -i "$IMG" ::/EFI/BOOT
else
  echo "mtools yok -> sudo apt install -y mtools   (loop mount'a dusuluyor)"
  T=$(mktemp -d); sudo mount -o loop "$IMG" "$T"
  sudo mkdir -p "$T/EFI/BOOT"; sudo cp "$APP" "$T/EFI/BOOT/BOOTAA64.EFI"
  [ -f "$NSH" ] && { sudo cp "$NSH" "$T/startup.nsh"; sudo cp "$NSH" "$T/EFI/BOOT/startup.nsh"; }
  sudo umount "$T"; rmdir "$T"
fi

say "Odin icin sparse imaj + tar"
WORK="$(mktemp -d)"
if command -v img2simg >/dev/null; then
  img2simg "$IMG" "$WORK/$PART.img"
  MAGIC=$(od -An -tx4 -N4 "$WORK/$PART.img" | tr -d ' ')
  echo "  sparse sihirli sayisi: 0x$MAGIC  (beklenen: ed26ff3a)"
else
  echo "  UYARI: img2simg yok -> sudo apt install -y android-sdk-libsparse-utils"
  echo "  Ham imaj tar'lanıyor; Odin reddedebilir."
  cp "$IMG" "$WORK/$PART.img"
fi
( cd "$WORK" && tar -H ustar -cf "$OUT/$PART-esp.tar" "$PART.img" )
rm -rf "$WORK"

say "Cikti"
ls -lh "$IMG" "$OUT/$PART-esp.tar"

for p in /mnt/c/Users/*/OneDrive/Masa*st* /mnt/c/Users/*/OneDrive/Desktop \
         /mnt/c/Users/*/Masa*st* /mnt/c/Users/*/Desktop; do
  [ -d "$p" ] || continue
  mkdir -p "$p/beyond2lte-flash"
  cp -f "$OUT/$PART-esp.tar" "$IMG" "$p/beyond2lte-flash/"
  echo
  echo "Masaustune kopyalandi: $p/beyond2lte-flash"
  echo "  $PART-esp.tar  -> Odin, AP slotu (Re-Partition ISARETSIZ)"
  echo "  $PART.img      -> recovery: adb push + dd"
  echo "  hedef bolum: $(echo "$PART" | tr a-z A-Z)"
  break
done
