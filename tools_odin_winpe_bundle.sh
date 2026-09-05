#!/usr/bin/env bash
#
# UFS WinPE turu icin BOOT + SYSTEM + CACHE temizleme imajlarini tek Odin AP
# arsivinde birlestirir.
#
# NEDEN: Bu turda iki farkli katman degisiyor:
#   boot.img   -> UEFI + etkin UFS ACPI tanimi (ham firmware imaji)
#   system.img -> varsayilan: WinPE boot.wim tasiyan FAT ESP
#                 --disk: B2LUFS boot-start WinPE tasiyan NTFS sistem birimi
#   cache.img  -> varsayilan: bos FAT32 CACHE
#                 --disk: bootmgfw + disk BCD tasiyan FAT ESP
#
# Odin, FAT imajlarini ham halde reddedebiliyor. Bu betik SYSTEM ve CACHE
# kaynak imajlarini bizzat img2simg ile sparse'e cevirir; sparse basligini ve
# son tar'dan cikarilan baytlarin hash'ini dogrular. BOOT kasitli olarak ham
# kalir: daha once calisan uefi-boot.tar da boot.img'yi bu bicimde tasiyordu.
#
# Kullanim (yap.sh, tools_b2lfb.sh ve iki cache_esp.sh cagrisindan sonra):
#   bash cache_esp.sh --win --system
#   bash cache_esp.sh --win --bos
#   bash tools_odin_winpe_bundle.sh
# Diskten boot icin:
#   bash tools_odin_winpe_bundle.sh --disk
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
DISK_MODE=0
DESKTOP_MODE=0
TARGET=system
ARGS=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --disk) DISK_MODE=1; shift ;;
    --desktop) DISK_MODE=1; DESKTOP_MODE=1; shift ;;
    --target)
      [ "$#" -ge 2 ] || { echo "HATA: --target degeri eksik" >&2; exit 1; }
      TARGET="$2"; shift 2 ;;
    --target=*) TARGET="${1#*=}"; shift ;;
    *) ARGS+=("$1"); shift ;;
  esac
done
case "$TARGET" in system|userdata) ;; *) echo "HATA: hedef yalniz system veya userdata olabilir" >&2; exit 1 ;; esac
if [ "$DESKTOP_MODE" = 0 ] && [ "$TARGET" != system ]; then
  echo "HATA: --target userdata yalniz --desktop ile kullanilir" >&2
  exit 1
fi
BOOT_TAR="${ARGS[0]:-$OUT/uefi-boot.tar}"
TARGET_ENTRY="$TARGET.img"
SYSTEM_RAW="${ARGS[1]:-$OUT/$TARGET_ENTRY}"
CACHE_RAW="${ARGS[2]:-$OUT/cache.img}"
if [ "$DESKTOP_MODE" = 1 ]; then
  BUNDLE="$OUT/beyond2lte-windows-desktop-$TARGET.tar"
elif [ "$DISK_MODE" = 1 ]; then
  BUNDLE="$OUT/beyond2lte-winpe-ufs-disk.tar"
else
  BUNDLE="$OUT/beyond2lte-winpe-ufs.tar"
fi

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { echo "HATA: $*" >&2; exit 1; }

[ -s "$BOOT_TAR" ] || fail "BOOT arsivi yok ya da bos: $BOOT_TAR"
for F in "$SYSTEM_RAW" "$CACHE_RAW"; do
  [ -s "$F" ] || fail "ham ESP imaji yok ya da bos: $F"
done

command -v tar >/dev/null || fail "tar yok"
command -v img2simg >/dev/null || fail "img2simg yok -> sudo apt install -y android-sdk-libsparse-utils"

if [ "$DISK_MODE" = 1 ]; then
  command -v mdir >/dev/null || fail "--disk CACHE ESP dogrulamasi icin mtools/mdir gerekli"
  NTFS_MAGIC="$(dd if="$SYSTEM_RAW" bs=1 skip=3 count=8 status=none)"
  [ "$NTFS_MAGIC" = "NTFS    " ] || fail "--disk SYSTEM imaji NTFS degil"
  MTOOLS_SKIP_CHECK=1 mdir -i "$CACHE_RAW" ::/EFI/Microsoft/Boot/BCD >/dev/null 2>&1 || \
    fail "--disk CACHE imajinda EFI/Microsoft/Boot/BCD yok"
  if MTOOLS_SKIP_CHECK=1 mdir -i "$CACHE_RAW" ::/sources/boot.wim >/dev/null 2>&1; then
    fail "--disk CACHE ESP hala boot.wim tasiyor; RAM disk imaji reddedildi"
  fi
  say "DISK MODU dogrulandi"
  echo "  ${TARGET^^}: NTFS"
  echo "  CACHE: bootmgfw/BCD ESP, boot.wim yok"
fi
if [ "$DESKTOP_MODE" = 1 ]; then
  MARKER="$OUT/windows-desktop-target.txt"
  [ -s "$MARKER" ] || fail "Desktop hedef isareti yok: $MARKER"
  grep -Fqx 'B2L_WINDOWS_DESKTOP=1' "$MARKER" || fail "Desktop hedef isareti gecersiz"
  grep -Fqx "B2L_TARGET=$TARGET" "$MARKER" || fail "Desktop hedef isareti $TARGET ile uyusmuyor"
  grep -Fqx "B2L_IMAGE=$TARGET_ENTRY" "$MARKER" || fail "Desktop imaj adi hedef isaretiyle uyusmuyor"
  say "WINDOWS DESKTOP MODU dogrulandi"
  echo "  Windows birimi: ${TARGET^^} ($TARGET_ENTRY)"
fi

say "Girdiler"
printf '  BOOT arsivi: %s\n' "$BOOT_TAR"
printf '  %-12s %s\n' "${TARGET^^} ham:" "$SYSTEM_RAW"
printf '  CACHE ham:    %s\n' "$CACHE_RAW"

WORK="$(mktemp -d)"
CHECK="$WORK/check"
trap 'rm -rf "$WORK"' EXIT

only_entry() {
  local ARCH="$1" EXPECT="$2" ACTUAL
  ACTUAL="$(tar -tf "$ARCH" | sed '/^$/d')"
  [ "$ACTUAL" = "$EXPECT" ] || fail "$ARCH icinde yalniz '$EXPECT' bekleniyordu; bulunan: ${ACTUAL:-<bos>}"
}

sparse_magic() {
  od -An -tx4 -N4 "$1" | tr -d '[:space:]'
}

require_sparse() {
  local IMAGE="$1" MAGIC
  MAGIC="$(sparse_magic "$IMAGE")"
  [ "$MAGIC" = "ed26ff3a" ] || fail "sparse basligi gecersiz: $IMAGE (0x${MAGIC:-<bos>})"
  printf '  %s sparse sihirli sayisi: 0x%s\n' "$(basename "$IMAGE")" "$MAGIC"
}

only_entry "$BOOT_TAR" boot.img
tar -xf "$BOOT_TAR" -C "$WORK"
[ -s "$WORK/boot.img" ] || fail "BOOT arsivinden boot.img cikarilamadi"

say "${TARGET^^} ve CACHE Android sparse'e cevriliyor"
img2simg "$SYSTEM_RAW" "$WORK/$TARGET_ENTRY"
img2simg "$CACHE_RAW" "$WORK/cache.img"
require_sparse "$WORK/$TARGET_ENTRY"
require_sparse "$WORK/cache.img"

say "Tek AP arsivi uretiliyor"
rm -f "$BUNDLE"
(
  cd "$WORK"
  tar -H ustar -cf "$BUNDLE" boot.img "$TARGET_ENTRY" cache.img
)

say "Dogrulama"
EXPECTED="$(printf 'boot.img\n%s\ncache.img' "$TARGET_ENTRY")"
ACTUAL="$(tar -tf "$BUNDLE" | sed '/^$/d')"
[ "$ACTUAL" = "$EXPECTED" ] || fail "cikti arsiv hedef listesi beklenenden farkli: $ACTUAL"
mkdir "$CHECK"
tar -xf "$BUNDLE" -C "$CHECK"
for F in boot.img "$TARGET_ENTRY" cache.img; do
  BEFORE="$(sha256sum "$WORK/$F" | awk '{print $1}')"
  AFTER="$(sha256sum "$CHECK/$F" | awk '{print $1}')"
  [ "$BEFORE" = "$AFTER" ] || fail "tar icinde $F degismis"
  printf '  %-10s hash esit: %s\n' "$F" "$AFTER"
done
require_sparse "$CHECK/$TARGET_ENTRY"
require_sparse "$CHECK/cache.img"
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

echo
echo "Hazir: ${BUNDLE#$HERE/}"
echo "  Odin -> AP: bu TEK dosya"
echo "  Re-Partition: KAPALI"
echo
echo "Arsiv yalnizca boot.img, $TARGET_ENTRY ve cache.img icerir."
if [ "$DESKTOP_MODE" = 1 ]; then
  echo "Windows Desktop ${TARGET^^} NTFS, CACHE Desktop-BCD ESP ve iki sparse imaj dogrulandi."
  if [ "$TARGET" = userdata ]; then
    echo "UYARI: Bu arsiv USERDATA bolumundeki mevcut veriyi siler."
  fi
elif [ "$DISK_MODE" = 1 ]; then
  echo "SYSTEM NTFS, CACHE disk-BCD ESP ve iki sparse imaj dogrulandi."
else
  echo "SYSTEM ve CACHE'nin Android sparse oldugu dogrulandi."
fi
