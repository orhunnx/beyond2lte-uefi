#!/usr/bin/env bash
# Add Microsoft's ARM64 UrsCx class-extension runtime to a bare ADK WinPE.
#
# The WDK only supplies UrsCx.h and UrsCxStub.lib.  UrsCx01000.sys and its
# service registration are Windows runtime components, so both are copied
# from an ARM64 Windows install image.  The source service tree is exported
# from the source SYSTEM hive; no registry values are guessed here.  Exact
# build is preferred.  UrsCx 1.0 from an older build in the same Windows line
# may be used by a newer WinPE, but the unsafe opposite direction is denied.
#
# Usage:
#   B2L_WINDOWS_ARM64_SOURCE=/mnt/c/.../Windows11_ARM64.iso \
#     bash tools_winpe_urs_runtime.sh [source.iso|install.wim|install.esd] [index]

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_WIM="$HERE/esp/sources/boot.wim"
SOURCE="${1:-${B2L_WINDOWS_ARM64_SOURCE:-}}"
SOURCE_INDEX="${2:-${B2L_WINDOWS_ARM64_INDEX:-1}}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for Tool in wimlib-imagex sha256sum hivexget hivexregedit; do
  command -v "$Tool" >/dev/null || {
    case "$Tool" in
      hivexget) die "hivexget yok -> sudo apt install -y libhivex-bin" ;;
      hivexregedit) die "hivexregedit yok -> sudo apt install -y libwin-hivex-perl" ;;
      *) die "$Tool yok" ;;
    esac
  }
done
[ -s "$TARGET_WIM" ] || die "boot.wim yok: $TARGET_WIM"
case "$SOURCE_INDEX" in
  ''|*[!0-9]*) die "Windows kaynak indeksi sayi olmali: $SOURCE_INDEX" ;;
esac

export WIMLIB_IMAGEX_IGNORE_CASE=1
WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

wim_field() {
  local Image="$1" Index="$2" Field="$3"
  wimlib-imagex info "$Image" "$Index" 2>/dev/null |
    awk -F: -v Wanted="$Field" '
      {
        Name = $1
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", Name)
      }
      tolower(Name) == tolower(Wanted) {
        sub(/^[[:space:]]+/, "", $2); print $2; exit
      }
    '
}

hive_control_set() {
  local Hive="$1" Current
  Current="$(hivexget "$Hive" Select Current 2>/dev/null || true)"
  case "$Current" in
    ''|*[!0-9]*) return 1 ;;
  esac
  printf 'ControlSet%03d\n' "$Current"
}

# A previous successful injection is cheap to validate and avoids unpacking a
# multi-gigabyte install image on every --usb-test build.
mkdir -p "$WORK/precheck-driver" "$WORK/precheck-manifest" "$WORK/precheck-hive"
if wimlib-imagex extract "$TARGET_WIM" 1 /Windows/System32/drivers/UrsCx01000.sys \
     --dest-dir="$WORK/precheck-driver" --no-acls >/dev/null 2>&1 && \
   wimlib-imagex extract "$TARGET_WIM" 1 /Windows/B2LURS/manifest.txt \
     --dest-dir="$WORK/precheck-manifest" --no-acls >/dev/null 2>&1 && \
   wimlib-imagex extract "$TARGET_WIM" 1 /Windows/System32/config/SYSTEM \
     --dest-dir="$WORK/precheck-hive" --no-acls >/dev/null 2>&1; then
  PRECHECK_HASH="$(sha256sum "$WORK/precheck-driver/UrsCx01000.sys" | awk '{print $1}')"
  MANIFEST_HASH="$(sed -n 's/^B2LURS_SYS_SHA256=//p' "$WORK/precheck-manifest/manifest.txt" | head -n 1)"
  MANIFEST_PACKAGE="$(sed -n 's/^B2LURS_PACKAGE=//p' "$WORK/precheck-manifest/manifest.txt" | head -n 1)"
  PRECHECK_CS="$(hive_control_set "$WORK/precheck-hive/SYSTEM" || true)"
  if [ "$PRECHECK_HASH" = "$MANIFEST_HASH" ] && \
     [ "$MANIFEST_PACKAGE" = "microsoft-urscx-winpe-runtime-v1" ] && \
     [ -n "$PRECHECK_CS" ] && \
     hivexget "$WORK/precheck-hive/SYSTEM" \
       "$PRECHECK_CS\\Services\\UrsCx01000" ImagePath >/dev/null 2>&1; then
    say "WinPE zaten dogrulanmis Microsoft UrsCx runtime iceriyor"
    echo "  UrsCx01000.sys SHA256: ${PRECHECK_HASH:0:16}"
    exit 0
  fi
fi

for Tool in 7z file; do
  command -v "$Tool" >/dev/null || die "$Tool yok"
done

if [ -z "$SOURCE" ]; then
  for Candidate in \
    "$HOME"/*ARM64*.iso \
    "$HOME"/*Arm64*.iso \
    "$HOME"/*arm64*.iso \
    /mnt/c/Users/*/Downloads/*ARM64*.iso \
    /mnt/c/Users/*/Downloads/*Arm64*.iso \
    /mnt/c/Users/*/Downloads/*arm64*.iso \
    /mnt/c/Users/*/Downloads/*.iso; do
    [ -f "$Candidate" ] && { SOURCE="$Candidate"; break; }
  done
fi
[ -f "$SOURCE" ] || {
  die "ARM64 Windows ISO/install.wim bulunamadi; B2L_WINDOWS_ARM64_SOURCE ayarla"
}

SOURCE_WIM="$SOURCE"
case "${SOURCE,,}" in
  *.iso)
    say "Windows kurulum imaji ISO'dan cikariliyor"
    ISO_LIST="$WORK/iso-list.txt"
    7z l -slt "$SOURCE" > "$ISO_LIST"
    INSTALL_PATH="$(
      sed -n 's/^Path = //p' "$ISO_LIST" |
        grep -i -m1 -E '(^|[/\\])sources[/\\]install\.(wim|esd)$' || true
    )"
    [ -n "$INSTALL_PATH" ] || die "ISO icinde sources/install.wim veya install.esd yok"
    7z e -o"$WORK" "$SOURCE" "$INSTALL_PATH" >/dev/null
    SOURCE_WIM="$WORK/$(basename "${INSTALL_PATH//\\//}")"
    [ -s "$SOURCE_WIM" ] || die "kurulum WIM/ESD dosyasi ISO'dan cikarilamadi"
    ;;
  *.wim|*.esd) ;;
  *) die "kaynak .iso, .wim veya .esd olmali: $SOURCE" ;;
esac

wimlib-imagex info "$TARGET_WIM" 1 >/dev/null 2>&1 || \
  die "hedef WinPE indeksi okunamadi: $TARGET_WIM:1"
wimlib-imagex info "$SOURCE_WIM" "$SOURCE_INDEX" >/dev/null 2>&1 || \
  die "Windows kaynak indeksi okunamadi: $SOURCE_WIM:$SOURCE_INDEX"

TARGET_ARCH="$(wim_field "$TARGET_WIM" 1 Architecture)"
SOURCE_ARCH="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" Architecture)"
TARGET_MAJOR="$(wim_field "$TARGET_WIM" 1 'Major Version')"
TARGET_MINOR="$(wim_field "$TARGET_WIM" 1 'Minor Version')"
TARGET_BUILD="$(wim_field "$TARGET_WIM" 1 Build)"
SOURCE_MAJOR="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" 'Major Version')"
SOURCE_MINOR="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" 'Minor Version')"
SOURCE_BUILD="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" Build)"
TARGET_VERSION="$TARGET_MAJOR.$TARGET_MINOR.$TARGET_BUILD"
SOURCE_VERSION="$SOURCE_MAJOR.$SOURCE_MINOR.$SOURCE_BUILD"

case "${TARGET_ARCH,,}" in *arm64*|*aarch64*) ;; *) die "hedef WinPE ARM64 degil: ${TARGET_ARCH:-bilinmiyor}" ;; esac
case "${SOURCE_ARCH,,}" in *arm64*|*aarch64*) ;; *) die "Windows kaynagi ARM64 degil: ${SOURCE_ARCH:-bilinmiyor}" ;; esac
for VersionPart in \
  "$TARGET_MAJOR" "$TARGET_MINOR" "$TARGET_BUILD" \
  "$SOURCE_MAJOR" "$SOURCE_MINOR" "$SOURCE_BUILD"; do
  case "$VersionPart" in
    ''|*[!0-9]*) die "WinPE/Windows surum bilgisi okunamadi: hedef=$TARGET_VERSION kaynak=$SOURCE_VERSION" ;;
  esac
done
[ "$TARGET_MAJOR" = "$SOURCE_MAJOR" ] && [ "$TARGET_MINOR" = "$SOURCE_MINOR" ] || {
  die "UrsCx ve WinPE ayni Windows surum ailesinde olmali: hedef=$TARGET_VERSION kaynak=$SOURCE_VERSION"
}
if [ "$SOURCE_BUILD" -gt "$TARGET_BUILD" ]; then
  die "daha yeni UrsCx eski WinPE'ye eklenmez: hedef=$TARGET_VERSION kaynak=$SOURCE_VERSION"
elif [ "$SOURCE_BUILD" -lt "$TARGET_BUILD" ]; then
  URS_COMPATIBILITY="older-urscx-1.0-on-newer-winpe"
  echo "  UYARI: kaynak UrsCx 1.0 hedef WinPE'den eski; cihaz testi deneysel kabul edilecek"
else
  URS_COMPATIBILITY="exact-build"
fi

say "Microsoft UrsCx kaynagi dogrulandi"
echo "  kaynak : $SOURCE"
echo "  indeks : $SOURCE_INDEX"
echo "  mimari : $SOURCE_ARCH"
echo "  build  : $SOURCE_VERSION"

mkdir -p "$WORK/source-driver" "$WORK/source-hive" "$WORK/target-hive"
wimlib-imagex extract "$SOURCE_WIM" "$SOURCE_INDEX" \
  /Windows/System32/drivers/UrsCx01000.sys \
  --dest-dir="$WORK/source-driver" --no-acls >/dev/null || \
  die "kaynak Windows imajinda UrsCx01000.sys yok (indeks=$SOURCE_INDEX)"
wimlib-imagex extract "$SOURCE_WIM" "$SOURCE_INDEX" \
  /Windows/System32/config/SYSTEM \
  --dest-dir="$WORK/source-hive" --no-acls >/dev/null || \
  die "kaynak Windows SYSTEM hive cikarilamadi"
wimlib-imagex extract "$TARGET_WIM" 1 \
  /Windows/System32/config/SYSTEM \
  --dest-dir="$WORK/target-hive" --no-acls >/dev/null || \
  die "hedef WinPE SYSTEM hive cikarilamadi"

URS_SYS="$WORK/source-driver/UrsCx01000.sys"
SOURCE_SYSTEM="$WORK/source-hive/SYSTEM"
TARGET_SYSTEM="$WORK/target-hive/SYSTEM"
[ -s "$URS_SYS" ] || die "UrsCx01000.sys cikarimi bos"
file "$URS_SYS" | grep -Eqi 'aarch64|arm64' || die "kaynak UrsCx01000.sys ARM64 degil"

SOURCE_CS="$(hive_control_set "$SOURCE_SYSTEM" || true)"
TARGET_CS="$(hive_control_set "$TARGET_SYSTEM" || true)"
[ -n "$SOURCE_CS" ] || die "kaynak SYSTEM Select\\Current okunamadi"
[ -n "$TARGET_CS" ] || die "hedef SYSTEM Select\\Current okunamadi"

SOURCE_REG="$WORK/urscx-source.reg"
TARGET_REG="$WORK/urscx-target.reg"
hivexregedit --export --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' \
  "$SOURCE_SYSTEM" "$SOURCE_CS\\Services\\UrsCx01000" > "$SOURCE_REG" || \
  die "kaynak Services\\UrsCx01000 anahtari disari aktarilamadi"
grep -Fq '\Services\UrsCx01000]' "$SOURCE_REG" || \
  die "kaynak UrsCx servis kaydi bos veya beklenmeyen bicimde"

SOURCE_NEEDLE="\\${SOURCE_CS}\\"
TARGET_NEEDLE="\\${TARGET_CS}\\"
while IFS= read -r Line || [ -n "$Line" ]; do
  printf '%s\n' "${Line//$SOURCE_NEEDLE/$TARGET_NEEDLE}"
done < "$SOURCE_REG" > "$TARGET_REG"

hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' \
  "$TARGET_SYSTEM" "$TARGET_REG" || die "UrsCx servis kaydi hedef SYSTEM hive'a yazilamadi"
for Value in Type Start ErrorControl ImagePath; do
  hivexget "$TARGET_SYSTEM" "$TARGET_CS\\Services\\UrsCx01000" "$Value" >/dev/null 2>&1 || \
    die "aktarilan UrsCx servis kaydinda $Value yok"
done

MANIFEST="$WORK/manifest.txt"
{
  echo "B2LURS_PACKAGE=microsoft-urscx-winpe-runtime-v1"
  echo "B2LURS_TARGET_BUILD=$TARGET_VERSION"
  echo "B2LURS_SOURCE_BUILD=$SOURCE_VERSION"
  echo "B2LURS_COMPATIBILITY=$URS_COMPATIBILITY"
  echo "B2LURS_CLASS_EXTENSION_INTERFACE=1.0"
  echo "B2LURS_SOURCE_ARCH=$SOURCE_ARCH"
  echo "B2LURS_SOURCE_INDEX=$SOURCE_INDEX"
  echo "B2LURS_SERVICE_CONTROL_SET=$TARGET_CS"
  echo "B2LURS_SYS_SHA256=$(sha256sum "$URS_SYS" | awk '{print $1}')"
  echo "B2LURS_SERVICE_EXPORT_SHA256=$(sha256sum "$TARGET_REG" | awk '{print $1}')"
} > "$MANIFEST"

say "Microsoft UrsCx runtime ve servis kaydi boot.wim'e ekleniyor"
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="delete --force /Windows/System32/drivers/UrsCx01000.sys" >/dev/null 2>&1 || true
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$URS_SYS' /Windows/System32/drivers/UrsCx01000.sys" >/dev/null
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$TARGET_SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="delete --force /Windows/B2LURS/manifest.txt" >/dev/null 2>&1 || true
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$MANIFEST' /Windows/B2LURS/manifest.txt" >/dev/null

mkdir -p "$WORK/verify-driver" "$WORK/verify-hive" "$WORK/verify-manifest"
wimlib-imagex extract "$TARGET_WIM" 1 /Windows/System32/drivers/UrsCx01000.sys \
  --dest-dir="$WORK/verify-driver" --no-acls >/dev/null
wimlib-imagex extract "$TARGET_WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$WORK/verify-hive" --no-acls >/dev/null
wimlib-imagex extract "$TARGET_WIM" 1 /Windows/B2LURS/manifest.txt \
  --dest-dir="$WORK/verify-manifest" --no-acls >/dev/null
[ "$(sha256sum "$URS_SYS" | awk '{print $1}')" = \
  "$(sha256sum "$WORK/verify-driver/UrsCx01000.sys" | awk '{print $1}')" ] || \
  die "UrsCx01000.sys WIM geri-okuma hash'i farkli"
[ "$(sha256sum "$MANIFEST" | awk '{print $1}')" = \
  "$(sha256sum "$WORK/verify-manifest/manifest.txt" | awk '{print $1}')" ] || \
  die "B2LURS manifest WIM geri-okuma hash'i farkli"
for Value in Type Start ErrorControl ImagePath; do
  hivexget "$WORK/verify-hive/SYSTEM" "$TARGET_CS\\Services\\UrsCx01000" "$Value" >/dev/null 2>&1 || \
    die "WIM geri-okumada UrsCx servis degeri yok: $Value"
done

say "Microsoft UrsCx WinPE runtime hazir"
echo "  SYS SHA256 : $(sha256sum "$URS_SYS" | awk '{print substr($1,1,16)}')"
echo "  servis     : $TARGET_CS\\Services\\UrsCx01000 (kaynaktan birebir)"
echo "  manifest   : /Windows/B2LURS/manifest.txt"
