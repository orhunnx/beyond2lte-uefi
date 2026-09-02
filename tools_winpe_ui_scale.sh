#!/usr/bin/env bash
# Set the bare WinPE user's system-wide display scale before the session starts.
# 96 DPI is 100%; 168 DPI is the requested 175% scale.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"
SCALE_PERCENT=175
LOG_PIXELS=168

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for Tool in wimlib-imagex hivexregedit hivexget sha256sum; do
  command -v "$Tool" >/dev/null || {
    case "$Tool" in
      hivexregedit) die "hivexregedit yok -> sudo apt install -y libwin-hivex-perl" ;;
      hivexget) die "hivexget yok -> sudo apt install -y libhivex-bin" ;;
      *) die "$Tool yok" ;;
    esac
  }
done
[ -s "$WIM" ] || die "boot.wim yok: $WIM"

export WIMLIB_IMAGEX_IGNORE_CASE=1
WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

mkdir -p "$WORK/hive" "$WORK/verify-hive" "$WORK/verify-manifest"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/DEFAULT \
  --dest-dir="$WORK/hive" --no-acls >/dev/null || \
  die "WinPE DEFAULT kullanici hive'i cikarilamadi"
DEFAULT_HIVE="$(find "$WORK/hive" -type f -iname DEFAULT -print -quit)"
[ -s "$DEFAULT_HIVE" ] || die "WinPE DEFAULT kullanici hive cikarimi bos"

REG_FILE="$WORK/scale.reg"
cat > "$REG_FILE" <<'REG'
[HKEY_USERS\.DEFAULT\Control Panel\Desktop]
"Win8DpiScaling"=dword:00000001
"LogPixels"=dword:000000a8
REG
hivexregedit --merge --prefix 'HKEY_USERS\.DEFAULT' \
  "$DEFAULT_HIVE" "$REG_FILE" || die "WinPE 175% DPI ayari DEFAULT hive'a yazilamadi"

SAVED_DPI="$(hivexget "$DEFAULT_HIVE" 'Control Panel\Desktop' LogPixels 2>/dev/null || true)"
SAVED_MODE="$(hivexget "$DEFAULT_HIVE" 'Control Panel\Desktop' Win8DpiScaling 2>/dev/null || true)"
[ "$SAVED_DPI" = "$LOG_PIXELS" ] || die "LogPixels geri okuma $SAVED_DPI; beklenen $LOG_PIXELS"
[ "$SAVED_MODE" = "1" ] || die "Win8DpiScaling geri okuma $SAVED_MODE; beklenen 1"

MANIFEST="$WORK/manifest.txt"
{
  echo "B2LUI_PACKAGE=winpe-display-scale-v1"
  echo "B2LUI_SCALE_PERCENT=$SCALE_PERCENT"
  echo "B2LUI_LOG_PIXELS=$LOG_PIXELS"
  echo "B2LUI_DEFAULT_HIVE_SHA256=$(sha256sum "$DEFAULT_HIVE" | awk '{print $1}')"
} > "$MANIFEST"

say "WinPE ekran olcegi %$SCALE_PERCENT yapiliyor"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/config/DEFAULT" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$DEFAULT_HIVE' /Windows/System32/config/DEFAULT" >/dev/null
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/B2LUI/manifest.txt" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$MANIFEST' /Windows/B2LUI/manifest.txt" >/dev/null

wimlib-imagex extract "$WIM" 1 /Windows/System32/config/DEFAULT \
  --dest-dir="$WORK/verify-hive" --no-acls >/dev/null
wimlib-imagex extract "$WIM" 1 /Windows/B2LUI/manifest.txt \
  --dest-dir="$WORK/verify-manifest" --no-acls >/dev/null
VERIFY_HIVE="$(find "$WORK/verify-hive" -type f -iname DEFAULT -print -quit)"
VERIFY_MANIFEST="$(find "$WORK/verify-manifest" -type f -iname manifest.txt -print -quit)"
[ -s "$VERIFY_HIVE" ] || die "DEFAULT hive WIM geri-okumasi bos"
[ -s "$VERIFY_MANIFEST" ] || die "B2LUI manifest WIM geri-okumasi bos"
[ "$(sha256sum "$DEFAULT_HIVE" | awk '{print $1}')" = \
  "$(sha256sum "$VERIFY_HIVE" | awk '{print $1}')" ] || \
  die "DEFAULT hive WIM geri-okuma hash'i farkli"
[ "$(sha256sum "$MANIFEST" | awk '{print $1}')" = \
  "$(sha256sum "$VERIFY_MANIFEST" | awk '{print $1}')" ] || \
  die "B2LUI manifest WIM geri-okuma hash'i farkli"
[ "$(hivexget "$VERIFY_HIVE" 'Control Panel\Desktop' LogPixels 2>/dev/null || true)" = "$LOG_PIXELS" ] || \
  die "WIM geri-okumada LogPixels $LOG_PIXELS degil"

echo "  LogPixels=$LOG_PIXELS (%$SCALE_PERCENT)"
echo "  manifest=/Windows/B2LUI/manifest.txt"
