#!/usr/bin/env bash
#
# UFS WinPE turu icin BOOT + SYSTEM + CACHE temizleme imajlarini tek Odin AP
# arsivinde birlestirir.
#
# NEDEN: Bu turda iki farkli katman degisiyor:
#   boot.img   -> UEFI + etkin UFS ACPI tanimi
#   system.img -> WinPE boot.wim + girissiz PnP/UFS raporu
#   cache.img  -> eski startup.nsh/ESP'nin secilmesini onlemek icin bosaltma
#
# Odin hedef bolumu tar ICINDEKI dosya adi ile belirler. Bu betik mevcut
# artefaktlari degistirmez; yalniz isimleri dogrulanmis yeni bir ustar arsivi
# uretir.
#
# Kullanim (yap.sh, tools_b2lfb.sh ve iki cache_esp.sh cagrisindan sonra):
#   bash tools_odin_winpe_bundle.sh
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
BOOT_TAR="${1:-$OUT/uefi-boot.tar}"
SYSTEM_TAR="${2:-$OUT/system-esp.tar}"
CACHE_TAR="${3:-$OUT/cache-esp.tar}"
BUNDLE="$OUT/beyond2lte-winpe-ufs.tar"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { echo "HATA: $*" >&2; exit 1; }

for F in "$BOOT_TAR" "$SYSTEM_TAR" "$CACHE_TAR"; do
  [ -s "$F" ] || fail "gerekli arsiv yok ya da bos: $F"
done

command -v tar >/dev/null || fail "tar yok"

say "Girdiler"
printf '  BOOT:   %s\n' "$BOOT_TAR"
printf '  SYSTEM: %s\n' "$SYSTEM_TAR"
printf '  CACHE:  %s\n' "$CACHE_TAR"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

only_entry() {
  local ARCH="$1" EXPECT="$2" ACTUAL
  ACTUAL="$(tar -tf "$ARCH" | sed '/^$/d')"
  [ "$ACTUAL" = "$EXPECT" ] || fail "$ARCH icinde yalniz '$EXPECT' bekleniyordu; bulunan: ${ACTUAL:-<bos>}"
}

only_entry "$BOOT_TAR" boot.img
only_entry "$SYSTEM_TAR" system.img
only_entry "$CACHE_TAR" cache.img

tar -xf "$BOOT_TAR" -C "$WORK"
tar -xf "$SYSTEM_TAR" -C "$WORK"
tar -xf "$CACHE_TAR" -C "$WORK"

for F in boot.img system.img cache.img; do
  [ -s "$WORK/$F" ] || fail "cikarilan $F yok ya da bos"
done

say "Tek AP arsivi uretiliyor"
rm -f "$BUNDLE"
(
  cd "$WORK"
  tar -H ustar -cf "$BUNDLE" boot.img system.img cache.img
)

say "Dogrulama"
EXPECTED=$'boot.img\nsystem.img\ncache.img'
ACTUAL="$(tar -tf "$BUNDLE" | sed '/^$/d')"
[ "$ACTUAL" = "$EXPECTED" ] || fail "cikti arsiv hedef listesi beklenenden farkli: $ACTUAL"
tar -tvf "$BUNDLE"
sha256sum "$BUNDLE"

for DESK in /mnt/c/Users/*/OneDrive/Masa*st* \
            /mnt/c/Users/*/OneDrive/Desktop \
            /mnt/c/Users/*/Masa*st* \
            /mnt/c/Users/*/Desktop; do
  [ -d "$DESK" ] || continue
  mkdir -p "$DESK/beyond2lte-flash"
  cp -f "$BUNDLE" "$DESK/beyond2lte-flash/"
  echo
  echo "Masaustune kopyalandi: $DESK/beyond2lte-flash/$(basename "$BUNDLE")"
  break
done

cat <<'SON'

Hazir: out/beyond2lte-winpe-ufs.tar
  Odin -> AP: bu TEK dosya
  Re-Partition: KAPALI

Arsiv yalnizca boot.img, system.img ve cache.img icerir.
SON
