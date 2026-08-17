#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREPARE="$ROOT/tools_ufsboot_prepare.sh"
REG="$ROOT/windows/b2lufs/b2lufs-boot.reg"
ORCHESTRATOR="$ROOT/tools_ufsboot.sh"
BCD_LAUNCHER="$ROOT/windows/B2L_BCD_DISK_OLUSTUR.cmd"
BCD_BUILDER="$ROOT/windows/bcd_olustur.cmd"
DISM_INSTALLER="$ROOT/windows/B2L_UFS_WIM_SURUCU_EKLE.cmd"
WINDISK="$ROOT/tools_windisk.sh"
WINBOOT="$ROOT/tools_winboot.sh"
CACHE_ESP="$ROOT/cache_esp.sh"
UFS_INF="$ROOT/windows/b2lufs/b2lufs.inf.in"
UFS_DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"

for script in \
  "$ROOT/tools_ufsboot.sh" \
  "$ROOT/tools_winboot.sh" \
  "$ROOT/tools_b2lufs.sh" \
  "$ROOT/tools_ufsboot_prepare.sh" \
  "$ROOT/tools_windisk.sh" \
  "$ROOT/cache_esp.sh"; do
  bash -n "$script"
done

template_sets="$(grep -oE 'ControlSet[0-9]{3}' "$REG" | sort -u)"
[[ "$template_sets" == "ControlSet001" ]] || {
  echo "HATA: registry dosyasi tek ControlSet001 sablonu degil." >&2
  exit 1
}

for control_set in ControlSet001 ControlSet002 ControlSet007; do
  rendered="$(sed "s/ControlSet001/$control_set/g" "$REG")"
  rendered_sets="$(grep -oE 'ControlSet[0-9]{3}' <<<"$rendered" | sort -u)"
  [[ "$rendered_sets" == "$control_set" ]] || {
    echo "HATA: registry sablonu $control_set icin dogru uretilmedi." >&2
    exit 1
  }
done

list_function="$(awk '
  /^list_control_sets\(\) \{/ { in_function=1 }
  in_function { print }
  in_function && /^}/ { exit }
' "$PREPARE")"
eval "$list_function"
hivexsh() {
  printf '%s\n' Select ControlSet007 MountedDevices ControlSet001
}
discovered="$(list_control_sets unused-hive)"
[[ "$discovered" == $'ControlSet001\nControlSet007' ]] || {
  echo "HATA: hive control-set kesfi beklenen sonucu vermedi." >&2
  exit 1
}

grep -Fq "printf 'ls\\n' | hivexsh" "$PREPARE"
grep -Fq "add %s\\ncommit\\n" "$PREPARE"
if grep -Fq 'for cs in ControlSet001 ControlSet002' "$PREPARE"; then
  echo "HATA: sabit control-set varsayimi geri gelmis." >&2
  exit 1
fi

grep -Fq '[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([Environment]::GetFolderPath("Desktop")))' "$ORCHESTRATOR"
grep -Fq 'base64 --decode' "$ORCHESTRATOR"
grep -Fq 'B2L-UFS-BCD' "$ORCHESTRATOR"
grep -Fq 'B2L_BCD_STAGE_VERSION=1' "$ORCHESTRATOR"
grep -Fq 'B2L_BCD_VARIANT=disk' "$ORCHESTRATOR"
grep -Fq 'cmp -s "$BCD_MANUAL_OUT" "$ROOT/windows/BCD"' "$ORCHESTRATOR"
if grep -Fq 'cmd.exe /d /s /c "\"$BCD_SCRIPT_WIN\"' "$ORCHESTRATOR"; then
  echo "HATA: dogrudan UNC BCD betigi cagrisi geri gelmis." >&2
  exit 1
fi
if grep -Fq 'cmd.exe /d /c bcd_olustur.cmd' "$ORCHESTRATOR"; then
  echo "HATA: BCD betigi yonetici yetkisi olmadan cagriliyor." >&2
  exit 1
fi
if grep -Fq 'run_bcd_elevated.ps1' "$ORCHESTRATOR"; then
  echo "HATA: calismayan otomatik UAC BCD yolu geri gelmis." >&2
  exit 1
fi
grep -Fq 'fltmc >nul 2>&1' "$BCD_LAUNCHER"
grep -Fq 'call "%~dp0bcd_olustur.cmd" disk "%BCD%"' "$BCD_LAUNCHER"
grep -Fq 'B2L_BCD_STAGE_VERSION=1' "$BCD_LAUNCHER"
grep -Fq 'B2L_BCD_VARIANT=disk' "$BCD_LAUNCHER"
grep -Fq 'testsigning        Yes' "$BCD_BUILDER"
if grep -Eq '^bcdedit .*nointegritychecks' "$BCD_BUILDER"; then
  echo "HATA: Secure Boot'un reddedebildigi gereksiz nointegritychecks geri gelmis." >&2
  exit 1
fi
if grep -Eq '^bcdedit .*2>&1.*goto :hata' "$BCD_BUILDER"; then
  echo "HATA: olumcul BCDEdit hatasi kullanicidan gizleniyor." >&2
  exit 1
fi

bcd_call_line="$(grep -n '^prepare_disk_bcd$' "$ORCHESTRATOR" | cut -d: -f1)"
step_one_line="$(grep -n '^echo "==> 1/7' "$ORCHESTRATOR" | cut -d: -f1)"
[[ -n "$bcd_call_line" && -n "$step_one_line" && "$bcd_call_line" -lt "$step_one_line" ]] || {
  echo "HATA: manuel BCD kapisi pahali WIM adimlarindan once degil." >&2
  exit 1
}

# Boot-start servis kaydi tek basina yeterli degildir. PnP paketinin DISM ile
# offline DriverStore/DriverDatabase'a kurulmasi, hive duzenlemesinden once
# bitmis ve servis edilen WIM geri okunduktan sonra dogrulanmis olmalidir.
grep -Fq '[Environment]::GetFolderPath("LocalApplicationData")' "$ORCHESTRATOR"
grep -Fq 'B2L_DISM_REQUEST_ID=' "$ORCHESTRATOR"
grep -Fq 'Onceki B2LUFS DriverStore paketi bulundu' "$ORCHESTRATOR"
grep -Fq 'bash "$ROOT/tools_winboot.sh" "$ISO" --wim-yenile' "$ORCHESTRATOR"
grep -Fq 'wimlib-imagex verify "$DISM_STAGE_WSL/boot.wim"' "$ORCHESTRATOR"
grep -Fq 'driverstore/filerepository/b2lufs\.inf_' "$ORCHESTRATOR"
grep -Fq 'cmp -s "$DISM_STAGE_WSL/$package_file" "$WORK/driverstore-check/$package_file"' "$ORCHESTRATOR"
grep -Fq 'fltmc >nul 2>&1' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Mount-Image' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Image:"%MOUNT%" /Add-Driver /Driver:"%INF%" /ForceUnsigned' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Unmount-Image /MountDir:"%MOUNT%" /Commit' "$DISM_INSTALLER"
grep -Fq 'for /r "%MOUNT%\Windows\System32\DriverStore\FileRepository"' "$DISM_INSTALLER"

dism_call_line="$(grep -n '^install_b2lufs_driverstore$' "$ORCHESTRATOR" | cut -d: -f1)"
prepare_call_line="$(grep -n 'bash "$ROOT/tools_ufsboot_prepare.sh"' "$ORCHESTRATOR" | cut -d: -f1)"
[[ -n "$dism_call_line" && -n "$prepare_call_line" && "$dism_call_line" -lt "$prepare_call_line" ]] || {
  echo "HATA: DISM DriverStore kurulumu boot-critical hive adimindan once degil." >&2
  exit 1
}
grep -Fq "WIM DriverStore'unda b2lufs.\$package_ext yok" "$PREPARE"

# UFS disk-boot zinciri: boot-critical WIM -> NTFS SYSTEM ve yalniz EFI/BCD
# tasiyan CACHE. Bu kapilar gevserse paket uretilse bile cihaz UFS'ten acilmaz.
grep -Fq 'bash "$HERE/tools_ufsboot_prepare.sh" --check' "$WINDISK"
grep -Fq 'b2lufs.sys (boot-critical)' "$WINDISK"
grep -Fq 'rm -f "$ESP/sources/boot.wim" "$ESP/boot/boot.sdi"' "$WINBOOT"
grep -Fq '::/EFI/Microsoft/Boot/BCD' "$CACHE_ESP"
grep -Fq 'disk-boot CACHE imajina sources/ sizmis' "$CACHE_ESP"
grep -Fq 'ACPI\BYND9820' "$UFS_INF"
grep -Fq 'Name (_CID, "BYND9820")' "$UFS_DSDT"
grep -Fq 'CriticalDeviceDatabase\ACPI#BYND9820' "$REG"

unicode_desktop_b64='QzpcVXNlcnNcb3JodW5cT25lRHJpdmVcTWFzYcO8c3TDvA=='
unicode_desktop="$(printf '%s' "$unicode_desktop_b64" | base64 --decode)"
[[ "$unicode_desktop" == 'C:\Users\orhun\OneDrive\Masaüstü' ]] || {
  echo "HATA: Unicode Windows Masaustu yolu UTF-8 olarak geri acilamadi." >&2
  exit 1
}

echo "UFS boot script regresyon testleri BASARILI."
