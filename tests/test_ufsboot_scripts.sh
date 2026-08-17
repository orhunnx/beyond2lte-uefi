#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREPARE="$ROOT/tools_ufsboot_prepare.sh"
REG="$ROOT/windows/b2lufs/b2lufs-boot.reg"
ORCHESTRATOR="$ROOT/tools_ufsboot.sh"
BCD_LAUNCHER="$ROOT/windows/B2L_BCD_DISK_OLUSTUR.cmd"
BCD_BUILDER="$ROOT/windows/bcd_olustur.cmd"
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

grep -Fq "'[Environment]::GetFolderPath(\"Desktop\")'" "$ORCHESTRATOR"
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

echo "UFS boot script regresyon testleri BASARILI."
