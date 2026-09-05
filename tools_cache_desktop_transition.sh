#!/usr/bin/env bash
#
# Klavyesiz cihaz icin tek kullanimlik WinPE SYSTEM imaji uretir. WinPE
# acilinca CACHE'i Disk 0 / Partition 28 olarak W: harfine baglar, mevcut
# BCD'yi yedekler, winpe degerini siler, normal winload.efi yolunu yazar ve
# telefonu kapatir. CACHE imaji uretmez ve CACHE partitionina flash atmaz.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_WIM="$HERE/esp/sources/boot.wim"
SOURCE_SYSTEM="$HERE/out/system.img"
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

if [ -s "$SOURCE_SYSTEM" ] && \
   [ "$(dd if="$SOURCE_SYSTEM" bs=1 skip=3 count=8 status=none)" = "NTFS    " ]; then
  # Disk-WinPE'den kalan uygulanmis system.img en hizli ve en guvenli
  # kaynaktir. USERDATA Desktop uretimi bu dosyaya dokunmaz.
  for tool in ntfscp ntfscat; do
    command -v "$tool" >/dev/null || die "$tool yok -> sudo apt install -y ntfs-3g"
  done
  say "Mevcut disk-WinPE SYSTEM imaji gecis icin kopyalaniyor"
  cp --sparse=always "$SOURCE_SYSTEM" "$IMAGE"
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
  [ -s "$SOURCE_WIM" ] || \
    die "ne disk-WinPE out/system.img ne de esp/sources/boot.wim bulundu"
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
