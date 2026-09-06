#!/usr/bin/env bash
#
# Daha once uretilmis tam Windows NTFS imajinda ilk-acilis Setup döngüsünü
# düzeltir. Büyük install.wim'i yeniden uygulamaz; yalnız offline SYSTEM hive
# içindeki Setup/Status/ChildCompletion/setup.exe DWORD değerini 3 yapar.
#
# Kullanim:
#   bash tools_windesktop_oobe_repair.sh
#   bash tools_windesktop_oobe_repair.sh out/userdata.img

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
IMAGE="${1:-$OUT/userdata.img}"
MARKER="$OUT/windows-desktop-target.txt"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { echo "HATA: $*" >&2; exit 1; }

[ -s "$IMAGE" ] || fail "Desktop NTFS imaji yok ya da bos: $IMAGE"
[ -s "$MARKER" ] || fail "Desktop hedef isareti yok: $MARKER"
grep -Fqx 'B2L_WINDOWS_DESKTOP=1' "$MARKER" || fail "Desktop hedef isareti gecersiz"
grep -Fqx "B2L_IMAGE=$(basename "$IMAGE")" "$MARKER" || \
  fail "hedef isareti bu imaja ait degil: $(basename "$IMAGE")"

for tool in ntfscat ntfscp hivexget hivexregedit; do
  command -v "$tool" >/dev/null || \
    fail "$tool yok -> sudo apt install -y ntfs-3g libhivex-bin"
done

NTFS_MAGIC="$(dd if="$IMAGE" bs=1 skip=3 count=8 status=none)"
[ "$NTFS_MAGIC" = "NTFS    " ] || fail "Desktop imaji NTFS degil: $IMAGE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HIVE="$WORK/SYSTEM"
VERIFY_HIVE="$WORK/SYSTEM.verify"
REG="$WORK/oobe.reg"
KEY='Setup\Status\ChildCompletion'
PREFIX='HKEY_LOCAL_MACHINE\SYSTEM'

say "Offline SYSTEM hive cikariliyor"
ntfscat "$IMAGE" /Windows/System32/config/SYSTEM > "$HIVE" || \
  fail "SYSTEM hive NTFS imajindan okunamadi"
[ -s "$HIVE" ] || fail "cikarilan SYSTEM hive bos"

BACKUP="$OUT/$(basename "$IMAGE").SYSTEM-before-oobe"
if [ ! -e "$BACKUP" ]; then
  cp --reflink=auto "$HIVE" "$BACKUP"
  echo "  geri donus hive'i: ${BACKUP#$HERE/}"
fi

BEFORE="$(hivexget "$HIVE" "$KEY" setup.exe 2>/dev/null | tr -d '[:space:]' || true)"
printf '%s\n\n[%s\\Setup\\Status\\ChildCompletion]\n"setup.exe"=dword:00000003\n' \
  'Windows Registry Editor Version 5.00' "$PREFIX" > "$REG"

say "ChildCompletion setup.exe=3 yaziliyor"
hivexregedit --merge --prefix "$PREFIX" "$HIVE" "$REG"
AFTER="$(hivexget "$HIVE" "$KEY" setup.exe | tr -d '[:space:]')"
case "$AFTER" in 3|0x3|0X3|00000003) ;; *) fail "gecici hive geri okuma basarisiz: ${AFTER:-<bos>}" ;; esac
echo "  once: ${BEFORE:-<yok>}"
echo "  sonra: $AFTER"

say "Onarilan hive NTFS imajina yaziliyor"
ntfscp -f "$IMAGE" "$HIVE" /Windows/System32/config/SYSTEM || \
  fail "onarilan SYSTEM hive NTFS imajina yazilamadi"
ntfscat "$IMAGE" /Windows/System32/config/SYSTEM > "$VERIFY_HIVE" || \
  fail "yazilan SYSTEM hive yeniden okunamadi"
ON_IMAGE="$(hivexget "$VERIFY_HIVE" "$KEY" setup.exe | tr -d '[:space:]')"
case "$ON_IMAGE" in 3|0x3|0X3|00000003) ;; *) fail "imaj ici ChildCompletion dogrulanamadi: ${ON_IMAGE:-<bos>}" ;; esac

MARKER_TMP="$WORK/windows-desktop-target.txt"
grep -Fvx 'B2L_OOBE_CHILD_COMPLETION=3' "$MARKER" > "$MARKER_TMP" || true
printf '%s\n' 'B2L_OOBE_CHILD_COMPLETION=3' >> "$MARKER_TMP"
mv "$MARKER_TMP" "$MARKER"

say "Desktop ilk-acilis onarimi hazir"
echo "  imaj: $IMAGE"
echo "  dogrulanan: HKLM\\SYSTEM\\$KEY\\setup.exe=3"
echo
echo "Simdi yeniden paketle ve Odin AP ile flashla:"
echo "  bash tools_odin_winpe_bundle.sh --desktop --target $(sed -n 's/^B2L_TARGET=//p' "$MARKER")"
