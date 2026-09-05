#!/usr/bin/env bash
#
# B2LUFS boot-start WinPE'yi RAM diskten degil GERCEK UFS/NTFS bolumunden
# acmak icin SYSTEM imaji uretir.
#
#   bash tools_windisk.sh [boyut_GiB]      varsayilan 3
#
# ---------------------------------------------------------------------------
# NEDEN - FIKIR KULLANICIDAN GELDI VE HAKLIYDI
# ---------------------------------------------------------------------------
# "Diske erisip erisemedigini tek tek test ediyoruz. Diske yazsak ve surucu
# basarisiz olsa acilmaz ve calismadigini anlariz."
#
# Bunu once elemistim ("diskten acmak diskin zaten calismasini gerektirir"),
# ama itiraz dogru: BASARISIZLIGIN KENDISI SINYAL.
#
# RAM diskten acarken depolama miniportunun yuklenmesi acilis icin zorunlu
# degildi. Artik b2lufs offline DriverStore'da Start=0 ve SCSI Miniport
# grubundadir; winload surucuyu cekirdege gecmeden belleğe alir.
#
# Diskten acarken b2lufs baglanmazsa Windows ACILIS BIRIMINI BULAMAZ ve
# EKRANA HATA BASAR. Bunun calistigini biliyoruz: ws2ifsl.sys denemesinde
# "0xc0000428 / digital signature couldn't be verified" ekranini gordük.
# winload'un hata yolu saglam; sorun ona ulasacak bir hata olmamasiydi.
#
# Yani bu degisiklik GOZLEMLENEMEZ bir soruyu GORUNUR bir hataya ceviriyor.
#
# ---------------------------------------------------------------------------
# BOLUM VE BICIM
# ---------------------------------------------------------------------------
# Hedef SYSTEM (5.57 GB). Odin hedefi tar icindeki dosya adiyla belirlendigi
# icin dosya "system.img" olmak zorunda.
#
# NTFS sektor ve kume boyutu 4096: UFS'in mantiksal blok boyutu 4096 ve
# bootmgr blok G/C uzerinden okuyacak. Uyumsuz sektor boyutu okumayi
# bastan bozar.
#
# UEFI'de NTFS surucusu OLMAMASI sorun degil - bootmgr'in kendi NTFS
# okuyucusu var, dosya sistemini UEFI'den bagimsiz okuyor.
#
# ---------------------------------------------------------------------------
# BCD TARAFI
# ---------------------------------------------------------------------------
# Windows makinesinde:  windows\bcd_olustur.cmd disk
#
# O varyant ramdisk yerine "locate=" kullaniyor: bootmgr birimleri kendisi
# tarayip \Windows\System32\winload.efi'yi buluyor. Boylece GPT bolum
# GUID'ini elle yazmak zorunda kalmiyoruz - hangi bolume yazarsak yazalim
# calisiyor.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"
OUT="$HERE/out"; mkdir -p "$OUT"
GIB="${1:-3}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "esp/sources/boot.wim yok. Once tools_winboot.sh calistir."; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok -> sudo apt install -y wimtools"; exit 1; }
command -v mkntfs        >/dev/null || { echo "mkntfs yok -> sudo apt install -y ntfs-3g"; exit 1; }

# Disk imajini yalniz gercek offline boot paketi tasiyan WIM'den uret.
# /Windows/B2LUFS altindaki INF/SYS kopyalari tek basina yeterli degildir;
# bu isaret ancak Windows DISM, SYSTEM hive Start=0/Group degerlerini ve
# System32/drivers hash'ini dogruladiktan sonra yazilir.
VERIFY_DIR="$(mktemp -d)"
trap 'rm -rf "$VERIFY_DIR"' EXIT
if ! wimlib-imagex extract "$WIM" 1 /Windows/B2LUFS/offline-driverstore.txt \
    --dest-dir="$VERIFY_DIR" --no-acls >/dev/null; then
  echo "HATA: boot.wim'de B2LUFS offline boot-start isareti yok."
  echo "Once: bash tools_b2lufs.sh"
  exit 1
fi
for EXPECTED in \
  'B2L_OFFLINE_DRIVERSTORE=1' \
  'B2L_OFFLINE_PACKAGE=UFS' \
  'B2L_BOOT_START=1' \
  'B2L_LOAD_ORDER_GROUP=SCSI Miniport' \
  'B2L_ERROR_CONTROL=3'; do
  tr -d '\r' < "$VERIFY_DIR/offline-driverstore.txt" | grep -Fqx "$EXPECTED" || {
    echo "HATA: boot.wim B2LUFS boot kaydi eksik: $EXPECTED"
    exit 1
  }
done
rm -rf "$VERIFY_DIR"
trap - EXIT
echo "  B2LUFS offline boot-start paketi dogrulandi"

IMG="$OUT/system.img"

say "NTFS imaji olusturuluyor (${GIB} GiB, 4096 bayt sektor)"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=$((GIB*1024)) status=none
#
# -Q hizli bicimlendirme (kotu blok taramasi yok), -F dosyaya yazmayi zorla.
# Sektor ve kume 4096: UFS mantiksal blok boyutuyla ayni olmali.
#
#
# mkntfs "Windows will not be able to boot from this device" diye uyaracak.
# ZARARSIZ: o uyari BPB'deki HiddenSectors ve CHS alanlarinin bos kalmasiyla
# ilgili ve bunlar ESKI BIOS onyukleme kodu icin. UEFI'de bootmgr bolumu
# UEFI blok G/C uzerinden aciyor ve kendi NTFS okuyucusuyla bolumun basindan
# itibaren okuyor; o alanlara hic bakmiyor.
#
mkntfs -Q -F -s 4096 -c 4096 -L WINPE "$IMG" 2>&1 | grep -vi "not a block device\|has been set to 0\|was not specified\|will not be able to boot\|forced anyway" || true
echo "  hazir: $(du -h "$IMG" | cut -f1)"

say "boot.wim NTFS'e aciliyor"
echo "  (birkac dakika surer, ~1.6 GB dosya)"
#
# wimlib NTFS-3g destegiyle derlenmisse imaja DOGRUDAN acabiliyor ve
# Windows'a ozel ustverileri (guvenlik tanimlayicilari, yeniden ayirma
# noktalari, kisa adlar) koruyor. Klasore acip kopyalamak bunlari kaybettirir
# ve Windows acilmaz.
#
if ! wimlib-imagex apply "$WIM" 1 "$IMG"; then
  echo
  echo "  NTFS'e dogrudan acma basarisiz."
  echo "  wimlib ntfs-3g destegi olmadan derlenmis olabilir:"
  echo "    sudo apt install -y wimtools ntfs-3g libntfs-3g-dev"
  exit 1
fi

#
# ICERIK DOGRULAMA
#
# ONCEKI SURUM YANILTICIYDI: "ntfscat | head" kullaniyordu ve ntfscat kurulu
# olmadiginda "set -e"/pipefail yuzunden uc satirin da OKUNAMADI gorunmesine
# yol acti. Oysa asil kanit hemen ustundeydi - wimlib 24932 dosyayi ve
# 1381 MiB veriyi yazdigini bildirmisti.
#
# Simdi ntfsls tercih ediliyor; yoksa dogrulama ATLANIYOR ve bunu acikca
# soyluyoruz. Yanlis alarm, alarm olmamasindan kotudur.
#
say "Icerik dogrulaniyor"
if command -v ntfsls >/dev/null; then
  for d in /Windows/System32 /Windows/System32/config; do
    N="$(ntfsls -p "$d" "$IMG" 2>/dev/null | wc -l)"
    printf '  %-32s %s girdi\n' "$d" "$N"
  done
  for f in winload.efi ntoskrnl.exe; do
    if ntfsls -p /Windows/System32 "$IMG" 2>/dev/null | grep -qi "^$f$"; then
      echo "  + $f"
    else
      echo "  ! $f LISTEDE YOK"
    fi
  done
else
  echo "  ntfsls yok, dogrulama atlandi -> sudo apt install -y ntfs-3g"
  echo "  wimlib'in kendi raporu gecerli: yukarida 'Done applying WIM image'"
fi

say "Odin icin sparse imaj + tar"
WORK="$(mktemp -d)"
if command -v img2simg >/dev/null; then
  img2simg "$IMG" "$WORK/system.img"
  MAGIC=$(od -An -tx4 -N4 "$WORK/system.img" | tr -d ' ')
  echo "  sparse sihirli sayisi: 0x$MAGIC  (beklenen ed26ff3a)"
else
  echo "  UYARI: img2simg yok -> sudo apt install -y android-sdk-libsparse-utils"
  cp "$IMG" "$WORK/system.img"
fi
( cd "$WORK" && tar -H ustar -cf "$OUT/system-esp.tar" system.img )
rm -rf "$WORK"

ls -lh "$OUT/system-esp.tar"
for p in /mnt/c/Users/*/OneDrive/Masa*st* /mnt/c/Users/*/OneDrive/Desktop \
         /mnt/c/Users/*/Masa*st* /mnt/c/Users/*/Desktop; do
  [ -d "$p" ] || continue
  mkdir -p "$p/beyond2lte-flash"
  cp -f "$OUT/system-esp.tar" "$p/beyond2lte-flash/"
  echo
  echo "Masaustune kopyalandi: $p/beyond2lte-flash/system-esp.tar"
  echo "  Odin, AP slotu (Re-Partition ISARETSIZ) -> SYSTEM bolumu"
  break
done

cat <<'SON'

SIRA:
  1) Windows'ta:  windows\bcd_olustur.cmd disk
     BCD'yi masaustunden kopyala:  cp .../Desktop/BCD windows/BCD
  2) ESP'yi guncelle (bootmgfw + yeni BCD, RAM diski yok):
        bash tools_winboot.sh <ISO> --disk
        bash cache_esp.sh --win --disk --baseline
  3) BOOT + NTFS SYSTEM + disk ESP'yi tek AP arsivine al:
        bash tools_odin_winpe_bundle.sh --disk
     Odin AP: out/beyond2lte-winpe-ufs-disk.tar

BEKLENEN OKUMA:
  Windows aciliyor            -> b2lufs boot-start + UFS/NTFS yolu calisiyor
  "Inaccessible boot device"  -> b2lufs acilis birimini tasiyamiyor
  Baska bir 0xc0000... kodu   -> winload tam olarak neyi bulamadigini soyluyor

  Ucu de sessizlikten iyi.
SON
