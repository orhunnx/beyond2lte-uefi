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

WORK="$(mktemp -d)"
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

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

ensure_clean_driverstore_base() {
  local existing_listing

  existing_listing="$(wimlib-imagex dir "$ROOT/esp/sources/boot.wim" 1 2>/dev/null \
    | tr '[:upper:]' '[:lower:]' || true)"
  if grep -Eq '/windows/system32/driverstore/filerepository/b2lufs\.inf_' \
    <<<"$existing_listing"; then
    echo
    echo "==> Onceki B2LUFS DriverStore paketi bulundu; WIM temiz ISO tabanindan yenileniyor"
    bash "$ROOT/tools_winboot.sh" "$ISO" --wim-yenile
  fi
}

install_b2lufs_driverstore() {
  local appdata_b64 request_id ready_text expected_size actual_size
  local driverstore_listing driverstore_listing_lower
  local package_file driverstore_dir current_package
  local -a driverstore_dirs

  appdata_b64="$(powershell.exe -NoProfile -NonInteractive -Command \
    '[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([Environment]::GetFolderPath("LocalApplicationData")))' \
    2>/dev/null | tr -d '\r\n' || true)"
  if [[ -z "$appdata_b64" || ! "$appdata_b64" =~ ^[A-Za-z0-9+/]+={0,2}$ ]]; then
    echo "HATA: Windows LocalAppData yolu PowerShell'den okunamadi." >&2
    return 1
  fi
  DISM_STAGE_WIN="$(printf '%s' "$appdata_b64" | base64 --decode 2>/dev/null || true)\\B2L-UFS-DISM"
  DISM_STAGE_WSL="$(wslpath -u "$DISM_STAGE_WIN" 2>/dev/null || true)"
  if [[ -z "$DISM_STAGE_WSL" ]]; then
    echo "HATA: Windows DISM sahne yolu WSL'ye cevrilemedi: $DISM_STAGE_WIN" >&2
    return 1
  fi

  mkdir -p "$DISM_STAGE_WSL"
  rm -f "$DISM_STAGE_WSL/ready.txt"
  cp "$ROOT/esp/sources/boot.wim" "$DISM_STAGE_WSL/boot.wim"
  for package_file in b2lufs.inf b2lufs.sys b2lufs.cat; do
    [[ -s "$OUT/b2lufs/$package_file" ]] || {
      echo "HATA: DISM icin B2LUFS paket dosyasi yok: $OUT/b2lufs/$package_file" >&2
      return 1
    }
    cp "$OUT/b2lufs/$package_file" "$DISM_STAGE_WSL/$package_file"
  done
  sed 's/\r*$/\r/' "$ROOT/windows/B2L_UFS_WIM_SURUCU_EKLE.cmd" \
    >"$DISM_STAGE_WSL/B2L_UFS_WIM_SURUCU_EKLE.cmd"

  request_id="$(printf '%s\n' \
    "$(sha256sum "$DISM_STAGE_WSL/boot.wim")" \
    "$(sha256sum "$DISM_STAGE_WSL/b2lufs.inf")" \
    "$(sha256sum "$DISM_STAGE_WSL/b2lufs.sys")" \
    "$(sha256sum "$DISM_STAGE_WSL/b2lufs.cat")" | sha256sum | awk '{print $1}')"
  {
    echo 'B2L_DISM_STAGE_VERSION=1'
    echo "B2L_DISM_REQUEST_ID=$request_id"
  } >"$DISM_STAGE_WSL/request.txt"

  echo
  echo "==> B2LUFS Windows DISM ile offline DriverStore'a kurulacak"
  echo "  klasor : $DISM_STAGE_WIN"
  echo "  Yonetici Komut Istemi'nde:"
  echo "    cd /d \"$DISM_STAGE_WIN\""
  echo "    B2L_UFS_WIM_SURUCU_EKLE.cmd"
  echo
  if [[ -t 0 ]]; then
    read -r -p "  Betik BASARILI dedikten sonra buraya donup ENTER'a basin... "
  else
    echo "HATA: Etkilesimli terminal yok; DISM betigini calistirip tekrar deneyin." >&2
    return 1
  fi

  [[ -s "$DISM_STAGE_WSL/ready.txt" && -s "$DISM_STAGE_WSL/boot.wim" ]] || {
    echo "HATA: DISM basari isareti veya servis edilmis boot.wim bulunamadi." >&2
    return 1
  }
  ready_text="$(tr -d '\r' <"$DISM_STAGE_WSL/ready.txt")"
  grep -Fxq 'B2L_DISM_STAGE_VERSION=1' <<<"$ready_text" || {
    echo "HATA: DISM basari isareti surumu gecersiz." >&2
    return 1
  }
  grep -Fxq "B2L_DISM_REQUEST_ID=$request_id" <<<"$ready_text" || {
    echo "HATA: DISM sonucu bu WIM/surucu paketi icin uretilmemis." >&2
    return 1
  }
  expected_size="$(sed -n 's/^B2L_DISM_WIM_SIZE=//p' <<<"$ready_text")"
  actual_size="$(stat -c%s "$DISM_STAGE_WSL/boot.wim")"
  [[ "$expected_size" =~ ^[0-9]+$ && "$expected_size" == "$actual_size" ]] || {
    echo "HATA: DISM sonrasi WIM boyutu basari isaretiyle eslesmiyor." >&2
    return 1
  }
  wimlib-imagex verify "$DISM_STAGE_WSL/boot.wim" >/dev/null || {
    echo "HATA: DISM sonrasi boot.wim butunluk dogrulamasindan gecemedi." >&2
    return 1
  }
  driverstore_listing="$(wimlib-imagex dir "$DISM_STAGE_WSL/boot.wim" 1 2>/dev/null)"
  driverstore_listing_lower="$(tr '[:upper:]' '[:lower:]' <<<"$driverstore_listing")"
  for package_file in inf sys cat; do
    grep -Eq "/windows/system32/driverstore/filerepository/b2lufs\.inf_[^/]+/b2lufs\.$package_file$" \
      <<<"$driverstore_listing_lower" || {
      echo "HATA: servis edilmis WIM DriverStore'unda b2lufs.$package_file yok." >&2
      return 1
    }
  done

  mapfile -t driverstore_dirs < <(
    awk 'tolower($0) ~ /^\/windows\/system32\/driverstore\/filerepository\/b2lufs\.inf_[^/]+\/b2lufs\.inf$/ {
      sub(/\/[^/]+$/, ""); print
    }' <<<"$driverstore_listing"
  )
  current_package=0
  for driverstore_dir in "${driverstore_dirs[@]}"; do
    rm -rf "$WORK/driverstore-check"
    mkdir -p "$WORK/driverstore-check"
    if ! wimlib-imagex extract "$DISM_STAGE_WSL/boot.wim" 1 \
      "$driverstore_dir/b2lufs.inf" \
      "$driverstore_dir/b2lufs.sys" \
      "$driverstore_dir/b2lufs.cat" \
      --dest-dir="$WORK/driverstore-check" --no-acls >/dev/null 2>&1; then
      continue
    fi
    current_package=1
    for package_file in b2lufs.inf b2lufs.sys b2lufs.cat; do
      cmp -s "$DISM_STAGE_WSL/$package_file" "$WORK/driverstore-check/$package_file" || {
        current_package=0
        break
      }
    done
    (( current_package )) && break
  done
  (( current_package )) || {
    echo "HATA: DriverStore'daki B2LUFS paketi bu derlemenin INF/SYS/CAT dosyalariyla eslesmiyor." >&2
    echo "boot.wim'i --wim-yenile ile temizleyip DISM adimini yeniden calistirin." >&2
    return 1
  }

  cp "$DISM_STAGE_WSL/boot.wim" "$ROOT/esp/sources/boot.wim"
  cmp -s "$DISM_STAGE_WSL/boot.wim" "$ROOT/esp/sources/boot.wim" || {
    echo "HATA: DISM ile servis edilmis WIM repoya geri kopyalanamadi." >&2
    return 1
  }
  echo "  DriverStore dogrulandi: B2LUFS INF/SYS/CAT offline imaja kuruldu"
}

echo "==> 1/7 Windows ARM64 WinPE dosyalari hazirlaniyor"
bash "$ROOT/tools_winboot.sh" "$ISO"
ensure_clean_driverstore_base

echo "==> 2/7 Var olan PowerShell SYS ile B2LUFS paketi uretiliyor"
B2L_SKIP_MSVC_BUILD=1 bash "$ROOT/tools_b2lufs.sh"

install_b2lufs_driverstore

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
