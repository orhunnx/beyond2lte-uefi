#!/usr/bin/env bash
# B2LUFS'u WinPE WIM'ine boot-critical olarak yerlestirir ve dogrular.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$ROOT/esp/sources/boot.wim"
SYS="$ROOT/out/b2lufs/b2lufs.sys"
REG="$ROOT/windows/b2lufs/b2lufs-boot.reg"
CHECK_ONLY=0

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Kullanim: bash tools_ufsboot_prepare.sh [--check]

  (bayraksiz)  boot.wim icine b2lufs.sys ve boot-critical servis kayitlarini ekler
  --check      mevcut boot.wim icindeki SYS ve offline SYSTEM hive kayitlarini dogrular

Bu arac SYS derlemez. b2lufs.sys yalniz Windows PowerShell'de derlenir.
EOF
  exit 0
elif [[ "${1:-}" == "--check" ]]; then
  CHECK_ONLY=1
elif [[ $# -ne 0 ]]; then
  echo "Kullanim: bash tools_ufsboot_prepare.sh [--check]" >&2
  exit 2
fi

for tool in wimlib-imagex hivexget sha256sum; do
  command -v "$tool" >/dev/null || {
    echo "HATA: $tool yok. WSL: sudo apt install -y wimtools libhivex-bin" >&2
    exit 1
  }
done
if (( ! CHECK_ONLY )); then
  for tool in hivexregedit hivexsh; do
    command -v "$tool" >/dev/null || {
      echo "HATA: $tool yok. WSL: sudo apt install -y libhivex-bin libwin-hivex-perl" >&2
      exit 1
    }
  done
fi

[[ -s "$WIM" ]] || { echo "HATA: $WIM yok; once tools_winboot.sh calistir." >&2; exit 1; }
[[ -s "$SYS" ]] || {
  echo "HATA: $SYS yok. Surucuyu yalniz PowerShell'de derle:" >&2
  echo 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"' >&2
  exit 1
}
[[ -s "$REG" ]] || { echo "HATA: $REG yok." >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

extract_for_check() {
  rm -rf "$WORK/check"
  mkdir -p "$WORK/check"
  wimlib-imagex extract "$WIM" 1 \
    /Windows/System32/config/SYSTEM \
    /Windows/System32/drivers/b2lufs.sys \
    /Windows/B2LUFS/boot-critical.txt \
    --dest-dir="$WORK/check" --no-acls >/dev/null
}

check_hive_value() {
  local hive="$1" key="$2" value="$3" pattern="$4"
  local got
  got="$(hivexget "$hive" "$key" "$value" 2>/dev/null | tr -d '\r\000' || true)"
  if ! grep -Eiq "$pattern" <<<"$got"; then
    echo "HATA: hive dogrulamasi: $key/$value beklenen '$pattern', gelen '$got'" >&2
    return 1
  fi
}

ensure_hive_child_key() {
  local hive="$1" parent="$2" child="$3"
  local children

  children="$(printf 'cd \\%s\nls\n' "$parent" \
    | hivexsh "$hive" 2>/dev/null | tr -d '\r' || true)"
  if grep -Fqx "$child" <<<"$children"; then
    return 0
  fi

  # hivexregedit hedef anahtari olusturabilir, ancak eksik bir ara ebeveyni
  # olusturamaz. Yeni Windows 11 WinPE hive'larinda CriticalDeviceDatabase
  # bulunmadigi icin onu merge'den once acikca ekle. hivexsh -w tek basina
  # yeterli degildir; commit olmazsa add kabuk kapanirken geri alinir.
  printf 'cd \\%s\nadd %s\ncommit\n' "$parent" "$child" \
    | hivexsh -w "$hive" >/dev/null

  children="$(printf 'cd \\%s\nls\n' "$parent" \
    | hivexsh "$hive" 2>/dev/null | tr -d '\r' || true)"
  grep -Fqx "$child" <<<"$children" || {
    echo "HATA: hive anahtari olusturulamadi: $parent\\$child" >&2
    return 1
  }
}

verify() {
  extract_for_check
  # wimlib tekil dosya cikarmalarinda WIM icindeki ust dizinleri korumaz;
  # dosyalari dogrudan --dest-dir altina kendi adlariyla yerlestirir.
  local image_sys="$WORK/check/b2lufs.sys"
  local hive="$WORK/check/SYSTEM"
  local marker="$WORK/check/boot-critical.txt"
  local expected actual
  expected="$(sha256sum "$SYS" | awk '{print $1}')"
  actual="$(sha256sum "$image_sys" | awk '{print $1}')"
  [[ "$expected" == "$actual" ]] || {
    echo "HATA: WIM icindeki b2lufs.sys guncel degil." >&2
    return 1
  }
  grep -Fq "$expected" "$marker" || {
    echo "HATA: boot-critical isareti SYS karmasiyla eslesmiyor." >&2
    return 1
  }

  local cs
  for cs in ControlSet001 ControlSet002; do
    check_hive_value "$hive" "$cs\\Services\\b2lufs" Type '0x1|dword:00000001|^1$'
    check_hive_value "$hive" "$cs\\Services\\b2lufs" Start '0x0|dword:00000000|^0$'
    check_hive_value "$hive" "$cs\\Services\\b2lufs" ErrorControl '0x1|dword:00000001|^1$'
    check_hive_value "$hive" "$cs\\Services\\b2lufs" Group 'SCSI Miniport'
    check_hive_value "$hive" "$cs\\Services\\b2lufs" ImagePath 'SystemRoot.*System32.*drivers.*b2lufs\.sys'
    check_hive_value "$hive" "$cs\\Control\\CriticalDeviceDatabase\\ACPI#BYND9820" Service 'b2lufs'
    check_hive_value "$hive" "$cs\\Control\\CriticalDeviceDatabase\\ACPI#BYND9820" ClassGUID '4d36e97b-e325-11ce-bfc1-08002be10318'
  done
  echo "B2LUFS boot-critical WIM dogrulamasi BASARILI: $expected"
}

if (( CHECK_ONLY )); then
  verify
  exit 0
fi

echo "==> WIM SYSTEM hive cikariliyor"
mkdir -p "$WORK/edit"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$WORK/edit" --no-acls >/dev/null
HIVE="$WORK/edit/SYSTEM"

echo "==> B2LUFS boot-critical servis ve CriticalDeviceDatabase kayitlari ekleniyor"
for cs in ControlSet001 ControlSet002; do
  ensure_hive_child_key "$HIVE" "$cs\\Control" CriticalDeviceDatabase
done
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$HIVE" "$REG"

HASH="$(sha256sum "$SYS" | awk '{print $1}')"
MARKER="$WORK/boot-critical.txt"
printf 'B2LUFS_BOOT_CRITICAL=1\nB2LUFS_SYS_SHA256=%s\n' "$HASH" >"$MARKER"

CMDS="$WORK/wim-update.txt"
printf '%s\n' \
  'delete --force /Windows/System32/config/SYSTEM' \
  "add '$HIVE' /Windows/System32/config/SYSTEM" \
  'delete --force /Windows/System32/drivers/b2lufs.sys' \
  "add '$SYS' /Windows/System32/drivers/b2lufs.sys" \
  'delete --force /Windows/B2LUFS/boot-critical.txt' \
  "add '$MARKER' /Windows/B2LUFS/boot-critical.txt" >"$CMDS"

echo "==> WIM guncelleniyor"
wimlib-imagex update "$WIM" 1 <"$CMDS" >/dev/null
wimlib-imagex optimize "$WIM" >/dev/null

verify
