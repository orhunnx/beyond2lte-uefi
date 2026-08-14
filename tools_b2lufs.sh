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
# Gerekenler: osslsigncode, openssl, wimlib-imagex;
# Windows tarafinda WDK (Inf2Cat dahil) ve MSVC C++ Build Tools (ARM64 hedefi).
# WDK ile uyumlu derleme, WSL clang'i degil MSVC'nin kendi cl.exe/link.exe
# araclariyla cmd.exe uzerinden yapilir.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/windows/b2lufs"
OUT="$HERE/out/b2lufs"
WIM="$HERE/esp/sources/boot.wim"
WINROOT="${B2L_WINROOT:-/mnt/c}"
KITS="${B2L_WDK_ROOT:-$WINROOT/Program Files (x86)/Windows Kits/10}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for tool in cmd.exe osslsigncode openssl wimlib-imagex sha256sum wslpath; do
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

# WSL clang ile MSVC 14.51 intrin.h'nin __prefetch bildirimleri farkli
# builtin imzasina dusebilir. Bu Faz 0 surucusunu dogrudan WDK'nin hedef
# aldigi MSVC cl.exe/link.exe ile derliyoruz. Ortami her cmd cagrisinda
# amd64-host/ARM64-target vcvars dosyasiyla kurmak, kullanicinin acik olan
# terminal ortamindan bagimsiz ve tekrar edilebilir bir derleme saglar.
VCVARS="${B2L_VCVARS_ARM64:-}"
if [ -z "$VCVARS" ]; then
  for Root in "$WINROOT/Program Files" "$WINROOT/Program Files (x86)"; do
    [ -d "$Root/Microsoft Visual Studio" ] || continue
    while IFS= read -r Batch; do
      VCVARS="$Batch"
      break 2
    done < <(find "$Root/Microsoft Visual Studio" \
      -path '*/VC/Auxiliary/Build/vcvarsamd64_arm64.bat' -type f -print 2>/dev/null | sort -V -r)
  done
fi
[ -f "$VCVARS" ] || die "vcvarsamd64_arm64.bat yok; Visual Studio C++ ARM64 build tools kur veya B2L_VCVARS_ARM64 ver"

# MSVC_INC bazen WDK'nin ucrt/ dizini olabilir; bu nedenle cl.exe yolunu
# include yolundan turetmiyoruz. Secilen vcvars dosyasinin ait oldugu VS
# kurulumunun ARM64 hedef aracini ariyoruz.
VS_ROOT="$(cd "$(dirname "$VCVARS")/../../.." && pwd)"
MSVC_ARM64_BIN="${B2L_MSVC_ARM64_BIN:-}"
if [ -z "$MSVC_ARM64_BIN" ]; then
  while IFS= read -r Compiler; do
    CandidateBin="$(dirname "$Compiler")"
    if [ -f "$CandidateBin/link.exe" ]; then
      MSVC_ARM64_BIN="$CandidateBin"
      break
    fi
  done < <(find "$VS_ROOT/VC/Tools/MSVC" \
    -path '*/bin/Hostx64/arm64/cl.exe' -type f -print 2>/dev/null | sort -V -r)
fi
[ -f "$MSVC_ARM64_BIN/cl.exe" ] || die "MSVC ARM64 cl.exe yok: $MSVC_ARM64_BIN/cl.exe; Visual Studio Installer'da 'MSVC v143/v14x C++ ARM64/ARM64EC build tools' bilesenini ekle"
[ -f "$MSVC_ARM64_BIN/link.exe" ] || die "MSVC ARM64 link.exe yok: $MSVC_ARM64_BIN/link.exe; Visual Studio C++ ARM64 build tools bilesenini ekle"

# WDK 28000 ile birlikte ntdef.h her kurulumda km/ altinda olmuyor. Ornegin
# bazi SDK yerlesimlerinde km/crt/ altina tasiniyor. wdm.h bunu acik bir
# acili parantez include'u ile istedigi icin clang, km/ dizini verilse bile
# onu kendiliginden bulamaz. WDK kokunun icinde yeri kesin olarak bulup
# yalniz o dizini ekliyoruz; um/km basliklarini topluca arama yoluna eklemek
# yanlis platform basliklarinin secilmesine yol acabilir.
WDK_NTDEF_INC=""
while IFS= read -r Header; do
  WDK_NTDEF_INC="$(dirname "$Header")"
  break
done < <(find "$WDK_INC" -type f -iname ntdef.h -print 2>/dev/null | sort | head -n 1)

[ -n "$WDK_NTDEF_INC" ] || die "ntdef.h yok: eslesen Windows SDK Headers kurulmamış. WDK $WDK_VERSION icin ayni build numarali Windows SDK'yi kur; ADK tek basina yeterli degil. Beklenen yol: $WDK_INC/shared/ntdef.h"

say "WDK ve MSVC bulundu"
echo "  WDK : $WDK_VERSION"
echo "  MSVC: $MSVC_INC"
echo "  vcvars: $VCVARS"
echo "  ARM64: $MSVC_ARM64_BIN"
echo "  ntdef: $WDK_NTDEF_INC/ntdef.h"

mkdir -p "$OUT"

# cl.exe Windows ARM64 ABI'sini ve x18 platform yazmacini kendisi korur.
# /Zl, /GS- ve /NODEFAULTLIB freestanding Faz 0 koduna CRT veya
# stack-cookie kutuphanesi bagimliligi eklenmesini engeller. _ARM64_ WDK
# basliklarinin mimari dalini acikca secmek icin korunur.
WIN_VCVARS="$(wslpath -w "$VCVARS")"
WIN_SRC="$(wslpath -w "$SRC")"
WIN_OUT="$(wslpath -w "$OUT")"
WIN_WDK_INC="$(wslpath -w "$WDK_INC")"
WIN_WDK_LIB="$(wslpath -w "$WDK_LIB")"
WIN_MSVC_ARM64_BIN="$(wslpath -w "$MSVC_ARM64_BIN")"
MSVC_FLAGS="/nologo /c /TC /kernel /Zl /GS- /W4 /WX /O1 /D_KERNEL_MODE /D_WIN32_WINNT=0x0A00 /D_ARM64_=1 /I\"$WIN_WDK_INC\\km\" /I\"$WIN_WDK_INC\\shared\" /I\"$WIN_WDK_INC\\ucrt\" /I\"$WIN_WDK_INC\\um\""
BUILD_CMD="$OUT/build-b2lufs-arm64.cmd"
WIN_BUILD_CMD="$(wslpath -w "$BUILD_CMD")"

# cmd.exe'ye uzun, ic ice tirnakli komut dizeleri vermek yerine ayni dosyayi
# VS'nin kurdugu ARM64 ortaminda calistiriyoruz. Bu, hem bosluk iceren
# Program Files yollarini hem de cl/link'in hata kodunu kayipsiz tasir.
cat > "$BUILD_CMD" <<EOF
@echo off
call "$WIN_VCVARS" >nul
if errorlevel 1 exit /b 1
"$WIN_MSVC_ARM64_BIN\\cl.exe" $MSVC_FLAGS /Fo"$WIN_OUT\\b2lufs_resources.obj" "$WIN_SRC\\b2lufs_resources.c"
if errorlevel 1 exit /b 1
"$WIN_MSVC_ARM64_BIN\\cl.exe" $MSVC_FLAGS /Fo"$WIN_OUT\\b2lufs_storport.obj" "$WIN_SRC\\b2lufs_storport.c"
if errorlevel 1 exit /b 1
"$WIN_MSVC_ARM64_BIN\\link.exe" /nologo /kernel /driver /subsystem:native /entry:DriverEntry /machine:arm64 /nodefaultlib /libpath:"$WIN_WDK_LIB" /out:"$WIN_OUT\\b2lufs.sys" "$WIN_OUT\\b2lufs_resources.obj" "$WIN_OUT\\b2lufs_storport.obj" storport.lib ntoskrnl.lib
exit /b %errorlevel%
EOF
sed -i 's/$/\r/' "$BUILD_CMD"

say "B2LUFS Faz 0 derleniyor (MSVC ARM64 + WDK)"
echo "  calistiriliyor: $WIN_BUILD_CMD"
cmd.exe /d /s /c "call \"$WIN_BUILD_CMD\""
say "Baglandi (MSVC link.exe; StorPort + ntoskrnl, ARM64)"
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
