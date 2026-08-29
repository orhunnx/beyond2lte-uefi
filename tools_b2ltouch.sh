#!/usr/bin/env bash
# Build, test-sign and inject the ARM64 sec_ts KMDF/VHF driver into WinPE.
# Run from WSL with the Windows WDK and Visual Studio ARM64 build tools.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/windows/b2ltouch"
OUT="$HERE/out/b2ltouch"
WIM="$HERE/esp/sources/boot.wim"
WINROOT="${B2L_WINROOT:-/mnt/c}"
KITS="${B2L_WDK_ROOT:-$WINROOT/Program Files (x86)/Windows Kits/10}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for tool in powershell.exe file osslsigncode openssl wimlib-imagex sha256sum wslpath; do
  command -v "$tool" >/dev/null || die "$tool yok"
done
[ -s "$WIM" ] || die "boot.wim yok; once tools_winpe_adk.sh calistir"
[ -d "$KITS/Include" ] || die "WDK Include yok: $KITS/Include"

WDK_VERSION=""
while IFS= read -r Candidate; do
  if [ -f "$KITS/Include/$Candidate/km/ntddk.h" ] && \
     [ -f "$KITS/Include/$Candidate/shared/vhf.h" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/VhfKm.lib" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/libcntpr.lib" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/ntoskrnl.lib" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/BufferOverflowFastFailK.lib" ]; then
    WDK_VERSION="$Candidate"
    break
  fi
done < <(find "$KITS/Include" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V -r)
[ -n "$WDK_VERSION" ] || die "ARM64 VHF kutuphanelerini iceren WDK bulunamadi"
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
PS_BUILD="$SRC/build-b2ltouch.ps1"
[ -s "$PS_BUILD" ] || die "PowerShell derleme betigi yok: $PS_BUILD"
if [ "${B2L_SKIP_MSVC_BUILD:-0}" = "1" ]; then
  [ -s "$OUT/b2ltouch.sys" ] || die "B2L_SKIP_MSVC_BUILD=1 ama b2ltouch.sys yok"
  NEWER="$(find "$SRC" -maxdepth 1 -newer "$OUT/b2ltouch.sys" \
    \( -name 'b2ltouch*.c' -o -name 'b2ltouch*.h' -o -name 'build-b2ltouch.ps1' \) \
    -printf '%f\n' 2>/dev/null || true)"
  if [ -n "$NEWER" ] && [ "${B2L_ALLOW_STALE_SYS:-0}" != "1" ]; then
    printf 'HATA: b2ltouch.sys kaynaklardan eski:\n%s\n' "$NEWER" >&2
    exit 1
  fi
  say "Var olan B2LTOUCH SYS kullaniliyor"
else
  say "B2LTOUCH derleniyor (Windows MSVC ARM64 + KMDF/VHF)"
  powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$PS_BUILD")" \
    -ProjectRoot "$(wslpath -w "$HERE")" \
    -OutDir "$(wslpath -w "$OUT")" \
    -KitsRoot "$(wslpath -w "$KITS")" \
    -WdkVersion "$WDK_VERSION"
fi
[ -s "$OUT/b2ltouch.sys" ] || die "b2ltouch.sys uretilmedi"
file "$OUT/b2ltouch.sys" | grep -qi 'aarch64\|arm64' || die "b2ltouch.sys ARM64 degil"

CERT="$HERE/windows/b2lfb/b2l-test.pem"
KEY="$HERE/windows/b2lfb/b2l-test.key"
if [ ! -s "$CERT" ] || [ ! -s "$KEY" ]; then
  say "Ortak beyond2lte test sertifikasi uretiliyor"
  openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$KEY" -out "$CERT" -subj "/CN=beyond2lte test signing" 2>/dev/null
fi

say "SYS ve PnP katalogu test imzasiyla hazirlaniyor"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte sec_ts HID source" -h sha256 \
  -in "$OUT/b2ltouch.sys" -out "$OUT/b2ltouch-signed.sys" >/dev/null
mv -f "$OUT/b2ltouch-signed.sys" "$OUT/b2ltouch.sys"
cp "$SRC/b2ltouch.inf.in" "$OUT/b2ltouch.inf"
rm -f "$OUT/b2ltouch.cat"
"$INF2CAT" /driver:"$(wslpath -w "$OUT")" /os:10_GE_ARM64
[ -s "$OUT/b2ltouch.cat" ] || die "Inf2Cat b2ltouch.cat uretmedi"

PFX="$OUT/b2l-test.pfx"
CHECK=""
cleanup() {
  rm -f "$PFX"
  [ -z "$CHECK" ] || rm -rf "$CHECK"
}
trap cleanup EXIT
PFX_PASS="b2ltouch-vhf"
openssl pkcs12 -export -out "$PFX" -inkey "$KEY" -in "$CERT" \
  -passout "pass:$PFX_PASS" >/dev/null 2>&1
"$SIGNTOOL" sign /v /fd sha256 /f "$(wslpath -w "$PFX")" \
  /p "$PFX_PASS" "$(wslpath -w "$OUT/b2ltouch.cat")"

verify_test_signature() {
  local Label="$1"
  shift
  local Log
  Log="$(mktemp "${TMPDIR:-/tmp}/b2ltouch-signtool.XXXXXX")" || \
    die "SignTool dogrulama gunlugu olusturulamadi"
  if "$SIGNTOOL" verify "$@" >"$Log" 2>&1; then
    printf '  dogrulandi: %s\n' "$Label"
  elif grep -Fqi 'certificate chain processed, but terminated in a root' "$Log" && \
       grep -Fqi 'certificate which is not trusted by the trust provider' "$Log"; then
    printf '  test imza dogrulandi; build bilgisayari test kokune guvenmiyor: %s\n' "$Label"
  else
    cat "$Log" >&2
    rm -f "$Log"
    die "SignTool dogrulamasi basarisiz: $Label"
  fi
  rm -f "$Log"
}

verify_test_signature "b2ltouch.cat" /v /pa "$(wslpath -w "$OUT/b2ltouch.cat")"
verify_test_signature "b2ltouch.sys katalog eslesmesi" /v /pa /c \
  "$(wslpath -w "$OUT/b2ltouch.cat")" "$(wslpath -w "$OUT/b2ltouch.sys")"
rm -f "$PFX"

MANIFEST="$OUT/b2ltouch-manifest.txt"
{
  echo "B2LTOUCH_PACKAGE=sec-ts-kmdf-vhf-single-touch"
  echo "B2LTOUCH_DRIVER_VERSION=0.1.4.0"
  echo "B2LTOUCH_ACPI_ID=ACPI\\BYN0001"
  echo "B2LTOUCH_INF_SHA256=$(sha256sum "$OUT/b2ltouch.inf" | awk '{print $1}')"
  echo "B2LTOUCH_SYS_SHA256=$(sha256sum "$OUT/b2ltouch.sys" | awk '{print $1}')"
  echo "B2LTOUCH_CAT_SHA256=$(sha256sum "$OUT/b2ltouch.cat" | awk '{print $1}')"
} > "$MANIFEST"

say "B2LTOUCH paketi boot.wim'e ekleniyor"
for Item in b2ltouch.inf b2ltouch.sys b2ltouch.cat; do
  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/B2LTOUCH/$Item" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$OUT/$Item' /Windows/B2LTOUCH/$Item" >/dev/null
done
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/B2LTOUCH/manifest.txt" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$MANIFEST' /Windows/B2LTOUCH/manifest.txt" >/dev/null

SNTMP="$OUT/startnet.cmd"
sed 's/\r$//; s/$/\r/' "$HERE/windows/startnet.cmd" > "$SNTMP"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/startnet.cmd" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$SNTMP' /Windows/System32/startnet.cmd" >/dev/null

CHECK="$(mktemp -d)"
for Item in b2ltouch.inf b2ltouch.sys b2ltouch.cat; do
  wimlib-imagex extract "$WIM" 1 "/Windows/B2LTOUCH/$Item" \
    --dest-dir="$CHECK" --no-acls >/dev/null
  WANT="$(sha256sum "$OUT/$Item" | awk '{print $1}')"
  GOT="$(sha256sum "$CHECK/$Item" | awk '{print $1}')"
  [ "$WANT" = "$GOT" ] || die "$Item WIM geri okuma hash'i farkli"
  printf '  dogrulandi: %s SHA256=%s\n' "$Item" "${WANT:0:16}"
done
wimlib-imagex extract "$WIM" 1 /Windows/B2LTOUCH/manifest.txt \
  --dest-dir="$CHECK" --no-acls >/dev/null
[ "$(sha256sum "$MANIFEST" | awk '{print $1}')" = \
  "$(sha256sum "$CHECK/manifest.txt" | awk '{print $1}')" ] || die "manifest WIM'de farkli"
wimlib-imagex extract "$WIM" 1 /Windows/System32/startnet.cmd \
  --dest-dir="$CHECK" --no-acls >/dev/null
[ "$(sha256sum "$SNTMP" | awk '{print $1}')" = \
  "$(sha256sum "$CHECK/startnet.cmd" | awk '{print $1}')" ] || die "startnet.cmd WIM'de farkli"

say "B2LTOUCH hazir"
echo "Sonraki adim: bash cache_esp.sh --win --system"
