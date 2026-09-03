#!/usr/bin/env bash
# Build, test-sign and inject the ARM64 Exynos9820 KMDF static-host USB package.
# Run from WSL with Windows WDK, Visual Studio ARM64 tools and boot.wim.

set -euo pipefail

# WIM paths are Windows paths even when wimlib runs under Linux.  The ADK
# image preserves mixed/lower-case names for parts of the inbox input stack
# (for example hidclass.sys), so exact-case extraction would report a false
# missing-file error for the upper-case validation paths below.
export WIMLIB_IMAGEX_IGNORE_CASE=1

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/windows/b2lusb"
OUT="$HERE/out/b2lusb"
WIM="$HERE/esp/sources/boot.wim"
WINROOT="${B2L_WINROOT:-/mnt/c}"
KITS="${B2L_WDK_ROOT:-$WINROOT/Program Files (x86)/Windows Kits/10}"
AUTO_HOST="${B2L_USB_AUTO_HOST:-0}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for Tool in powershell.exe file osslsigncode openssl wimlib-imagex sha256sum wslpath; do
  command -v "$Tool" >/dev/null || die "$Tool yok"
done
[ -s "$WIM" ] || die "boot.wim yok; once tools_winpe_adk.sh calistir"
[ -d "$KITS/Include" ] || die "WDK Include yok: $KITS/Include"
case "$AUTO_HOST" in 0|1) ;; *) die "B2L_USB_AUTO_HOST yalniz 0 veya 1 olabilir" ;; esac

WDK_VERSION=""
while IFS= read -r Candidate; do
  if [ -f "$KITS/Include/$Candidate/km/ntddk.h" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/libcntpr.lib" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/ntoskrnl.lib" ]; then
    WDK_VERSION="$Candidate"
    break
  fi
done < <(find "$KITS/Include" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V -r)
[ -n "$WDK_VERSION" ] || {
  printf 'Aranan ARM64 WDK: ntddk.h, libcntpr.lib ve ntoskrnl.lib\n' >&2
  printf 'Taranan WDK kok dizini: %s\n' "$KITS" >&2
  while IFS= read -r Candidate; do
    printf '  Surum %s:\n' "$Candidate" >&2
    for Required in \
      "Include/$Candidate/km/ntddk.h" \
      "Lib/$Candidate/km/arm64/libcntpr.lib" \
      "Lib/$Candidate/km/arm64/ntoskrnl.lib"; do
      if [ -f "$KITS/$Required" ]; then
        printf '    VAR: %s\n' "$Required" >&2
      else
        printf '    YOK: %s\n' "$Required" >&2
      fi
    done
  done < <(find "$KITS/Include" -mindepth 1 -maxdepth 1 -type d \
    -name '[0-9]*' -printf '%f\n' 2>/dev/null | sort -V)
  die "ARM64 kernel kutuphanelerini iceren WDK bulunamadi"
}
[ -f "$KITS/Include/wdf/kmdf/1.15/wdf.h" ] || die "KMDF 1.15 basliklari yok"
[ -f "$KITS/Lib/wdf/kmdf/arm64/1.15/WdfDriverEntry.lib" ] || die "ARM64 KMDF 1.15 kutuphaneleri yok"

INF2CAT="${B2L_INF2CAT:-}"
SIGNTOOL="${B2L_SIGNTOOL:-}"
if [ -z "$INF2CAT" ]; then
  INF2CAT="$(find "$KITS/bin" -iname Inf2Cat.exe -type f -print 2>/dev/null | sort -V -r | head -n 1 || true)"
fi
if [ -z "$SIGNTOOL" ]; then
  SIGNTOOL="$(find "$KITS/bin" -iname signtool.exe -type f -print 2>/dev/null | sort -V -r | head -n 1 || true)"
fi
[ -f "$INF2CAT" ] || die "Inf2Cat.exe yok; WDK Tools bilesenini kur"
[ -f "$SIGNTOOL" ] || die "signtool.exe yok; Windows SDK Signing Tools kur"

mkdir -p "$OUT"
PS_BUILD="$SRC/build-b2lusb.ps1"
[ -s "$PS_BUILD" ] || die "PowerShell derleme betigi yok: $PS_BUILD"
if [ "${B2L_SKIP_MSVC_BUILD:-0}" = "1" ]; then
  [ -s "$OUT/b2lusb.sys" ] || die "B2L_SKIP_MSVC_BUILD=1 ama b2lusb.sys yok"
  [ -s "$OUT/b2lusbmon.sys" ] || die "B2L_SKIP_MSVC_BUILD=1 ama b2lusbmon.sys yok"
  [ -s "$OUT/b2lmax77705.sys" ] || die "B2L_SKIP_MSVC_BUILD=1 ama b2lmax77705.sys yok"
  [ -s "$OUT/b2lusbctl.exe" ] || die "B2L_SKIP_MSVC_BUILD=1 ama b2lusbctl.exe yok"
  NEWER_DRIVER="$(find "$SRC" -maxdepth 1 -newer "$OUT/b2lusb.sys" \
    \( -name 'b2lusb*.c' -o -name 'b2lusb*.h' -o -name 'build-b2lusb.ps1' \) \
    -printf '%f\n' 2>/dev/null || true)"
  NEWER_MONITOR="$(find "$SRC" -maxdepth 1 -newer "$OUT/b2lusbmon.sys" \
    \( -name 'b2lusbmon.c' -o -name 'build-b2lusb.ps1' \) \
    -printf '%f\n' 2>/dev/null || true)"
  NEWER_MAX77705="$(find "$SRC" -maxdepth 1 -newer "$OUT/b2lmax77705.sys" \
    \( -name 'b2lmax77705*.c' -o -name 'b2lmax77705*.h' -o -name 'build-b2lusb.ps1' \) \
    -printf '%f\n' 2>/dev/null || true)"
  NEWER_CONTROL="$(find "$SRC" -maxdepth 1 -newer "$OUT/b2lusbctl.exe" \
    \( -name 'b2lusbctl.c' -o -name 'b2lusb_public.h' -o -name 'build-b2lusb.ps1' \) \
    -printf '%f\n' 2>/dev/null || true)"
  if { [ -n "$NEWER_DRIVER" ] || [ -n "$NEWER_MONITOR" ] || \
       [ -n "$NEWER_MAX77705" ] || \
       [ -n "$NEWER_CONTROL" ]; } && \
     [ "${B2L_ALLOW_STALE_SYS:-0}" != "1" ]; then
    printf 'HATA: B2LUSB ARM64 ciktilari kaynaklardan eski:\n%s\n%s\n%s\n%s\n' \
      "$NEWER_DRIVER" "$NEWER_MONITOR" "$NEWER_MAX77705" "$NEWER_CONTROL" >&2
    exit 1
  fi
  say "Var olan B2LUSB ARM64 ciktilari kullaniliyor"
else
  say "B2LUSB derleniyor (Windows MSVC ARM64 + KMDF static HOST)"
  powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$PS_BUILD")" \
    -ProjectRoot "$(wslpath -w "$HERE")" \
    -OutDir "$(wslpath -w "$OUT")" \
    -KitsRoot "$(wslpath -w "$KITS")" \
    -WdkVersion "$WDK_VERSION"
fi

for Item in b2lusb.sys b2lusbmon.sys b2lmax77705.sys b2lusbctl.exe; do
  [ -s "$OUT/$Item" ] || die "$Item uretilmedi"
  file "$OUT/$Item" | grep -qi 'aarch64\|arm64' || die "$Item ARM64 degil"
done

CERT="$HERE/windows/b2lfb/b2l-test.pem"
KEY="$HERE/windows/b2lfb/b2l-test.key"
if [ ! -s "$CERT" ] || [ ! -s "$KEY" ]; then
  say "Ortak beyond2lte test sertifikasi uretiliyor"
  openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$KEY" -out "$CERT" -subj "/CN=beyond2lte test signing" 2>/dev/null
fi

say "B2LUSB SYS/EXE ve ortak PnP katalogu test imzasiyla hazirlaniyor"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte Exynos USB host platform" -h sha256 \
  -in "$OUT/b2lusb.sys" -out "$OUT/b2lusb-signed.sys" >/dev/null
mv -f "$OUT/b2lusb-signed.sys" "$OUT/b2lusb.sys"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte USBXHCI resource monitor" -h sha256 \
  -in "$OUT/b2lusbmon.sys" -out "$OUT/b2lusbmon-signed.sys" >/dev/null
mv -f "$OUT/b2lusbmon-signed.sys" "$OUT/b2lusbmon.sys"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte MAX77705 Type-C OTG" -h sha256 \
  -in "$OUT/b2lmax77705.sys" -out "$OUT/b2lmax77705-signed.sys" >/dev/null
mv -f "$OUT/b2lmax77705-signed.sys" "$OUT/b2lmax77705.sys"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte USB host control" -h sha256 \
  -in "$OUT/b2lusbctl.exe" -out "$OUT/b2lusbctl-signed.exe" >/dev/null
mv -f "$OUT/b2lusbctl-signed.exe" "$OUT/b2lusbctl.exe"
cp "$SRC/b2lusb.inf.in" "$OUT/b2lusb.inf"
cp "$SRC/b2lusb-host.inf.in" "$OUT/b2lusb-host.inf"
cp "$SRC/b2lmax77705.inf.in" "$OUT/b2lmax77705.inf"
rm -f "$OUT/b2lusb.cat"
"$INF2CAT" /driver:"$(wslpath -w "$OUT")" /os:10_GE_ARM64
[ -s "$OUT/b2lusb.cat" ] || die "Inf2Cat b2lusb.cat uretmedi"

PFX="$OUT/b2l-test.pfx"
CHECK=""
cleanup() {
  rm -f "$PFX"
  [ -z "$CHECK" ] || rm -rf "$CHECK"
}
trap cleanup EXIT
PFX_PASS="b2lusb-host"
openssl pkcs12 -export -out "$PFX" -inkey "$KEY" -in "$CERT" \
  -passout "pass:$PFX_PASS" >/dev/null 2>&1
"$SIGNTOOL" sign /v /fd sha256 /f "$(wslpath -w "$PFX")" \
  /p "$PFX_PASS" "$(wslpath -w "$OUT/b2lusb.cat")"

verify_test_signature() {
  local Label="$1"
  shift
  local Log
  Log="$(mktemp "${TMPDIR:-/tmp}/b2lusb-signtool.XXXXXX")" || \
    die "SignTool dogrulama gunlugu olusturulamadi"
  if "$SIGNTOOL" verify "$@" >"$Log" 2>&1; then
    printf '  dogrulandi: %s\n' "$Label"
  elif grep -Fqi 'certificate chain processed, but terminated in a root' "$Log" && \
       grep -Fqi 'certificate which is not trusted by the trust provider' "$Log"; then
    printf '  test imza dogrulandi; build bilgisayari test kokune guvenmiyor: %s\n' "$Label"
  else
    sed -n '1,160p' "$Log" >&2
    rm -f "$Log"
    die "SignTool dogrulamasi basarisiz: $Label"
  fi
  rm -f "$Log"
}

verify_test_signature "b2lusb.cat" /v /pa "$(wslpath -w "$OUT/b2lusb.cat")"
verify_test_signature "b2lusb.sys katalog eslesmesi" /v /pa /c \
  "$(wslpath -w "$OUT/b2lusb.cat")" "$(wslpath -w "$OUT/b2lusb.sys")"
verify_test_signature "b2lusbmon.sys katalog eslesmesi" /v /pa /c \
  "$(wslpath -w "$OUT/b2lusb.cat")" "$(wslpath -w "$OUT/b2lusbmon.sys")"
verify_test_signature "b2lmax77705.sys katalog eslesmesi" /v /pa /c \
  "$(wslpath -w "$OUT/b2lusb.cat")" "$(wslpath -w "$OUT/b2lmax77705.sys")"
verify_test_signature "b2lusbctl.exe" /v /pa "$(wslpath -w "$OUT/b2lusbctl.exe")"
rm -f "$PFX"

AUTO_HOST_MARKER="$OUT/auto-host.test"
if [ "$AUTO_HOST" = "1" ]; then
  printf 'B2LUSB_AUTO_HOST=ENABLED\n' > "$AUTO_HOST_MARKER"
else
  rm -f "$AUTO_HOST_MARKER"
fi

MANIFEST="$OUT/b2lusb-manifest.txt"
{
  echo "B2LUSB_PACKAGE=exynos9820-kmdf-static-usb2-host-bringup"
  echo "B2LUSB_DRIVER_VERSION=0.1.0.15"
  echo "B2LUSB_ACPI_ID=ACPI\\BYN0002"
  echo "B2LUSB_HOST_ID=ACPI\\BYN0003"
  echo "B2LUSB_MAX77705_ID=ACPI\\BYN0004"
  echo "B2LUSB_SUPERSPEED=DISABLED_UNTIL_COMBO_PMA_SAFE"
  echo "B2LUSB_AUTO_HOST=$AUTO_HOST"
  echo "B2LUSB_OFFLINE_DRIVERSTORE=1"
  echo "B2LUSB_INF_SHA256=$(sha256sum "$OUT/b2lusb.inf" | awk '{print $1}')"
  echo "B2LUSB_HOST_INF_SHA256=$(sha256sum "$OUT/b2lusb-host.inf" | awk '{print $1}')"
  echo "B2LUSB_MAX77705_INF_SHA256=$(sha256sum "$OUT/b2lmax77705.inf" | awk '{print $1}')"
  echo "B2LUSB_SYS_SHA256=$(sha256sum "$OUT/b2lusb.sys" | awk '{print $1}')"
  echo "B2LUSB_MON_SHA256=$(sha256sum "$OUT/b2lusbmon.sys" | awk '{print $1}')"
  echo "B2LUSB_MAX77705_SYS_SHA256=$(sha256sum "$OUT/b2lmax77705.sys" | awk '{print $1}')"
  echo "B2LUSB_CAT_SHA256=$(sha256sum "$OUT/b2lusb.cat" | awk '{print $1}')"
  echo "B2LUSB_CTL_SHA256=$(sha256sum "$OUT/b2lusbctl.exe" | awk '{print $1}')"
} > "$MANIFEST"

say "WinPE inbox USB host bilesenleri denetleniyor"
bash "$HERE/tools_winpe_ui_scale.sh"
CHECK="$(mktemp -d)"
for InboxPath in \
  /Windows/System32/drivers/Ucx01000.sys \
  /Windows/System32/drivers/USBXHCI.SYS \
  /Windows/System32/drivers/USBHUB3.SYS \
  /Windows/System32/drivers/HIDCLASS.SYS \
  /Windows/System32/drivers/HIDPARSE.SYS \
  /Windows/System32/drivers/HIDUSB.SYS \
  /Windows/System32/drivers/MOUHID.SYS \
  /Windows/System32/drivers/MOUCLASS.SYS; do
  rm -rf "$CHECK"/*
  wimlib-imagex extract "$WIM" 1 "$InboxPath" \
    --dest-dir="$CHECK" --no-acls >/dev/null || \
    die "WinPE inbox bileseni yok: $InboxPath"
  [ -n "$(find "$CHECK" -type f -print -quit)" ] || die "WIM cikarimi bos: $InboxPath"
  printf '  var: %s\n' "$InboxPath"
done

PS_STAGE="$HERE/windows/stage-winpe-driverstore.ps1"
[ -s "$PS_STAGE" ] || die "Offline DriverStore PowerShell betigi yok: $PS_STAGE"
case "${B2L_STAGE_TOUCH_WITH_USB:-0}" in
  0) OFFLINE_PACKAGE="Usb" ;;
  1) OFFLINE_PACKAGE="UsbTouch" ;;
  *) die "B2L_STAGE_TOUCH_WITH_USB yalniz 0 veya 1 olabilir" ;;
esac
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/B2LUSB/offline-driverstore.txt" \
  >/dev/null 2>&1 || true
if [ "$OFFLINE_PACKAGE" = "UsbTouch" ]; then
  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/B2LTOUCH/offline-driverstore.txt" \
    >/dev/null 2>&1 || true
fi
say "B2LUSB DriverStore paketi boot.wim'e offline ekleniyor: $OFFLINE_PACKAGE"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$(wslpath -w "$PS_STAGE")" \
  -ProjectRoot "$(wslpath -w "$HERE")" \
  -WimPath "$(wslpath -w "$WIM")" \
  -Package "$OFFLINE_PACKAGE" || die "B2LUSB offline DriverStore servislemesi basarisiz"

say "B2LUSB paketi boot.wim'e ekleniyor"
for Item in b2lusb.inf b2lusb-host.inf b2lmax77705.inf b2lusb.sys b2lusbmon.sys b2lmax77705.sys b2lusb.cat b2lusbctl.exe; do
  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/B2LUSB/$Item" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$OUT/$Item' /Windows/B2LUSB/$Item" >/dev/null
done
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/B2LUSB/manifest.txt" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$MANIFEST' /Windows/B2LUSB/manifest.txt" >/dev/null
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/B2LUSB/auto-host.test" >/dev/null 2>&1 || true
if [ "$AUTO_HOST" = "1" ]; then
  wimlib-imagex update "$WIM" 1 \
    --command="add '$AUTO_HOST_MARKER' /Windows/B2LUSB/auto-host.test" >/dev/null
fi

SNTMP="$OUT/startnet.cmd"
sed 's/\r$//; s/$/\r/' "$HERE/windows/startnet.cmd" > "$SNTMP"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/startnet.cmd" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$SNTMP' /Windows/System32/startnet.cmd" >/dev/null

rm -rf "$CHECK"
CHECK="$(mktemp -d)"
for Item in b2lusb.inf b2lusb-host.inf b2lmax77705.inf b2lusb.sys b2lusbmon.sys b2lmax77705.sys b2lusb.cat b2lusbctl.exe; do
  wimlib-imagex extract "$WIM" 1 "/Windows/B2LUSB/$Item" \
    --dest-dir="$CHECK" --no-acls >/dev/null
  Want="$(sha256sum "$OUT/$Item" | awk '{print $1}')"
  Got="$(sha256sum "$CHECK/$Item" | awk '{print $1}')"
  [ "$Want" = "$Got" ] || die "$Item WIM geri okuma hash'i farkli"
  printf '  dogrulandi: %s SHA256=%s\n' "$Item" "${Want:0:16}"
done
wimlib-imagex extract "$WIM" 1 /Windows/B2LUSB/manifest.txt \
  --dest-dir="$CHECK" --no-acls >/dev/null
[ "$(sha256sum "$MANIFEST" | awk '{print $1}')" = \
  "$(sha256sum "$CHECK/manifest.txt" | awk '{print $1}')" ] || die "manifest WIM'de farkli"
wimlib-imagex extract "$WIM" 1 /Windows/B2LUSB/offline-driverstore.txt \
  --dest-dir="$CHECK" --no-acls >/dev/null
tr -d '\r' < "$CHECK/offline-driverstore.txt" | \
  grep -qx 'B2L_OFFLINE_DRIVERSTORE=1' || \
  die "B2LUSB offline DriverStore isareti WIM'de gecersiz"
if [ "$AUTO_HOST" = "1" ]; then
  wimlib-imagex extract "$WIM" 1 /Windows/B2LUSB/auto-host.test \
    --dest-dir="$CHECK" --no-acls >/dev/null
  [ "$(sha256sum "$AUTO_HOST_MARKER" | awk '{print $1}')" = \
    "$(sha256sum "$CHECK/auto-host.test" | awk '{print $1}')" ] || \
    die "auto-host.test WIM'de farkli"
fi
wimlib-imagex extract "$WIM" 1 /Windows/System32/startnet.cmd \
  --dest-dir="$CHECK" --no-acls >/dev/null
[ "$(sha256sum "$SNTMP" | awk '{print $1}')" = \
  "$(sha256sum "$CHECK/startnet.cmd" | awk '{print $1}')" ] || die "startnet.cmd WIM'de farkli"

say "B2LUSB hazir"
echo "Sonraki adim: bash cache_esp.sh --win --system --baseline"
