#!/usr/bin/env bash
#
# B2LUFS Faz 0 — WDK basliklariyla ARM64 StorPort miniportunu derle,
# mevcut b2lfb test sertifikasi ile imzala ve yalniz WinPE WIM'ine koy.
#
# Bu tur bir disk surucusu DEGILDIR. Sadece ACPI'nin verdigi alti MMIO
# kaynagini StorPort uzerinden sahiplenir; UFS/PHY/UniPro register'ina
# dokunmaz, DMA kurmaz ve her SCSI istegini INVALID_REQUEST ile bitirir.
#
# WSL'de, Windows'taki WDK + Visual Studio Build Tools gorunurken calistir:
#   bash tools_b2lufs.sh
#
# Gerekenler: clang, lld-link, osslsigncode, openssl, wimlib-imagex;
# Windows tarafinda WDK (Inf2Cat dahil) ve MSVC C++ Build Tools (ARM64 hedefi).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/windows/b2lufs"
OUT="$HERE/out/b2lufs"
WIM="$HERE/esp/sources/boot.wim"
WINROOT="${B2L_WINROOT:-/mnt/c}"
KITS="${B2L_WDK_ROOT:-$WINROOT/Program Files (x86)/Windows Kits/10}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for tool in clang lld-link osslsigncode openssl wimlib-imagex sha256sum wslpath; do
  command -v "$tool" >/dev/null || die "$tool yok"
done

[ -s "$WIM" ] || die "boot.wim yok; once tools_winpe_adk.sh ve tools_b2lfb.sh calistir"
[ -d "$KITS/Include" ] || die "WDK Include yok: $KITS/Include"

# En yeni, hem WDK basliklarini hem ARM64 km kutuphanelerini iceren surum.
WDK_VERSION=""
while IFS= read -r Candidate; do
  if [ -f "$KITS/Include/$Candidate/km/ntddk.h" ] && \
     [ -f "$KITS/Include/$Candidate/km/storport.h" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/storport.lib" ] && \
     [ -f "$KITS/Lib/$Candidate/km/arm64/ntoskrnl.lib" ]; then
    WDK_VERSION="$Candidate"
    break
  fi
done < <(find "$KITS/Include" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V -r)

[ -n "$WDK_VERSION" ] || die "ARM64 WDK km/storport.lib bulunamadi; Windows WDK'yi kur"
WDK_INC="$KITS/Include/$WDK_VERSION"
WDK_LIB="$KITS/Lib/$WDK_VERSION/km/arm64"

# PnP paketi SYS'ten ibaret degildir: INF ve SYS hash'lerini tasiyan imzali
# .cat gerekir. Inf2Cat / signtool WDK SDK bin agacinda bulunur; WSL Windows
# .exe'sini dogrudan calistirabilir. Araçlardan biri yoksa derlenmis ama
# yuklenemeyecek bir paket birakmak yerine burada dururuz.
INF2CAT="${B2L_INF2CAT:-}"
SIGNTOOL="${B2L_SIGNTOOL:-}"
if [ -z "$INF2CAT" ] || [ -z "$SIGNTOOL" ]; then
  if [ -z "$INF2CAT" ]; then
    INF2CAT="$(find "$KITS/bin" -iname Inf2Cat.exe -type f -print 2>/dev/null | sort -V -r | head -n 1 || true)"
  fi
  if [ -z "$SIGNTOOL" ]; then
    SIGNTOOL="$(find "$KITS/bin" -iname signtool.exe -type f -print 2>/dev/null | sort -V -r | head -n 1 || true)"
  fi
fi
[ -f "$INF2CAT" ] || die "Inf2Cat.exe yok; Windows WDK 'Tools' bilesenini kur veya B2L_INF2CAT ver"
[ -f "$SIGNTOOL" ] || die "signtool.exe yok; Windows SDK Signing Tools kur veya B2L_SIGNTOOL ver"

# ntddk.h vadefs.h ister. Yeni SDK'larda bu UCRT'dedir; eski kurulumlarda
# Visual Studio'nun include dizininde olur. Kullanici isterse B2L_MSVC_INCLUDE
# ile dogrudan yol verir.
MSVC_INC="${B2L_MSVC_INCLUDE:-}"
if [ -z "$MSVC_INC" ] && [ -f "$WDK_INC/ucrt/vadefs.h" ]; then
  MSVC_INC="$WDK_INC/ucrt"
fi
if [ -z "$MSVC_INC" ]; then
  for Root in "$WINROOT/Program Files" "$WINROOT/Program Files (x86)"; do
    [ -d "$Root" ] || continue
    while IFS= read -r Header; do
      MSVC_INC="$(dirname "$Header")"
      break 2
    done < <(find "$Root/Microsoft Visual Studio" -path '*/VC/Tools/MSVC/*/include/vadefs.h' -type f -print 2>/dev/null | sort -V -r)
  done
fi

[ -f "$MSVC_INC/vadefs.h" ] || die "vadefs.h yok; Visual Studio Build Tools C++ bilesenini kur veya B2L_MSVC_INCLUDE ver"

say "WDK ve MSVC bulundu"
echo "  WDK : $WDK_VERSION"
echo "  MSVC: $MSVC_INC"

mkdir -p "$OUT"

# -ffixed-x18 Windows ARM64 cekirdegindeki platform yazmacini korur.
# -ffreestanding ve iki kapatma, CRT/stack-probe bagimliligi uretmez.
CFLAGS=(
  -target aarch64-windows-msvc
  -ffreestanding -fno-stack-protector -mno-stack-arg-probe -ffixed-x18
  -fms-extensions -D_KERNEL_MODE -D_WIN32_WINNT=0x0A00
  -isystem "$WDK_INC/km" -isystem "$WDK_INC/shared"
  -isystem "$WDK_INC/ucrt" -isystem "$WDK_INC/um"
  -isystem "$MSVC_INC"
  -O1 -Wall -Wextra -Werror
)

say "B2LUFS Faz 0 derleniyor (aarch64-windows-msvc)"
clang "${CFLAGS[@]}" -c "$SRC/b2lufs_resources.c" -o "$OUT/b2lufs_resources.obj"
clang "${CFLAGS[@]}" -c "$SRC/b2lufs_storport.c" -o "$OUT/b2lufs_storport.obj"

say "Baglaniyor (StorPort + ntoskrnl, ARM64)"
lld-link /driver /subsystem:native /entry:DriverEntry /machine:arm64 \
  /nodefaultlib /libpath:"$WDK_LIB" \
  /out:"$OUT/b2lufs.sys" \
  "$OUT/b2lufs_resources.obj" "$OUT/b2lufs_storport.obj" \
  storport.lib ntoskrnl.lib
file "$OUT/b2lufs.sys" | sed 's/^/  /'

# b2lfb ile AYNI sertifika: cihazda zaten guvenilen zinciri degistirmeyiz.
CERT="$HERE/windows/b2lfb/b2l-test.pem"
KEY="$HERE/windows/b2lfb/b2l-test.key"
if [ ! -s "$CERT" ] || [ ! -s "$KEY" ]; then
  say "Mevcut b2lfb test sertifikasi uretiliyor (bir kereye mahsus)"
  openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$KEY" -out "$CERT" \
    -subj "/CN=beyond2lte test signing" 2>/dev/null
fi

say "b2lufs.sys imzalaniyor"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte Exynos UFS StorPort phase 0" -h sha256 \
  -in "$OUT/b2lufs.sys" -out "$OUT/b2lufs-signed.sys" >/dev/null
mv -f "$OUT/b2lufs-signed.sys" "$OUT/b2lufs.sys"

# INF ve SYS ayni klasorde olmali ki Inf2Cat tum hash'leri pakete katsin.
cp "$SRC/b2lufs.inf.in" "$OUT/b2lufs.inf"

say "PnP katalogu uretiliyor ve ayni sertifikayla imzalaniyor"
PFX="$OUT/b2l-test.pfx"
cleanup() {
  rm -f "$PFX"
  [ -z "${CHECK:-}" ] || rm -rf "$CHECK"
}
trap cleanup EXIT
rm -f "$OUT/b2lufs.cat" "$OUT/b2lufs-signed.cat" "$PFX"
"$INF2CAT" /driver:"$(wslpath -w "$OUT")" /os:10_GE_ARM64
[ -s "$OUT/b2lufs.cat" ] || die "Inf2Cat b2lufs.cat uretmedi"

# signtool Windows PFX bekler. PFX gecicidir; anahtar depoda tutulmaz ve
# b2lfb'nin PEM zincirinden bu tur icin uretilir.
PFX_PASS="b2lufs-phase0"
openssl pkcs12 -export -out "$PFX" \
  -inkey "$KEY" -in "$CERT" -passout "pass:$PFX_PASS" >/dev/null 2>&1
"$SIGNTOOL" sign /v /fd sha256 /f "$(wslpath -w "$PFX")" \
  /p "$PFX_PASS" "$(wslpath -w "$OUT/b2lufs.cat")"
"$SIGNTOOL" verify /v /pa "$(wslpath -w "$OUT/b2lufs.cat")"
"$SIGNTOOL" verify /v /pa /c "$(wslpath -w "$OUT/b2lufs.cat")" \
  "$(wslpath -w "$OUT/b2lufs.sys")"
rm -f "$PFX"

say "B2LUFS paketi boot.wim'e ekleniyor"
for Item in b2lufs.inf b2lufs.sys b2lufs.cat; do
  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/B2LUFS/$Item" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$OUT/$Item' /Windows/B2LUFS/$Item" >/dev/null
  echo "  /Windows/B2LUFS/$Item"
done

# tools_b2lfb.sh'nin sarmaladigi diagnostik komutu kaybolmasin: bu script
# startnet'i de kaynakla birebir yeniler. Bu sayede calisma sirasi hatasi
# nedeniyle eski startnet'in WIM'de kalmasi mumkun degil.
SNTMP="$OUT/startnet.cmd"
sed 's/\r$//; s/$/\r/' "$HERE/windows/startnet.cmd" > "$SNTMP"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/startnet.cmd" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$SNTMP' /Windows/System32/startnet.cmd" >/dev/null

CHECK="$(mktemp -d)"
for Item in b2lufs.inf b2lufs.sys b2lufs.cat; do
  wimlib-imagex extract "$WIM" 1 "/Windows/B2LUFS/$Item" \
    --dest-dir="$CHECK" --no-acls >/dev/null
  WANT="$(sha256sum "$OUT/$Item" | awk '{print $1}')"
  GOT="$(sha256sum "$CHECK/$Item" | awk '{print $1}')"
  [ "$WANT" = "$GOT" ] || die "$Item WIM geri okuma hash'i farkli; flaslama"
  echo "  dogrulandi: $Item SHA256=${WANT:0:16}"
done
wimlib-imagex extract "$WIM" 1 /Windows/System32/startnet.cmd \
  --dest-dir="$CHECK" --no-acls >/dev/null
[ "$(sha256sum "$SNTMP" | awk '{print $1}')" = "$(sha256sum "$CHECK/startnet.cmd" | awk '{print $1}')" ] || \
  die "startnet.cmd WIM geri okuma hash'i farkli; flaslama"
echo "  dogrulandi: startnet.cmd B2LUFS PnP kurulumunu iceriyor"

say "Faz 0 hazir"
echo "Bu imajda beklenen durum: ACPI\\SMSN9820\\0 -> b2lufs.inf, Started; DiskPart'ta dahili disk YOK."
echo "Sonraki paketleme: bash cache_esp.sh --win --system && bash cache_esp.sh --win --bos"
