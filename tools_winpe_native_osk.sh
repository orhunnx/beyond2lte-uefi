#!/usr/bin/env bash
# Inject Microsoft's classic ARM64 On-Screen Keyboard into the current WinPE.
#
# WinPE has no official OSK optional component.  This script therefore copies
# the native osk.exe from a matching ARM64 Windows install image and follows
# its PE import table, adding only DLLs that the target WinPE does not already
# contain.  It never substitutes a locally-built keyboard.
#
# Usage:
#   bash tools_winpe_native_osk.sh [/path/Windows11_Arm64.iso|install.wim] [index]
#
# With no argument, B2L_WINDOWS_ARM64_SOURCE and the same ISO locations used by
# tools_winboot.sh are searched.  The source and target must have the same
# major.minor.build number and both must be ARM64.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_WIM="$HERE/esp/sources/boot.wim"
SOURCE="${1:-${B2L_WINDOWS_ARM64_SOURCE:-}}"
SOURCE_INDEX="${2:-${B2L_WINDOWS_ARM64_INDEX:-1}}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

for Tool in wimlib-imagex 7z file sha256sum; do
  command -v "$Tool" >/dev/null || die "$Tool yok"
done
if command -v llvm-readobj >/dev/null; then
  IMPORT_TOOL=llvm-readobj
elif command -v llvm-readobj-18 >/dev/null; then
  IMPORT_TOOL=llvm-readobj-18
elif command -v objdump >/dev/null; then
  IMPORT_TOOL=objdump
else
  die "PE bagimliliklarini okuyacak llvm-readobj veya objdump yok"
fi

[ -s "$TARGET_WIM" ] || die "boot.wim yok: $TARGET_WIM"
case "$SOURCE_INDEX" in
  ''|*[!0-9]*) die "Windows kaynak indeksi sayi olmali: $SOURCE_INDEX" ;;
esac

# Repeated touch-driver builds must not unpack the multi-gigabyte install.wim
# again.  A native OSK already present in the target is left byte-for-byte
# untouched.
if wimlib-imagex dir "$TARGET_WIM" 1 /Windows/System32/osk.exe >/dev/null 2>&1; then
  say "WinPE zaten native osk.exe iceriyor"
  exit 0
fi

if [ -z "$SOURCE" ]; then
  for Candidate in \
    "$HOME"/*Arm64*.iso \
    "$HOME"/*.iso \
    /mnt/c/Users/*/Downloads/*Arm64*.iso \
    /mnt/c/Users/*/Downloads/*.iso; do
    [ -f "$Candidate" ] && { SOURCE="$Candidate"; break; }
  done
fi
[ -f "$SOURCE" ] || {
  die "eslesen ARM64 Windows ISO/install.wim bulunamadi; B2L_WINDOWS_ARM64_SOURCE ayarla"
}

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

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
    SOURCE_WIM="$WORK/$(basename "${INSTALL_PATH//\\/\/}")"
    [ -s "$SOURCE_WIM" ] || die "kurulum WIM/ESD dosyasi ISO'dan cikarilamadi"
    ;;
  *.wim|*.esd) ;;
  *) die "kaynak .iso, .wim veya .esd olmali: $SOURCE" ;;
esac

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

TARGET_ARCH="$(wim_field "$TARGET_WIM" 1 Architecture)"
SOURCE_ARCH="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" Architecture)"
TARGET_VERSION="$(wim_field "$TARGET_WIM" 1 'Major Version').$(wim_field "$TARGET_WIM" 1 'Minor Version').$(wim_field "$TARGET_WIM" 1 Build)"
SOURCE_VERSION="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" 'Major Version').$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" 'Minor Version').$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" Build)"
TARGET_LANG="$(wim_field "$TARGET_WIM" 1 'Default Language')"
SOURCE_LANG="$(wim_field "$SOURCE_WIM" "$SOURCE_INDEX" 'Default Language')"

case "${TARGET_ARCH,,}" in *arm64*|*aarch64*) ;; *) die "hedef WinPE ARM64 degil: ${TARGET_ARCH:-bilinmiyor}" ;; esac
case "${SOURCE_ARCH,,}" in *arm64*|*aarch64*) ;; *) die "Windows kaynagi ARM64 degil: ${SOURCE_ARCH:-bilinmiyor}" ;; esac
[ "$TARGET_VERSION" = "$SOURCE_VERSION" ] || {
  die "WinPE ve OSK kaynagi ayni build degil: hedef=$TARGET_VERSION kaynak=$SOURCE_VERSION"
}

say "Native Windows OSK kaynagi dogrulandi"
echo "  kaynak : $SOURCE"
echo "  indeks : $SOURCE_INDEX"
echo "  mimari : $SOURCE_ARCH"
echo "  build  : $SOURCE_VERSION"
echo "  dil    : ${SOURCE_LANG:-bilinmiyor}"

SOURCE_FILES="$WORK/source-files"
mkdir -p "$SOURCE_FILES"
extract_source_system32() {
  local Name="$1" Output="$SOURCE_FILES/$Name"
  local Found
  [ -s "$Output" ] && return 0
  wimlib-imagex extract "$SOURCE_WIM" "$SOURCE_INDEX" \
    "/Windows/System32/$Name" --dest-dir="$SOURCE_FILES" --no-acls >/dev/null 2>&1 || return 1
  if [ ! -s "$Output" ]; then
    Found="$(find "$SOURCE_FILES" -maxdepth 1 -type f -iname "$Name" -print -quit)"
    [ -n "$Found" ] || return 1
    mv -f "$Found" "$Output"
  fi
  [ -s "$Output" ]
}

pe_imports() {
  local Binary="$1"
  if [ "$IMPORT_TOOL" = objdump ]; then
    objdump -p "$Binary" 2>/dev/null |
      awk 'tolower($1) == "dll" && tolower($2) == "name:" { print $3 }'
  else
    "$IMPORT_TOOL" --coff-imports "$Binary" 2>/dev/null |
      sed -n 's/^[[:space:]]*Name: \([^[:space:]].*\.dll\)$/\1/ip'
  fi
}

target_has_system32() {
  wimlib-imagex dir "$TARGET_WIM" 1 "/Windows/System32/$1" >/dev/null 2>&1
}

extract_source_system32 osk.exe || die "kaynak Windows imajinda System32/osk.exe yok"
file "$SOURCE_FILES/osk.exe" | grep -Eqi 'aarch64|arm64' || die "kaynak osk.exe ARM64 degil"

# Breadth-first dependency closure.  Existing WinPE DLLs remain untouched;
# only missing imports are sourced from the exact same Windows build.
QUEUE_FILE="$WORK/queue.txt"
DONE_FILE="$WORK/done.txt"
COPY_FILE="$WORK/copy.txt"
: > "$QUEUE_FILE"
: > "$DONE_FILE"
: > "$COPY_FILE"
printf '%s\n' osk.exe >> "$QUEUE_FILE"

while IFS= read -r Current; do
  CurrentLower="${Current,,}"
  grep -Fqxi "$CurrentLower" "$DONE_FILE" && continue
  printf '%s\n' "$CurrentLower" >> "$DONE_FILE"
  [ -s "$SOURCE_FILES/$Current" ] || extract_source_system32 "$Current" || {
    die "Windows kaynagindan bagimlilik cikarilamadi: $Current"
  }
  while IFS= read -r Dependency; do
    [ -n "$Dependency" ] || continue
    Dependency="$(basename "${Dependency//\\/\/}")"
    DependencyLower="${Dependency,,}"
    case "$DependencyLower" in
      api-ms-win-*.dll|ext-ms-win-*.dll) continue ;;
    esac
    if target_has_system32 "$Dependency"; then
      continue
    fi
    if ! grep -Fqxi "$DependencyLower" "$COPY_FILE"; then
      extract_source_system32 "$Dependency" || {
        die "WinPE'de olmayan OSK bagimliligi kaynakta da yok: $Dependency"
      }
      file "$SOURCE_FILES/$Dependency" | grep -Eqi 'aarch64|arm64' || {
        die "OSK bagimliligi gecerli ARM64 PE degil: $Dependency"
      }
      printf '%s\n' "$Dependency" >> "$COPY_FILE"
      printf '%s\n' "$Dependency" >> "$QUEUE_FILE"
    fi
  done < <(pe_imports "$SOURCE_FILES/$Current" | sort -fu)
done < "$QUEUE_FILE"

LANGUAGE="${TARGET_LANG:-${SOURCE_LANG:-en-US}}"
MUI_SOURCE=""
for CandidateLanguage in "$LANGUAGE" "$SOURCE_LANG" en-US; do
  [ -n "$CandidateLanguage" ] || continue
  MUI_DIR="$WORK/mui-$CandidateLanguage"
  mkdir -p "$MUI_DIR"
  if wimlib-imagex extract "$SOURCE_WIM" "$SOURCE_INDEX" \
      "/Windows/System32/$CandidateLanguage/osk.exe.mui" \
      --dest-dir="$MUI_DIR" --no-acls >/dev/null 2>&1 && \
      [ -s "$MUI_DIR/osk.exe.mui" ]; then
    LANGUAGE="$CandidateLanguage"
    MUI_SOURCE="$MUI_DIR/osk.exe.mui"
    break
  fi
done
[ -n "$MUI_SOURCE" ] || die "osk.exe.mui kaynak Windows imajinda bulunamadi"

MANIFEST="$WORK/manifest.txt"
{
  echo "B2LOSK_PACKAGE=microsoft-classic-osk-import-closure"
  echo "B2LOSK_SOURCE_BUILD=$SOURCE_VERSION"
  echo "B2LOSK_SOURCE_ARCH=$SOURCE_ARCH"
  echo "B2LOSK_LANGUAGE=$LANGUAGE"
  echo "B2LOSK_OSK_SHA256=$(sha256sum "$SOURCE_FILES/osk.exe" | awk '{print $1}')"
  while IFS= read -r Dependency; do
    [ -n "$Dependency" ] || continue
    echo "B2LOSK_DLL_${Dependency^^}_SHA256=$(sha256sum "$SOURCE_FILES/$Dependency" | awk '{print $1}')"
  done < "$COPY_FILE"
  echo "B2LOSK_MUI_SHA256=$(sha256sum "$MUI_SOURCE" | awk '{print $1}')"
} > "$MANIFEST"

say "Native OSK ve eksik bagimliliklari boot.wim'e ekleniyor"
echo "  eklenecek DLL: $(grep -c . "$COPY_FILE" || true)"
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$SOURCE_FILES/osk.exe' /Windows/System32/osk.exe" >/dev/null
while IFS= read -r Dependency; do
  [ -n "$Dependency" ] || continue
  wimlib-imagex update "$TARGET_WIM" 1 \
    --command="add '$SOURCE_FILES/$Dependency' /Windows/System32/$Dependency" >/dev/null
done < "$COPY_FILE"
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$MUI_SOURCE' /Windows/System32/$LANGUAGE/osk.exe.mui" >/dev/null
wimlib-imagex update "$TARGET_WIM" 1 \
  --command="add '$MANIFEST' /Windows/B2LOSK/manifest.txt" >/dev/null

VERIFY="$WORK/verify"
mkdir -p "$VERIFY"
verify_wim_file() {
  local SourceFile="$1" WimPath="$2" Label="$3" ExtractDir="$VERIFY/$Label"
  mkdir -p "$ExtractDir"
  wimlib-imagex extract "$TARGET_WIM" 1 "$WimPath" \
    --dest-dir="$ExtractDir" --no-acls >/dev/null
  local Extracted="$ExtractDir/$(basename "$WimPath")"
  [ "$(sha256sum "$SourceFile" | awk '{print $1}')" = \
    "$(sha256sum "$Extracted" | awk '{print $1}')" ] || die "$Label WIM geri-okuma hash'i farkli"
}

verify_wim_file "$SOURCE_FILES/osk.exe" /Windows/System32/osk.exe osk
while IFS= read -r Dependency; do
  [ -n "$Dependency" ] || continue
  verify_wim_file "$SOURCE_FILES/$Dependency" "/Windows/System32/$Dependency" "dll-$Dependency"
done < "$COPY_FILE"
verify_wim_file "$MUI_SOURCE" "/Windows/System32/$LANGUAGE/osk.exe.mui" mui
verify_wim_file "$MANIFEST" /Windows/B2LOSK/manifest.txt manifest

say "Windows native OSK hazir"
echo "  acilis: %SystemRoot%\\System32\\osk.exe"
echo "  manifest: %SystemRoot%\\B2LOSK\\manifest.txt"
