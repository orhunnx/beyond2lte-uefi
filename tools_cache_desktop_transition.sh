#!/usr/bin/env bash
#
# Klavyesiz cihaz icin tek kullanimlik WinPE SYSTEM imaji uretir. WinPE
# acilinca CACHE'i Disk 0 / Partition 28 olarak W: harfine baglar, mevcut
# BCD'yi yedekler, winpe degerini siler, normal winload.efi yolunu yazar ve
# telefonu kapatir. CACHE imaji uretmez ve CACHE partitionina flash atmaz.
# Istege bagli ilk arguman ham disk-WinPE system.img veya onu tasiyan eski
# Odin .tar arsividir. Arguman yoksa bilinen out/Masaustu yollarini arar.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_WIM="$HERE/esp/sources/boot.wim"
SOURCE_SYSTEM="$HERE/out/system.img"
SOURCE_OVERRIDE="${1:-}"
STARTNET="$HERE/windows/startnet-cache-desktop.cmd"
OUT="$HERE/out"
IMAGE="$OUT/system-cache-bcd-transition.img"
TAR="$OUT/beyond2lte-cache-bcd-transition.tar"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for tool in img2simg sha256sum; do
  command -v "$tool" >/dev/null || die "$tool yok"
done
[ -s "$STARTNET" ] || die "otomatik gecis startnet'i yok: $STARTNET"

mkdir -p "$OUT"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
CRLF="$WORK/startnet.cmd"
sed 's/\r$//; s/$/\r/' "$STARTNET" > "$CRLF"

CHECK="$WORK/check"
mkdir -p "$CHECK/startnet" "$CHECK/ufs"
rm -f "$IMAGE" "$TAR"

system_is_ntfs() {
  [ -s "$1" ] && [ "$(dd if="$1" bs=1 skip=3 count=8 status=none)" = "NTFS    " ]
}

archive_has_system() {
  [ -s "$1" ] && [ "$(tar -tf "$1" 2>/dev/null | grep -Fxc 'system.img' || true)" = 1 ]
}

WINPE_SYSTEM=""
SOURCE_LABEL=""
if [ -n "$SOURCE_OVERRIDE" ]; then
  [ -s "$SOURCE_OVERRIDE" ] || die "verilen disk-WinPE kaynagi yok ya da bos: $SOURCE_OVERRIDE"
  if system_is_ntfs "$SOURCE_OVERRIDE"; then
    WINPE_SYSTEM="$SOURCE_OVERRIDE"
    SOURCE_LABEL="$SOURCE_OVERRIDE"
  elif archive_has_system "$SOURCE_OVERRIDE"; then
    RECOVERY_ARCHIVE="$SOURCE_OVERRIDE"
  else
    die "verilen kaynak ne ham NTFS system.img ne de system.img tasiyan tar: $SOURCE_OVERRIDE"
  fi
elif system_is_ntfs "$SOURCE_SYSTEM"; then
  WINPE_SYSTEM="$SOURCE_SYSTEM"
  SOURCE_LABEL="$SOURCE_SYSTEM"
else
  RECOVERY_ARCHIVE=""
  for candidate in \
    "$OUT/beyond2lte-winpe-ufs-disk.tar" \
    "$OUT/system-esp.tar" \
    /mnt/c/Users/*/OneDrive/Masa*st*/beyond2lte-flash/beyond2lte-winpe-ufs-disk.tar \
    /mnt/c/Users/*/OneDrive/Desktop/beyond2lte-flash/beyond2lte-winpe-ufs-disk.tar \
    /mnt/c/Users/*/Masa*st*/beyond2lte-flash/beyond2lte-winpe-ufs-disk.tar \
    /mnt/c/Users/*/Desktop/beyond2lte-flash/beyond2lte-winpe-ufs-disk.tar; do
    if archive_has_system "$candidate"; then
      RECOVERY_ARCHIVE="$candidate"
      break
    fi
  done
fi

if [ -n "${RECOVERY_ARCHIVE:-}" ]; then
  command -v simg2img >/dev/null || die "eski Odin arsivini acmak icin simg2img yok -> sudo apt install -y android-sdk-libsparse-utils"
  say "Disk-WinPE SYSTEM eski Odin arsivinden kurtariliyor"
  echo "  kaynak: $RECOVERY_ARCHIVE"
  RECOVERED_SPARSE="$WORK/system.winpe.sparse.img"
  RECOVERED_RAW="$WORK/system.winpe.raw.img"
  tar -xOf "$RECOVERY_ARCHIVE" system.img > "$RECOVERED_SPARSE" || \
    die "eski Odin arsivinden system.img cikarilamadi"
  [ "$(od -An -tx4 -N4 "$RECOVERED_SPARSE" | tr -d '[:space:]')" = ed26ff3a ] || \
    die "eski Odin arsivindeki system.img Android sparse degil"
  simg2img "$RECOVERED_SPARSE" "$RECOVERED_RAW"
  system_is_ntfs "$RECOVERED_RAW" || \
    die "eski Odin arsivindeki SYSTEM disk-WinPE NTFS imaji degil"
  WINPE_SYSTEM="$RECOVERED_RAW"
  SOURCE_LABEL="$RECOVERY_ARCHIVE -> system.img"
fi

if [ -n "$WINPE_SYSTEM" ]; then
  # Disk-WinPE'den kalan uygulanmis system.img en hizli ve en guvenli
  # kaynaktir. Ham imaj silinmisse eski Odin AP arsivinden geri acilir.
  for tool in ntfscp ntfscat; do
    command -v "$tool" >/dev/null || die "$tool yok -> sudo apt install -y ntfs-3g"
  done
  say "Mevcut disk-WinPE SYSTEM imaji gecis icin kopyalaniyor"
  echo "  kaynak: $SOURCE_LABEL"
  cp --sparse=always "$WINPE_SYSTEM" "$IMAGE"
  ntfscat "$IMAGE" /Windows/B2LUFS/offline-driverstore.txt > "$CHECK/ufs/offline-driverstore.txt" || \
    die "mevcut SYSTEM imajinda B2LUFS offline boot-start isareti yok"
  for expected in B2L_BOOT_START=1 'B2L_LOAD_ORDER_GROUP=SCSI Miniport'; do
    tr -d '\r' < "$CHECK/ufs/offline-driverstore.txt" | grep -Fqx "$expected" || \
      die "SYSTEM B2LUFS isareti eksik: $expected"
  done
  say "Tek kullanimlik otomatik startnet NTFS'e yaziliyor"
  ntfscp -f "$IMAGE" "$CRLF" /Windows/System32/startnet.cmd
  ntfscat "$IMAGE" /Windows/System32/startnet.cmd > "$CHECK/startnet/startnet.cmd" || \
    die "otomatik startnet NTFS'ten geri okunamadi"
else
  for tool in wimlib-imagex mkntfs; do
    command -v "$tool" >/dev/null || die "$tool yok"
  done
  if [ ! -s "$SOURCE_WIM" ]; then
    cat >&2 <<EOF

HATA: disk-WinPE kaynagi bulunamadi.
Eski calisan AP arsivini su adla geri kopyala:
  out/beyond2lte-winpe-ufs-disk.tar
veya yolu dogrudan ver:
  bash tools_cache_desktop_transition.sh /mnt/c/.../beyond2lte-winpe-ufs-disk.tar
EOF
    exit 1
  fi
  WIM="$WORK/transition.wim"
  cp "$SOURCE_WIM" "$WIM"
  say "Tek kullanimlik otomatik CACHE BCD gecisi WIM'e enjekte ediliyor"
  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/System32/startnet.cmd" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$CRLF' /Windows/System32/startnet.cmd" >/dev/null
  wimlib-imagex extract "$WIM" 1 /Windows/System32/startnet.cmd \
    --dest-dir="$CHECK/startnet" --no-acls >/dev/null
  wimlib-imagex extract "$WIM" 1 /Windows/B2LUFS/offline-driverstore.txt \
    --dest-dir="$CHECK/ufs" --no-acls >/dev/null || \
    die "WIM'de B2LUFS offline boot-start isareti yok"
  for expected in B2L_BOOT_START=1 'B2L_LOAD_ORDER_GROUP=SCSI Miniport'; do
    tr -d '\r' < "$CHECK/ufs/offline-driverstore.txt" | grep -Fqx "$expected" || \
      die "WIM B2LUFS isareti eksik: $expected"
  done
  wimlib-imagex verify "$WIM" >/dev/null || die "gecis WIM'i bozuk"
  say "Gecici NTFS SYSTEM imaji olusturuluyor"
  truncate -s 3072M "$IMAGE"
  mkntfs -Q -F -s 4096 -c 4096 -L WINPEBCD "$IMAGE" 2>&1 | \
    grep -vi "not a block device\|has been set to 0\|was not specified\|will not be able to boot\|forced anyway" || true
  wimlib-imagex apply "$WIM" 1 "$IMAGE"
fi

[ "$(sha256sum "$CRLF" | awk '{print $1}')" = \
  "$(sha256sum "$CHECK/startnet/startnet.cmd" | awk '{print $1}')" ] || \
  die "otomatik startnet geri okuma hash'i farkli"

say "Yalniz SYSTEM iceren Odin arsivi uretiliyor"
img2simg "$IMAGE" "$WORK/system.img"
[ "$(od -An -tx4 -N4 "$WORK/system.img" | tr -d '[:space:]')" = ed26ff3a ] || \
  die "SYSTEM Android sparse basligi gecersiz"
(
  cd "$WORK"
  tar -H ustar -cf "$TAR" system.img
)
[ "$(tar -tf "$TAR" | sed '/^$/d')" = system.img ] || \
  die "gecis arsivi yalniz system.img icermiyor"

say "Klavyesiz CACHE BCD gecis paketi hazir"
ls -lh "$TAR"
cat <<'EOF'

1) Odin AP: out/beyond2lte-cache-bcd-transition.tar
   Re-Partition KAPALI. Arsiv yalniz SYSTEM'e yazilir; CACHE'e dokunmaz.
2) Telefon WinPE'yi otomatik acar, CACHE BCD'yi donusturur ve kapanir.
   Yesil BASARILI ekrani gorulmeden sonraki asamaya gecme.
3) Kapandiktan sonra normal acma; Download Mode'a gir.
4) WSL: bash tools_odin_winpe_bundle.sh --desktop --target userdata
5) Odin AP: out/beyond2lte-windows-desktop-userdata.tar
EOF
