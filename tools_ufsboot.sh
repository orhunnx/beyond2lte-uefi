#!/usr/bin/env bash
# B2LUFS ile UFS'ten Windows boot denemesi icin SYSTEM+CACHE Odin paketi uretir.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$ROOT/out"
SYSTEM_GIB="${2:-4}"

usage() {
  cat <<'EOF'
Kullanim:
  bash tools_ufsboot.sh /mnt/c/yol/Windows11_ARM64.iso [system-boyutu-GiB]

Bu arac b2lufs.sys DERLEMEZ. SYS once Windows PowerShell'de uretilmelidir.
Geri kalan WIM, BCD, SYSTEM/CACHE ve Odin paketleme islemleri WSL'de yapilir.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi
if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage >&2
  exit 2
fi

ISO="$1"

if ! grep -Eiq 'microsoft|wsl' /proc/version 2>/dev/null; then
  echo "HATA: Bu arac WSL icinde calistirilmalidir." >&2
  exit 1
fi
[[ -f "$ISO" ]] || { echo "HATA: ISO bulunamadi: $ISO" >&2; exit 1; }
[[ "$SYSTEM_GIB" =~ ^[0-9]+$ ]] && (( SYSTEM_GIB >= 2 && SYSTEM_GIB <= 5 )) || {
  echo "HATA: SYSTEM boyutu 2..5 GiB arasinda tam sayi olmali." >&2
  exit 1
}

for tool in 7z cmd.exe hivexget hivexregedit img2simg mcopy mdir mkfs.vfat \
  mkntfs ntfsls sha256sum tar wimlib-imagex wslpath; do
  command -v "$tool" >/dev/null || {
    echo "HATA: gerekli arac bulunamadi: $tool" >&2
    echo "WSL paketleri: sudo apt install -y p7zip-full wimtools libhivex-bin libwin-hivex-perl ntfs-3g mtools dosfstools android-sdk-libsparse-utils" >&2
    exit 1
  }
done

SYS="$OUT/b2lufs/b2lufs.sys"
if [[ ! -s "$SYS" ]]; then
  echo "HATA: $SYS yok." >&2
  echo "b2lufs.sys dosyasini yalniz Windows PowerShell'de derle:" >&2
  echo 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"' >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> 1/7 Windows ARM64 WinPE dosyalari hazirlaniyor"
bash "$ROOT/tools_winboot.sh" "$ISO"

echo "==> 2/7 Var olan PowerShell SYS ile B2LUFS paketi uretiliyor"
B2L_SKIP_MSVC_BUILD=1 bash "$ROOT/tools_b2lufs.sh"

echo "==> 3/7 B2LUFS WIM'e boot-critical olarak yerlestiriliyor"
bash "$ROOT/tools_ufsboot_prepare.sh"

echo "==> 4/7 Disk boot BCD'si Windows bcdedit ile uretiliyor"
BCD_SCRIPT_WIN="$(wslpath -w "$ROOT/windows/bcd_olustur.cmd")"
BCD_OUT_WIN="$(wslpath -w "$ROOT/windows/BCD")"
cmd.exe /d /s /c "\"$BCD_SCRIPT_WIN\" disk \"$BCD_OUT_WIN\""
[[ -s "$ROOT/windows/BCD" ]] || { echo "HATA: windows/BCD uretilmedi." >&2; exit 1; }
if [[ "$(head -c 4 "$ROOT/windows/BCD")" != "regf" ]]; then
  echo "HATA: windows/BCD gecerli bir registry hive degil." >&2
  exit 1
fi

echo "==> 5/7 Windows SYSTEM imaji uretiliyor"
bash "$ROOT/tools_windisk.sh" "$SYSTEM_GIB"

echo "==> 6/7 CACHE disk-boot ESP imaji uretiliyor"
bash "$ROOT/tools_winboot.sh" "$ISO" --disk
bash "$ROOT/cache_esp.sh" --win --disk --baseline

echo "==> 7/7 Odin SYSTEM+CACHE paketi birlestiriliyor"
mkdir -p "$WORK/system" "$WORK/cache" "$WORK/package"
tar -xf "$OUT/system-esp.tar" -C "$WORK/system"
tar -xf "$OUT/cache-esp.tar" -C "$WORK/cache"
[[ -s "$WORK/system/system.img" ]] || { echo "HATA: system.img yok." >&2; exit 1; }
[[ -s "$WORK/cache/cache.img" ]] || { echo "HATA: cache.img yok." >&2; exit 1; }
cp "$WORK/system/system.img" "$WORK/package/system.img"
cp "$WORK/cache/cache.img" "$WORK/package/cache.img"
tar -C "$WORK/package" -cf "$OUT/ufs-diskboot.tar" system.img cache.img

mapfile -t members < <(tar -tf "$OUT/ufs-diskboot.tar")
if (( ${#members[@]} != 2 )) || [[ "${members[0]}" != "system.img" || "${members[1]}" != "cache.img" ]]; then
  echo "HATA: ufs-diskboot.tar yalniz system.img ve cache.img icermeli." >&2
  exit 1
fi

if desktop="$(wslpath "$(cmd.exe /d /c 'echo %USERPROFILE%\Desktop' 2>/dev/null | tr -d '\r')" 2>/dev/null)" \
  && [[ -d "$desktop" ]]; then
  cp "$OUT/ufs-diskboot.tar" "$desktop/ufs-diskboot.tar"
  echo "Masaustu kopyasi: $desktop/ufs-diskboot.tar"
fi

echo
echo "BASARILI: $OUT/ufs-diskboot.tar"
echo "Odin: AP alanina bu TAR'i sec; Re-Partition KAPALI kalsin."
echo "Yalniz SYSTEM ve CACHE yazilir; boot/recovery/stock bolumlerine dokunulmaz."
echo "Ilk cihaz testi ve geri donus adimlari: UFS_DISK_BOOT.md"
