#!/usr/bin/env bash
# B2LUFS ile UFS'ten Windows boot denemesi icin SYSTEM+CACHE Odin paketi uretir.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$ROOT/out"
SYSTEM_GIB="${2:-4}"

usage() {
  cat <<'EOF'
Kullanim:
  bash tools_ufsboot.sh /mnt/c/yol/Windows_ARM64.iso [system-boyutu-GiB]

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

for tool in 7z base64 clang cmd.exe cmp file hivexget hivexregedit hivexsh img2simg \
  lld-link llvm-dlltool mcopy mdir mkfs.vfat mkntfs mmd ntfsls openssl \
  osslsigncode powershell.exe readlink sed sha1sum sha256sum stat tar tr \
  wimlib-imagex wslpath; do
  command -v "$tool" >/dev/null || {
    echo "HATA: gerekli arac bulunamadi: $tool" >&2
    echo "WSL paketleri: sudo apt install -y p7zip-full clang lld llvm file osslsigncode openssl wimtools libhivex-bin libwin-hivex-perl ntfs-3g mtools dosfstools android-sdk-libsparse-utils" >&2
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

prepare_disk_bcd() {
  local expected_size actual_size ready_text desktop_b64

  # Windows PowerShell 5.1 konsol ciktisini OEM kod sayfasinda verebilir;
  # Masaustu gibi Turkce yollar WSL borusunda Masa�st� olarak bozulur. Yolu
  # PowerShell tarafinda UTF-8 baytlarina, sonra ASCII Base64'e cevir. WSL
  # yalniz ASCII tasir ve asil Unicode yolu burada geri acar.
  desktop_b64="$(powershell.exe -NoProfile -NonInteractive -Command \
    '[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([Environment]::GetFolderPath("Desktop")))' \
    2>/dev/null | tr -d '\r\n' || true)"
  if [[ -z "$desktop_b64" || ! "$desktop_b64" =~ ^[A-Za-z0-9+/]+={0,2}$ ]]; then
    echo "HATA: Windows Masaustu yolu PowerShell'den okunamadi." >&2
    return 1
  fi
  WIN_DESKTOP_WIN="$(printf '%s' "$desktop_b64" | base64 --decode 2>/dev/null || true)"
  WIN_DESKTOP_WSL="$(wslpath -u "$WIN_DESKTOP_WIN" 2>/dev/null || true)"
  if [[ -z "$WIN_DESKTOP_WIN" || ! -d "$WIN_DESKTOP_WSL" || ! -w "$WIN_DESKTOP_WSL" ]]; then
    echo "HATA: Windows Masaustu dizini WSL'den kullanilamiyor: ${WIN_DESKTOP_WIN:-OKUNAMADI}" >&2
    return 1
  fi

  BCD_MANUAL_DIR="$WIN_DESKTOP_WSL/B2L-UFS-BCD"
  BCD_MANUAL_DIR_WIN="$(wslpath -w "$BCD_MANUAL_DIR")"
  BCD_MANUAL_OUT="$BCD_MANUAL_DIR/BCD.disk"
  BCD_MANUAL_READY="$BCD_MANUAL_DIR/BCD.disk.ready"
  mkdir -p "$BCD_MANUAL_DIR"
  sed 's/\r*$/\r/' "$ROOT/windows/bcd_olustur.cmd" >"$BCD_MANUAL_DIR/bcd_olustur.cmd"
  sed 's/\r*$/\r/' "$ROOT/windows/B2L_BCD_DISK_OLUSTUR.cmd" \
    >"$BCD_MANUAL_DIR/B2L_BCD_DISK_OLUSTUR.cmd"

  disk_bcd_ready() {
    [[ -s "$BCD_MANUAL_OUT" && -s "$BCD_MANUAL_READY" ]] || return 1
    ready_text="$(tr -d '\r' <"$BCD_MANUAL_READY")"
    grep -Fxq 'B2L_BCD_STAGE_VERSION=1' <<<"$ready_text" || return 1
    grep -Fxq 'B2L_BCD_VARIANT=disk' <<<"$ready_text" || return 1
    [[ "$(head -c 4 "$BCD_MANUAL_OUT")" == "regf" ]] || return 1
    expected_size="$(sed -n 's/^B2L_BCD_SIZE=//p' <<<"$ready_text")"
    actual_size="$(stat -c%s "$BCD_MANUAL_OUT")"
    [[ "$expected_size" =~ ^[0-9]+$ && "$expected_size" == "$actual_size" ]]
  }

  if ! disk_bcd_ready; then
    echo
    echo "==> Disk BCD'si Windows Masaustunde bir kez uretilmeli"
    echo "  klasor : $BCD_MANUAL_DIR_WIN"
    echo "  Yonetici Komut Istemi'nde:"
    echo "    cd /d \"$BCD_MANUAL_DIR_WIN\""
    echo "    B2L_BCD_DISK_OLUSTUR.cmd"
    echo
    if [[ -t 0 ]]; then
      read -r -p "  Betik BASARILI dedikten sonra buraya donup ENTER'a basin... "
    else
      echo "HATA: Etkilesimli terminal yok; yukaridaki BCD betigini calistirip tekrar deneyin." >&2
      return 1
    fi
  fi

  if ! disk_bcd_ready; then
    echo "HATA: Masaustunde dogrulanmis disk BCD'si bulunamadi." >&2
    echo "Yonetici CMD'deki ilk bcdedit hatasini kontrol edin: $BCD_MANUAL_DIR_WIN" >&2
    return 1
  fi

  cp "$BCD_MANUAL_OUT" "$ROOT/windows/BCD"
  cmp -s "$BCD_MANUAL_OUT" "$ROOT/windows/BCD" || {
    echo "HATA: dogrulanan disk BCD'si repoya kopyalanamadi." >&2
    return 1
  }
  BCD_ACCEPTED_HASH="$(sha256sum "$ROOT/windows/BCD" | awk '{print $1}')"
  {
    echo 'B2L_BCD_VARIANT=disk'
    echo "B2L_BCD_SHA256=$BCD_ACCEPTED_HASH"
    echo "B2L_BCD_SIZE=$actual_size"
    echo "B2L_BCD_SOURCE=$BCD_MANUAL_DIR_WIN\\BCD.disk"
  } >"$OUT/disk-bcd-source.txt"
  echo "  Disk BCD dogrulandi: regf, $actual_size bayt, SHA256=${BCD_ACCEPTED_HASH:0:16}"
}

prepare_disk_bcd

WORK="$(mktemp -d)"
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> 1/7 Windows ARM64 WinPE dosyalari hazirlaniyor"
bash "$ROOT/tools_winboot.sh" "$ISO"

echo "==> 2/7 Var olan PowerShell SYS ile B2LUFS paketi uretiliyor"
B2L_SKIP_MSVC_BUILD=1 bash "$ROOT/tools_b2lufs.sh"

echo "==> 3/7 B2LUFS WIM'e boot-critical olarak yerlestiriliyor"
bash "$ROOT/tools_ufsboot_prepare.sh"

echo "==> 4/7 Onceden uretilen disk boot BCD'si dogrulaniyor"
[[ -s "$ROOT/windows/BCD" && "$(head -c 4 "$ROOT/windows/BCD")" == "regf" ]] || {
  echo "HATA: hazirlanan disk BCD'si kayboldu veya bozuldu." >&2
  exit 1
}
[[ "$(sha256sum "$ROOT/windows/BCD" | awk '{print $1}')" == "$BCD_ACCEPTED_HASH" ]] || {
  echo "HATA: disk BCD'si paketleme sirasinda degisti." >&2
  exit 1
}
echo "  Disk BCD ayni: SHA256=${BCD_ACCEPTED_HASH:0:16}"

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
