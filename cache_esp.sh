#!/usr/bin/env bash
#
# CACHE bolumu icin ESP imaji uretir. esp/ klasorundeki her seyi iceri koyar
# (startup.nsh, kernel, initramfs...). Iki cikti verir:
#
#   out/cache.img       ham imaj   -> recovery + adb dd yolu
#   out/cache-esp.tar   sparse tar -> Odin, AP slotu
#
#   bash cache_esp.sh [--win] [--disk|--baseline|--psci-hvc|--bos] [iceri-konacak-dosya.efi]
#
# Argüman verilmezse Mu-Silicium build'indeki Shell.efi aranir.
#
# ---------------------------------------------------------------------------
# Dolu ESP uretim yolunun uc temel kisiti var:
#
# 1) -S 4096 : UFS'in mantiksal blok boyutu 4096 (log: "BlockSize : 4096").
#    FAT'in BPB'sindeki sektor boyutu BlockIo'nunkiyle esitlesmeli, yoksa
#    FatPkg birimi baglamaz.
#
# 2) Dolu CACHE ESP yolu, daha once cihazda dogrulanan FAT16 + 32 KiB kume
#    davranisini korur. CACHE'in dogru boyutu 600 MiB oldugundan FAT32 + 4 KiB
#    kume de gecerlidir (yaklasik 153600 kume). `--win --bos` bunu kullanarak
#    tam boyutlu ve icinde dosya olmayan FAT32 imaji uretir.
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
# --win   : sadece Windows dosyalarini koy (Linux kernel/initramfs haric).
# --linux : Linux dosyalarini BILEREK buyuk boluma yaz. Asagidaki korumayi
#           kaldirmaz, niyeti acikca soyler.
# --bos   : eski CACHE ESP icerigini bos, 600 MiB FAT32 CACHE imajiyla
#           degistir. Bu bir Windows WinPE turudur; bayraksiz calisma Linux
#           modu oldugu icin --win zorunludur.
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
LINUXOK=0
BIGPART=0
BOSALT=0
BASELINE=0
PSCI_HVC=0
DISKBOOT=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --win)    WINONLY=1 ;;
    --linux)  LINUXOK=1 ;;
    --system) BIGPART=1 ;;
    --bos)    BOSALT=1 ;;
    --baseline) BASELINE=1 ;;
    --psci-hvc) PSCI_HVC=1 ;;
    --disk)   DISKBOOT=1 ;;
    *)        ARGS+=("$a") ;;
  esac
done
if [ ${#ARGS[@]} -gt 0 ]; then set -- "${ARGS[@]}"; else set --; fi

#
# --baseline: B2L UEFI tani kodu calistirmayan Windows ESP. Imzali Windows
# surucu paketleri boot.wim offline DriverStore'unda kalir; startnet yalniz
# durumlarini denetler.
#
# Bu mod B2LGOSTR.EFI ve EBSTEST.EFI'yi imaja ALMAZ, normal startup.nsh
# yerine yalnizca bootmgfw.efi'yi bulan kucuk baslangic betigini koyar.
# B2LGoster'in tum conventional RAM'i yaz/oku/geri-yaz taramasi yaptigi
# unutulmamali: normal Shell yolu onu Windows'tan ONCE calistirir. Bu,
# b2lfb Start=4 ve DXE.inc'teki B2L suruculerinin kapatilmasiyla birlikte
# gercekten aletsiz tabani olusturur.
#
# UFS/PWM veya Watchdog yamalarini DEGISTIRMEZ. Bir deneyde tek degisken
# aletler olsun diye onlar yap.sh'in mevcut ayarinda kalir.
#
if [ "$BASELINE" = 1 ] && [ "$WINONLY" = 0 ]; then
  echo "--baseline yalnizca Windows modu ile kullanilir: bash cache_esp.sh --win --system --baseline"
  exit 1
fi
if [ "$PSCI_HVC" = 1 ] && [ "$WINONLY" = 0 ]; then
  echo "--psci-hvc yalnizca Windows modu ile kullanilir: bash cache_esp.sh --win --system --psci-hvc"
  exit 1
fi
if [ "$PSCI_HVC" = 1 ] && [ "$BASELINE" = 1 ]; then
  echo "--psci-hvc ve --baseline birlikte kullanilmaz: HVC deneyinde onceki acilisin HVC kaydi ekranda okunur."
  exit 1
fi
if [ "$BOSALT" = 1 ] && [ "$WINONLY" = 0 ]; then
  echo "--bos yalnizca Windows WinPE turunde kullanilir: bash cache_esp.sh --win --bos"
  echo "Bayraksiz calisma Linux modu oldugu icin bos CACHE imaji yanlis moda etiketlenmez."
  exit 1
fi
if [ "$BOSALT" = 1 ] && [ "$BIGPART" = 1 ]; then
  echo "--bos yalniz CACHE icindir; --system ile birlikte kullanilmaz."
  echo "Dogru komut: bash cache_esp.sh --win --bos"
  exit 1
fi
if [ "$DISKBOOT" = 1 ] && [ "$WINONLY" = 0 ]; then
  echo "--disk yalniz Windows ESP icindir: bash cache_esp.sh --win --disk --baseline"
  exit 1
fi
if [ "$DISKBOOT" = 1 ] && { [ "$BIGPART" = 1 ] || [ "$BOSALT" = 1 ]; }; then
  echo "--disk CACHE'te boot ESP, SYSTEM'de NTFS kullanir; --system/--bos ile birlesmez."
  exit 1
fi
if [ "$DISKBOOT" = 1 ]; then
  [ -s "$PROJ/esp/EFI/Microsoft/Boot/BCD" ] || {
    echo "HATA: disk ESP icin BCD yok. Once: bash tools_winboot.sh --disk"
    exit 1
  }
  [ ! -e "$PROJ/esp/sources/boot.wim" ] || {
    echo "HATA: --disk secildi ama ESP hala boot.wim tasiyor; RAM disk karisimi reddedildi."
    exit 1
  }
  say "MOD: UFS disk boot ESP (CACHE: bootmgfw + BCD, boot.wim yok)"
fi
if [ "$WINONLY" = 1 ]; then say "MOD: Windows"; else say "MOD: Linux"; fi
if [ "$BASELINE" = 1 ]; then
  say "MOD: Windows temiz taban (B2L UEFI tani yok; Windows suruculeri etkin)"
fi
if [ "$PSCI_HVC" = 1 ]; then
  [ -s "$PROJ/esp/B2LGOSTR.EFI" ] || {
    echo "HATA: HVC kaydini gosterecek B2LGOSTR.EFI yok. Once: bash yap.sh --psci-hvc"
    exit 1
  }
  say "MOD: Windows PSCI HVC deneyi (B2LGoster sonraki acilista HVC kaydini gosterir)"
fi

#
# GUVENLIK KILIDI: --system + Linux modu neredeyse kesin bir hata.
#
# BU BIR TURA MAL OLDU. Talimat "bash cache_esp.sh --system" seklinde
# verildi, --win unutuldu. Script gorevini kusursuz yapti: SYSTEM'e bir
# LINUX ESP'si yazdi. Cihaz da startup.nsh'in Linux dalini calistirip
# vmlinuz-lts'i acti. Windows dosyalari imaja hic girmemisti.
#
# Ekranda "MOD: Linux" yaziyordu ama tek satir, elli satir cikti icinde.
# Sessiz dogru davranis, gurultulu yanlis davranistan daha tehlikeli
# olabiliyor - burada oyleydi.
#
# --system'in VAR OLMA SEBEBI 742 MB'lik boot.wim'in 600 MiB'lik CACHE'e
# sigmamasi. Linux dosyalari 68 MB; onlar icin buyuk bolume gecmenin hicbir
# anlami yok. Yani bu kombinasyon istenerek yazilmis olamaz.
#
if [ "$BIGPART" = 1 ] && [ "$WINONLY" = 0 ] && [ "$BOSALT" = 0 ] && [ "$LINUXOK" = 0 ]; then
  echo
  echo "  ###############################################################"
  echo "  # DUR: --system verildi ama --win YOK."
  echo "  #"
  echo "  # Bu haliyle SYSTEM bolumune bir LINUX ESP'si yazilir ve"
  echo "  # cihaz Windows yerine vmlinuz-lts'i acar."
  echo "  #"
  echo "  # Windows istiyorsan:   bash cache_esp.sh --win --system"
  echo "  # Gercekten Linux mu:   bash cache_esp.sh            (CACHE'e)"
  echo "  # Linux'u SYSTEM'e:     bash cache_esp.sh --linux --system"
  echo "  #"
  echo "  # Son secenek Linux kontrol deneyi icin: iki ayri ESP birakmak"
  echo "  # (CACHE'te Linux, SYSTEM'de Windows) kabugun hangisini bulacagini"
  echo "  # belirsiz birakiyor - bu tuzaga eski B2LGOSTR kopyalariyla zaten"
  echo "  # dustuk. Tek birim, tek startup.nsh."

  echo "  ###############################################################"
  echo
  exit 1
fi

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
compact_winpe_wim() {
  local WIM BEFORE AFTER SAVED

  [ "$BIGPART" = 1 ] && [ "$WINONLY" = 1 ] && [ "$BOSALT" = 0 ] || return 0
  WIM="$PROJ/esp/sources/boot.wim"
  [ -s "$WIM" ] || {
    echo "HATA: SYSTEM imaj boyutu hesaplanmadan once boot.wim bulunamadi: $WIM"
    exit 1
  }
  command -v wimlib-imagex >/dev/null 2>&1 || {
    echo "HATA: boot.wim deliklerini temizlemek icin wimlib-imagex gerekli."
    exit 1
  }

  BEFORE=$(stat -c%s "$WIM")
  say "boot.wim yeniden paketleniyor (birikmis WIM delikleri temizleniyor)"
  wimlib-imagex optimize "$WIM"
  wimlib-imagex verify "$WIM"
  AFTER=$(stat -c%s "$WIM")
  SAVED=$(( BEFORE > AFTER ? BEFORE - AFTER : 0 ))
  printf '  boot.wim: %s -> %s MiB; geri kazanilan: %s MiB\n' \
    "$(( (BEFORE + 1048575) / 1048576 ))" \
    "$(( (AFTER + 1048575) / 1048576 ))" \
    "$(( SAVED / 1048576 ))"
}

compact_winpe_wim

if [ "$BIGPART" = 1 ]; then
  PART="system"
  #
  # IMAJ BOYUTU ARTIK ICERIGE GORE - SABIT 2 GiB DEGIL.
  #
  # Her turda 2 GiB'lik bir dosya sifirdan uretiliyor, bicimlendiriliyor ve
  # img2simg ile bastan sona taraniyordu; oysa icerik ~800 MB. Sparse imaj
  # sayesinde AKTARILAN veri zaten kucuktu, ama YEREL islem tam boyut
  # uzerinden yapiliyordu ve dongunun gorunmez maliyetiydi.
  #
  # Icerik + %30 pay, 512 MiB alt sinirla. Alt sinirin sebebi FAT32: FatPkg
  # en az 65525 kume istiyor (Init.c), 4 KiB kumeyle bu 256 MiB ediyor.
  # 512 ile rahat payda kaliyoruz.
  #
  ICERIK_MB=$(du -sm "$PROJ/esp" 2>/dev/null | cut -f1 || echo 800)
  IMAJ_MB=$(( ICERIK_MB * 130 / 100 ))
  [ "$IMAJ_MB" -lt 512 ] && IMAJ_MB=512
  [ "$IMAJ_MB" -gt 5000 ] && IMAJ_MB=5000    # SYSTEM 5.57 GB
  SZ=$(( IMAJ_MB * 1048576 ))
  FATOPT="-F 32 -S 4096 -s 1"
  FATDESC="FAT32 (${IMAJ_MB} MiB, 4096 bayt sektor, 4 KiB kume)"
  LIMIT_MB=$(( IMAJ_MB - 60 ))
  say "HEDEF BOLUM: SYSTEM (5.57 GB) - imaj ${IMAJ_MB} MiB (icerik ${ICERIK_MB} MB)"
else
  PART="cache"
  SZ=629145600
  FATOPT="-F 16 -S 4096 -s 8"
  FATDESC="FAT16 (600 MiB, 4096 bayt sektor, 32 KiB kume)"
  LIMIT_MB=588
fi

#
# CACHE = 153600 blok x 4096 = 629145600 bayt = 600 MiB.
#
# Onceden 512 bayt varsayip 75 MB imaj yaziyorduk; bolumun geri kalani bos
# kaliyordu. Cihazdan geri okununca gercek boyut 600 MiB cikti (PARTITIONS.md).
# Windows'un boot.wim'i icin bu alana ihtiyacimiz var.
#
# KUME BOYUTU: Dolu ESP'nin tarihsel FAT16 yolu "-s 8" ile 32 KiB kume ve
# yaklasik 19200 kume kullanir. Bosaltma yolu ise FAT32 + 4 KiB kume kullanir;
# 600 MiB'de yaklasik 153600 kume FAT32'nin gecerli araligindadir.
#
#
# ---------------------------------------------------------------------------
# --bos : WINDOWS TURUNDE BOS FAT32 CACHE DOSYASI URET
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
# Bu mod yalnizca `--win --bos` ile cagrilir. Tam CACHE boyutunda, icinde
# dosya bulunmayan FAT32 olusturur. Boylece eski startup.nsh silinir ama
# Windows bolumu gecerli bir FAT32 birim olarak gorebilir.
#
if [ "$BOSALT" = 1 ]; then
  IMG="$OUT/$PART.img"
  say "BOS FAT32 URETILIYOR: $(echo "$PART" | tr a-z A-Z)  (600 MiB, 4096 bayt sektor/kume)"
  rm -f "$IMG"
  dd if=/dev/zero of="$IMG" bs=1M count=600 status=none
  mkfs.vfat -F 32 -S 4096 -s 1 -n CACHE "$IMG" >/dev/null

  BPB_SECTOR_BYTES="$(od -An -tu2 -j11 -N2 "$IMG" | tr -d ' ')"
  BPB_SECTORS_PER_CLUSTER="$(od -An -tu1 -j13 -N1 "$IMG" | tr -d ' ')"
  BPB_LABEL="$(dd if="$IMG" bs=1 skip=71 count=11 status=none | tr -d ' ')"
  BPB_FILESYSTEM="$(dd if="$IMG" bs=1 skip=82 count=8 status=none | tr -d ' ')"
  [ "$BPB_SECTOR_BYTES" = 4096 ] || {
    echo "HATA: bos CACHE BPB sektor boyutu 4096 degil: $BPB_SECTOR_BYTES"
    exit 1
  }
  [ "$BPB_SECTORS_PER_CLUSTER" = 1 ] || {
    echo "HATA: bos CACHE BPB kume boyutu 1 sektor degil: $BPB_SECTORS_PER_CLUSTER"
    exit 1
  }
  [ "$BPB_FILESYSTEM" = FAT32 ] || {
    echo "HATA: bos CACHE dosya sistemi FAT32 degil: $BPB_FILESYSTEM"
    exit 1
  }
  [ "$BPB_LABEL" = CACHE ] || {
    echo "HATA: bos CACHE birim etiketi CACHE degil: $BPB_LABEL"
    exit 1
  }
  echo "  dogrulandi: FAT32, 4096 bayt sektor, 4096 bayt kume, etiket CACHE"
  if command -v mdir >/dev/null; then
    MTOOLS_SKIP_CHECK=1 mdir -i "$IMG" ::/
  fi

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

APP="${1:-}"
if [ -z "$APP" ]; then
  APP="$(find "$MU/Build" -name 'Shell.efi' -path '*ShellPkg*' 2>/dev/null | head -1)"
  [ -n "$APP" ] || APP="$(find "$MU/Build" -maxdepth 5 -name 'Shell.efi' 2>/dev/null | head -1)"
fi
[ -f "$APP" ] || { echo "EFI uygulamasi bulunamadi. Yolu argüman olarak ver."; exit 1; }
say "Icerik: $APP ($(du -h "$APP" | cut -f1))"

# `out/b2lufs/b2lufs.sys` varsa bu WinPE turu o surucuyu olcmek icindir.
# Eski hata zinciri burada kapanir: WIM'e basariyla enjekte edilmis bir
# b2lufs paketi, daha sonra eski bir ESP arsiviyle degistirilebiliyordu ve
# cihazdaki startnet yalnizca "paket yok" diyordu. Sparse SYSTEM imajini
# uretmeden once WIM'in ayni SYS hash'ini tasiyan manifestini geri okuyoruz.
# Bu bir imza dogrulamasinin yerine gecmez (onu tools_b2lufs.sh yapar);
# yalniz dogru WIM'in dogru ESP imajina girdigini zorunlu kilar.
verify_winpe_b2lufs_package() {
  local VerifyDir ExpectedSys ManifestSys

  [ -s "$PROJ/out/b2lufs/b2lufs.sys" ] || return 0
  [ -s "$PROJ/esp/sources/boot.wim" ] || {
    echo "HATA: B2LUFS SYS var ama esp/sources/boot.wim yok. Once tools_b2lufs.sh calistir."
    return 1
  }
  command -v wimlib-imagex >/dev/null || {
    echo "HATA: B2LUFS WIM kapisi icin wimlib-imagex gerekli."
    return 1
  }

  VerifyDir="$(mktemp -d)" || {
    echo "HATA: B2LUFS WIM dogrulama dizini olusturulamadi."
    return 1
  }
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LUFS/manifest.txt --dest-dir="$VerifyDir" --no-acls >/dev/null; then
    rm -rf "$VerifyDir"
    echo "HATA: boot.wim'de B2LUFS manifest yok; eski WIM'i sparse ESP'ye koymak reddedildi."
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh"
    return 1
  fi

  ExpectedSys="$(sha256sum "$PROJ/out/b2lufs/b2lufs.sys" | awk '{print $1}')"
  ManifestSys="$(sed -n 's/^B2LUFS_SYS_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  rm -rf "$VerifyDir"
  if [ "$ManifestSys" != "$ExpectedSys" ]; then
    echo "HATA: boot.wim B2LUFS SYS hash'i derlenen SYS ile ayni degil; eski WIM reddedildi."
    echo "  WIM: ${ManifestSys:-YOK}"
    echo "  SYS: $ExpectedSys"
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh"
    return 1
  fi

  say "WinPE B2LUFS paketi dogrulandi"
  echo "  SYS SHA256: ${ExpectedSys:0:16}"
}

# The touch SYS has the same stale-WIM failure mode as B2LUFS.  If a local
# Windows touch build exists, require the WIM to contain its exact manifest
# hash before producing the sparse ESP image.
verify_winpe_b2ltouch_package() {
  local VerifyDir ExpectedSys ManifestSys ManifestOffline
  local OskDir OskManifestDir ExpectedOsk ManifestOsk ManifestOskPackage

  [ -s "$PROJ/out/b2ltouch/b2ltouch.sys" ] || return 0
  [ -s "$PROJ/esp/sources/boot.wim" ] || {
    echo "HATA: B2LTOUCH SYS var ama boot.wim yok. Once tools_b2ltouch.sh calistir."
    return 1
  }
  command -v wimlib-imagex >/dev/null || {
    echo "HATA: B2LTOUCH WIM kapisi icin wimlib-imagex gerekli."
    return 1
  }

  VerifyDir="$(mktemp -d)" || return 1
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LTOUCH/manifest.txt --dest-dir="$VerifyDir" --no-acls >/dev/null; then
    rm -rf "$VerifyDir"
    echo "HATA: boot.wim'de B2LTOUCH manifest yok; eski WIM reddedildi."
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2ltouch.sh"
    return 1
  fi

  ExpectedSys="$(sha256sum "$PROJ/out/b2ltouch/b2ltouch.sys" | awk '{print $1}')"
  ManifestSys="$(sed -n 's/^B2LTOUCH_SYS_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ManifestOffline="$(sed -n 's/^B2LTOUCH_OFFLINE_DRIVERSTORE=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LTOUCH/offline-driverstore.txt --dest-dir="$VerifyDir" --no-acls >/dev/null; then
    rm -rf "$VerifyDir"
    echo "HATA: boot.wim'de B2LTOUCH offline DriverStore isareti yok."
    return 1
  fi
  if [ "$ManifestOffline" != "1" ] || \
     ! tr -d '\r' < "$VerifyDir/offline-driverstore.txt" | \
       grep -qx 'B2L_OFFLINE_DRIVERSTORE=1'; then
    rm -rf "$VerifyDir"
    echo "HATA: B2LTOUCH paketi DriverStore'a offline eklenmemis."
    return 1
  fi
  rm -rf "$VerifyDir"
  if [ "$ManifestSys" != "$ExpectedSys" ]; then
    echo "HATA: boot.wim B2LTOUCH SYS hash'i derlenen SYS ile ayni degil."
    echo "  WIM: ${ManifestSys:-YOK}"
    echo "  SYS: $ExpectedSys"
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2ltouch.sh"
    return 1
  fi

  say "WinPE B2LTOUCH paketi dogrulandi"
  echo "  SYS SHA256: ${ExpectedSys:0:16}"

  # startnet.cmd native Windows OSK'yi acmayi bekliyorsa, klavyesiz bir WIM
  # artik flash paketine donusturulmez. Onceki turda guncel startnet vardi
  # fakat osk.exe WIM'e hic girmemisti; cihaz bunu ancak acilista gosterebildi.
  if grep -q 'B2LOSK\\manifest.txt' "$PROJ/windows/startnet.cmd"; then
    OskDir="$(mktemp -d)" || return 1
    OskManifestDir="$(mktemp -d)" || { rm -rf "$OskDir"; return 1; }
    if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
        /Windows/System32/osk.exe --dest-dir="$OskDir" --no-acls >/dev/null; then
      rm -rf "$OskDir" "$OskManifestDir"
      echo "HATA: boot.wim'de native Windows osk.exe yok; klavyesiz paket reddedildi."
      echo "Once: B2L_WINDOWS_ARM64_SOURCE=/mnt/c/.../Windows11_ARM64.iso bash tools_winpe_native_osk.sh"
      return 1
    fi
    if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
        /Windows/B2LOSK/manifest.txt --dest-dir="$OskManifestDir" --no-acls >/dev/null; then
      rm -rf "$OskDir" "$OskManifestDir"
      echo "HATA: boot.wim'de B2LOSK manifest yok; dogrulanmamis OSK reddedildi."
      return 1
    fi
    ExpectedOsk="$(sha256sum "$OskDir/osk.exe" | awk '{print $1}')"
    ManifestOsk="$(sed -n 's/^B2LOSK_OSK_SHA256=//p' "$OskManifestDir/manifest.txt" | head -n 1)"
    ManifestOskPackage="$(sed -n 's/^B2LOSK_PACKAGE=//p' "$OskManifestDir/manifest.txt" | head -n 1)"
    rm -rf "$OskDir" "$OskManifestDir"
    if [ "$ManifestOsk" != "$ExpectedOsk" ]; then
      echo "HATA: boot.wim native OSK hash'i manifest ile ayni degil."
      echo "  WIM: $ExpectedOsk"
      echo "  MAN: ${ManifestOsk:-YOK}"
      return 1
    fi
    if [ "$ManifestOskPackage" != "microsoft-classic-osk-winpe-runtime-v2" ]; then
      echo "HATA: boot.wim native OSK runtime paketi eski veya eksik."
      echo "  WIM: ${ManifestOskPackage:-YOK}"
      echo "Once: B2L_WINDOWS_ARM64_SOURCE=/mnt/c/.../Windows11_ARM64.iso bash tools_winpe_native_osk.sh"
      return 1
    fi
    say "WinPE native Windows OSK dogrulandi"
    echo "  OSK SHA256: ${ExpectedOsk:0:16}"
  fi
}

# A --usb-test build now produces both the staged UEFI diagnostic and the
# Windows ARM64 static-host package.  Baseline deliberately omits B2LUSB.EFI, but it
# must retain the exact driver build injected into boot.wim.
verify_winpe_b2lusb_package() {
  local VerifyDir ExpectedSys ManifestSys ExpectedMon ManifestMon
  local ExpectedMax77705Inf ManifestMax77705Inf
  local ExpectedMax77705 ManifestMax77705 ExpectedCtl ManifestCtl
  local ManifestAutoHost ManifestOffline AutoHostDir UiManifestDir UiScale UiPackage

  if [ ! -s "$PROJ/out/b2lusb/b2lusb.sys" ]; then
    if [ -s "$PROJ/esp/B2LUSB.EFI" ]; then
      echo "HATA: --usb-test turu var ama Windows B2LUSB paketi yok."
      echo "Once WSL'de: bash tools_b2lusb.sh"
      return 1
    fi
    return 0
  fi
  [ -s "$PROJ/out/b2lusb/b2lusbctl.exe" ] || {
    echo "HATA: b2lusb.sys var ama b2lusbctl.exe yok."
    return 1
  }
  [ -s "$PROJ/out/b2lusb/b2lusbmon.sys" ] || {
    echo "HATA: b2lusb.sys var ama b2lusbmon.sys yok."
    return 1
  }
  [ -s "$PROJ/out/b2lusb/b2lmax77705.sys" ] || {
    echo "HATA: b2lusb.sys var ama b2lmax77705.sys yok."
    return 1
  }
  [ -s "$PROJ/out/b2lusb/b2lmax77705.inf" ] || {
    echo "HATA: b2lusb.sys var ama b2lmax77705.inf yok."
    return 1
  }
  [ -s "$PROJ/esp/sources/boot.wim" ] || {
    echo "HATA: B2LUSB ciktilari var ama boot.wim yok. Once tools_b2lusb.sh calistir."
    return 1
  }
  command -v wimlib-imagex >/dev/null || {
    echo "HATA: B2LUSB WIM kapisi icin wimlib-imagex gerekli."
    return 1
  }

  VerifyDir="$(mktemp -d)" || return 1
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LUSB/manifest.txt --dest-dir="$VerifyDir" --no-acls >/dev/null; then
    rm -rf "$VerifyDir"
    echo "HATA: boot.wim'de B2LUSB manifest yok; eski WIM reddedildi."
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2lusb.sh"
    return 1
  fi

  ExpectedSys="$(sha256sum "$PROJ/out/b2lusb/b2lusb.sys" | awk '{print $1}')"
  # A mixed-runtime experiment is bound to the exact prepared WIM payload.
  # Packaging does not infer compatibility from a successful source-only audit.
  local RuntimeProfile RuntimeManifestHash
  RuntimeProfile="$(sed -n 's/^B2LUSB_RUNTIME_PROFILE=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  case "${RuntimeProfile:-strict}" in
    strict) ;;
    experimental-ufx)
      RuntimeManifestHash="$(sed -n 's/^B2LUSB_RUNTIME_MANIFEST_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
      if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
          /Windows/B2LUSB/experimental-ufx.json --dest-dir="$VerifyDir" --no-acls >/dev/null || \
          [ "$(sha256sum "$VerifyDir/experimental-ufx.json" | awk '{print $1}')" != "$RuntimeManifestHash" ]; then
        rm -rf "$VerifyDir"
        echo "HATA: UFX deneme manifesti eksik veya degismis."
        return 1
      fi
      # The function INF now owns this source binary as a real driver payload.
      # Verify the local drvload copy as well as the installed runtime below.
      if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
          /Windows/B2LUSB/ufxsynopsys.sys --dest-dir="$VerifyDir" --no-acls >/dev/null || \
          ! python3 - "$VerifyDir" <<'PYUFX'
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1])
record = json.loads((root/'experimental-ufx.json').read_text())
expected = record['files']['/Windows/System32/drivers/ufxsynopsys.sys']
if hashlib.sha256((root/'ufxsynopsys.sys').read_bytes()).hexdigest() != expected:
    raise SystemExit('HATA: function paket SYS hash farkli')
PYUFX
      then
        rm -rf "$VerifyDir"
        return 1
      fi
      if ! python3 "$PROJ/windows/b2lusb/check-winpe-runtime.py" "$PROJ/esp/sources/boot.wim" \
          --experimental-ufx-manifest "$VerifyDir/experimental-ufx.json"; then
        rm -rf "$VerifyDir"
        return 1
      fi
      ;;
    *) rm -rf "$VerifyDir"; echo "HATA: bilinmeyen USB runtime profili"; return 1 ;;
  esac
  ManifestSys="$(sed -n 's/^B2LUSB_SYS_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ExpectedMon="$(sha256sum "$PROJ/out/b2lusb/b2lusbmon.sys" | awk '{print $1}')"
  ManifestMon="$(sed -n 's/^B2LUSB_MON_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ExpectedMax77705Inf="$(sha256sum "$PROJ/out/b2lusb/b2lmax77705.inf" | awk '{print $1}')"
  ManifestMax77705Inf="$(sed -n 's/^B2LUSB_MAX77705_INF_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ExpectedMax77705="$(sha256sum "$PROJ/out/b2lusb/b2lmax77705.sys" | awk '{print $1}')"
  ManifestMax77705="$(sed -n 's/^B2LUSB_MAX77705_SYS_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ExpectedCtl="$(sha256sum "$PROJ/out/b2lusb/b2lusbctl.exe" | awk '{print $1}')"
  ManifestCtl="$(sed -n 's/^B2LUSB_CTL_SHA256=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ManifestAutoHost="$(sed -n 's/^B2LUSB_AUTO_HOST=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  ManifestOffline="$(sed -n 's/^B2LUSB_OFFLINE_DRIVERSTORE=//p' "$VerifyDir/manifest.txt" | head -n 1)"
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LUSB/offline-driverstore.txt --dest-dir="$VerifyDir" --no-acls >/dev/null; then
    rm -rf "$VerifyDir"
    echo "HATA: boot.wim'de B2LUSB offline DriverStore isareti yok."
    return 1
  fi
  if [ "$ManifestOffline" != "1" ] || \
     ! tr -d '\r' < "$VerifyDir/offline-driverstore.txt" | \
       grep -qx 'B2L_OFFLINE_DRIVERSTORE=1'; then
    rm -rf "$VerifyDir"
    echo "HATA: B2LUSB paketi DriverStore'a offline eklenmemis."
    return 1
  fi
  rm -rf "$VerifyDir"
  if [ "$ManifestSys" != "$ExpectedSys" ] || \
     [ "$ManifestMon" != "$ExpectedMon" ] || \
     [ "$ManifestMax77705Inf" != "$ExpectedMax77705Inf" ] || \
     [ "$ManifestMax77705" != "$ExpectedMax77705" ] || \
     [ "$ManifestCtl" != "$ExpectedCtl" ]; then
    echo "HATA: boot.wim B2LUSB hash'leri yerel ARM64 ciktilarla ayni degil."
    echo "  SYS WIM: ${ManifestSys:-YOK}"
    echo "  SYS OUT: $ExpectedSys"
    echo "  MON WIM: ${ManifestMon:-YOK}"
    echo "  MON OUT: $ExpectedMon"
    echo "  MIF WIM: ${ManifestMax77705Inf:-YOK}"
    echo "  MIF OUT: $ExpectedMax77705Inf"
    echo "  MAX WIM: ${ManifestMax77705:-YOK}"
    echo "  MAX OUT: $ExpectedMax77705"
    echo "  CTL WIM: ${ManifestCtl:-YOK}"
    echo "  CTL OUT: $ExpectedCtl"
    echo "Once: B2L_SKIP_MSVC_BUILD=1 bash tools_b2lusb.sh"
    return 1
  fi
  case "$ManifestAutoHost" in
    0) ;;
    1)
      AutoHostDir="$(mktemp -d)" || return 1
      if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
          /Windows/B2LUSB/auto-host.test \
          --dest-dir="$AutoHostDir" --no-acls >/dev/null; then
        rm -rf "$AutoHostDir"
        echo "HATA: --usb-test manifesti otomatik HOST diyor fakat marker WIM'de yok."
        return 1
      fi
      rm -rf "$AutoHostDir"
      ;;
    *)
      echo "HATA: B2LUSB_AUTO_HOST manifest degeri gecersiz: ${ManifestAutoHost:-YOK}"
      return 1
      ;;
  esac

  UiManifestDir="$(mktemp -d)" || return 1
  if ! wimlib-imagex extract "$PROJ/esp/sources/boot.wim" 1 \
      /Windows/B2LUI/manifest.txt \
      --dest-dir="$UiManifestDir" --no-acls >/dev/null; then
    rm -rf "$UiManifestDir"
    echo "HATA: boot.wim'de B2LUI ekran olcegi manifesti yok."
    return 1
  fi
  UiScale="$(sed -n 's/^B2LUI_SCALE_PERCENT=//p' "$UiManifestDir/manifest.txt" | head -n 1)"
  UiPackage="$(sed -n 's/^B2LUI_PACKAGE=//p' "$UiManifestDir/manifest.txt" | head -n 1)"
  rm -rf "$UiManifestDir"
  if [ "$UiScale" != "175" ] || [ "$UiPackage" != "winpe-display-scale-v1" ]; then
    echo "HATA: WinPE ekran olcegi %175 olarak dogrulanamadi."
    return 1
  fi

  say "WinPE B2LUSB Windows paketi dogrulandi"
  echo "  SYS SHA256: ${ExpectedSys:0:16}"
  echo "  MON SHA256: ${ExpectedMon:0:16}"
  echo "  MIF SHA256: ${ExpectedMax77705Inf:0:16}"
  echo "  MAX SHA256: ${ExpectedMax77705:0:16}"
  echo "  CTL SHA256: ${ExpectedCtl:0:16}"
  echo "  AUTO HOST : $ManifestAutoHost"
  echo "  UI SCALE  : %$UiScale"
}

if [ "$WINONLY" = 1 ] && [ "$DISKBOOT" = 0 ]; then
  verify_winpe_b2lufs_package
  verify_winpe_b2ltouch_package
  verify_winpe_b2lusb_package
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
if [ "$BASELINE" = 1 ]; then
  NSH="$PROJ/esp/startup-baseline.nsh"
fi

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
    [ "$b" = "startup-baseline.nsh" ] && continue
    [ -e "$f" ] || continue
    if [ "$BASELINE" = 1 ]; then
      case "$b" in
        B2LGOSTR.EFI|B2LUSB.EFI|B2LTOUCH.EFI|EBSTEST.EFI)
          echo "  - $b (atlandi: temiz taban)"
          continue
          ;;
      esac
    fi
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
  # RAM WinPE icin boot.wim gerekir; disk modunda ise bulunmasi hatadir.
  if [ "$DISKBOOT" = 1 ]; then
    mdir -i "$IMG" ::/EFI/Microsoft/Boot/BCD >/dev/null 2>&1 || {
      echo "HATA: disk ESP imajinda BCD yok."
      exit 1
    }
    if mdir -i "$IMG" ::/sources/boot.wim >/dev/null 2>&1; then
      echo "HATA: disk ESP imajina boot.wim sizdi."
      exit 1
    fi
    echo "--- disk boot ESP: BCD VAR, boot.wim YOK"
  elif mdir -i "$IMG" ::/sources >/dev/null 2>&1; then
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
TOUCH_BUNDLE=""
USB_BUNDLE=""
rm -f "$OUT/beyond2lte-usb-diag.tar"
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

# Dokunmatik tanisi iki bolumdeki iki dosyayi birlikte gerektirir:
# BOOT'ta B2LTouchDxe, SYSTEM ESP'de B2LTOUCH.EFI. Test uygulamasi mevcutsa
# ikisini tek Odin AP arsivinde birlestir. Boylece ayni turun iki ayri tar ile
# karistirilma riski kalmaz. Yalnizca daha once uretilen kendi BOOT arsimizi
# acar; cihazda veya baska bolumlerde herhangi bir islem yapmaz.
if [ "$BASELINE" = 0 ] && [ "$PART" = system ] && [ -s "$PROJ/esp/B2LTOUCH.EFI" ] && \
   [ -s "$OUT/uefi-boot.tar" ]; then
  if tar -tf "$OUT/uefi-boot.tar" | grep -qx 'boot.img'; then
    tar -xf "$OUT/uefi-boot.tar" -C "$WORK" boot.img
    TOUCH_BUNDLE="$OUT/beyond2lte-touch-diag.tar"
    ( cd "$WORK" && tar -H ustar -cf "$TOUCH_BUNDLE" boot.img system.img )
  else
    echo "HATA: out/uefi-boot.tar icinde boot.img yok; tek-flas tani paketi uretilmedi."
    rm -rf "$WORK"
    exit 1
  fi
fi

# USB tanisi da iki bolumu birlikte gerektirir: BOOT'taki platform memory map
# DWC3/PHY ve korunmus R8 HSI2C45 pencerelerini esler; R11 B2LUSB.EFI ise
# asamali olcum ve korumali HS-PHY/event-DMA probunu yapar. Iki dosyanin farkli
# turlardan kalmasini onlemek icin tek Odin AP arsivi uret.
if [ "$BASELINE" = 0 ] && [ "$PART" = system ] && [ -s "$PROJ/esp/B2LUSB.EFI" ]; then
  if [ ! -s "$OUT/uefi-boot.tar" ]; then
    echo "HATA: B2LUSB.EFI var ama out/uefi-boot.tar yok; once yap.sh --usb-test calistir."
    rm -rf "$WORK"
    exit 1
  elif tar -tf "$OUT/uefi-boot.tar" | grep -qx 'boot.img'; then
    tar -xf "$OUT/uefi-boot.tar" -C "$WORK" boot.img
    USB_BUNDLE="$OUT/beyond2lte-usb-diag.tar"
    ( cd "$WORK" && tar -H ustar -cf "$USB_BUNDLE" boot.img system.img )
  else
    echo "HATA: out/uefi-boot.tar icinde boot.img yok; USB tek-flas tani paketi uretilmedi."
    rm -rf "$WORK"
    exit 1
  fi
fi
rm -rf "$WORK"

say "Cikti"
ls -lh "$IMG" "$OUT/$PART-esp.tar"
if [ -n "$TOUCH_BUNDLE" ]; then
  ls -lh "$TOUCH_BUNDLE"
  echo "  TEK FLAS: beyond2lte-touch-diag.tar -> BOOT + SYSTEM"
fi
if [ -n "$USB_BUNDLE" ]; then
  ls -lh "$USB_BUNDLE"
  echo "  TEK FLAS: beyond2lte-usb-diag.tar -> BOOT + SYSTEM"
fi

for p in /mnt/c/Users/*/OneDrive/Masa*st* /mnt/c/Users/*/OneDrive/Desktop \
         /mnt/c/Users/*/Masa*st* /mnt/c/Users/*/Desktop; do
  [ -d "$p" ] || continue
  mkdir -p "$p/beyond2lte-flash"
  cp -f "$OUT/$PART-esp.tar" "$IMG" "$p/beyond2lte-flash/"
  if [ -n "$TOUCH_BUNDLE" ]; then
    cp -f "$TOUCH_BUNDLE" "$p/beyond2lte-flash/"
  fi
  if [ -n "$USB_BUNDLE" ]; then
    cp -f "$USB_BUNDLE" "$p/beyond2lte-flash/"
  fi
  echo
  echo "Masaustune kopyalandi: $p/beyond2lte-flash"
  echo "  $PART-esp.tar  -> Odin, AP slotu (Re-Partition ISARETSIZ)"
  echo "  $PART.img      -> recovery: adb push + dd"
  echo "  hedef bolum: $(echo "$PART" | tr a-z A-Z)"
  if [ -n "$TOUCH_BUNDLE" ]; then
    echo "  beyond2lte-touch-diag.tar -> TEK Odin AP flasi (BOOT + SYSTEM)"
  fi
  if [ -n "$USB_BUNDLE" ]; then
    echo "  beyond2lte-usb-diag.tar -> TEK Odin AP flasi (BOOT + SYSTEM)"
  fi
  break
done
