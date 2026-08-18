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
PARTITION_GEOMETRY="$ROOT/tools_partition_geometry.sh"
PIT="$ROOT/BEYOND2LTE_EUR_OPEN.pit"
UFS_INF="$ROOT/windows/b2lufs/b2lufs.inf.in"
UFS_DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"
UFS_STORPORT="$ROOT/windows/b2lufs/b2lufs_storport.c"
UFS_TRANSPORT="$ROOT/windows/b2lufs/b2lufs_transport.c"
UFS_FLIGHT="$ROOT/windows/b2lufs/b2lufs_flight.c"
UFS_FLIGHT_HEADER="$ROOT/windows/b2lufs/b2lufs_flight.h"
UFS_BUILD="$ROOT/windows/b2lufs/build-b2lufs.ps1"
UFS_GOSTER="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LGoster/B2LGoster.c"
UFS_GOSTER_INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LGoster/B2LGoster.inf"
UFS_DEBUG_STARTUP="$ROOT/esp/startup-ufs-debug.nsh"

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
grep -Fq 'STALE_SYS_SOURCES=' "$ORCHESTRATOR"
grep -Fq 'B2LUFS kaynaklarindan eski; disk-boot paketi uretilmedi' "$ORCHESTRATOR"
grep -Fq 'B2L_BCD_STAGE_VERSION=2' "$ORCHESTRATOR"
grep -Fq 'B2L_BCD_VARIANT=disk' "$ORCHESTRATOR"
grep -Fq 'B2L_BCD_REQUEST_ID=' "$ORCHESTRATOR"
grep -Fq 'echo "B2L_BCD_REQUEST_ID=$request_id"' "$ORCHESTRATOR"
grep -Fq 'sha256sum "$BCD_MANUAL_DIR/bcd_olustur.cmd"' "$ORCHESTRATOR"
grep -Fq 'sha256sum "$BCD_MANUAL_DIR/B2L_BCD_DISK_OLUSTUR.cmd"' "$ORCHESTRATOR"
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
grep -Fq 'B2L_BCD_STAGE_VERSION=2' "$BCD_LAUNCHER"
grep -Fq 'B2L_BCD_VARIANT=disk' "$BCD_LAUNCHER"
grep -Fq 'B2L_BCD_REQUEST_ID=' "$BCD_LAUNCHER"
grep -Fq 'if not defined DEVICE_OK' "$BCD_LAUNCHER"
grep -Fq 'if not defined OSDEVICE_OK' "$BCD_LAUNCHER"
grep -Fq 'testsigning        Yes' "$BCD_BUILDER"
grep -Fq 'device   locate=\Windows\System32\Boot\winload.efi' "$BCD_BUILDER"
grep -Fq 'osdevice locate=\Windows' "$BCD_BUILDER"
if grep -Fq 'osdevice locate=\Windows\System32\Boot\winload.efi' "$BCD_BUILDER"; then
  echo "HATA: osdevice yine yukleyici dosyasini sistem koku saniyor." >&2
  exit 1
fi
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
stale_gate_line="$(grep -n '^STALE_SYS_SOURCES=' "$ORCHESTRATOR" | cut -d: -f1)"
[[ -n "$bcd_call_line" && -n "$step_one_line" && "$bcd_call_line" -lt "$step_one_line" ]] || {
  echo "HATA: manuel BCD kapisi pahali WIM adimlarindan once degil." >&2
  exit 1
}
[[ -n "$stale_gate_line" && "$stale_gate_line" -lt "$bcd_call_line" ]] || {
  echo "HATA: bayat SYS kapisi BCD/ISO adimlarindan once degil." >&2
  exit 1
}

# Boot-start servis kaydi tek basina yeterli degildir. PnP paketinin DISM ile
# offline DriverStore/DriverDatabase'a kurulmasi, hive duzenlemesinden once
# bitmis ve servis edilen WIM geri okunduktan sonra dogrulanmis olmalidir.
grep -Fq '[Environment]::GetFolderPath("LocalApplicationData")' "$ORCHESTRATOR"
grep -Fq 'B2L_DISM_STAGE_VERSION=2' "$ORCHESTRATOR"
grep -Fq 'B2L_DISM_REQUEST_ID=' "$ORCHESTRATOR"
grep -Fq 'sha256sum "$DISM_STAGE_WSL/B2L_UFS_WIM_SURUCU_EKLE.cmd"' "$ORCHESTRATOR"
fresh_wim_call_line="$(grep -n '^bash "$ROOT/tools_winboot.sh" "$ISO" --wim-yenile$' "$ORCHESTRATOR" | cut -d: -f1)"
[[ -n "$fresh_wim_call_line" && "$fresh_wim_call_line" -gt "$step_one_line" ]] || {
  echo "HATA: UFS disk-boot her tur temiz ISO WIM tabanindan baslamiyor." >&2
  exit 1
}
if grep -Fq 'ensure_clean_driverstore_base' "$ORCHESTRATOR"; then
  echo "HATA: temiz WIM hala onceki B2LUFS paketinin gorulmesine bagli." >&2
  exit 1
fi
grep -Fq 'wimlib-imagex verify "$DISM_STAGE_WSL/boot.wim"' "$ORCHESTRATOR"
grep -Fq 'driverstore/filerepository/b2lufs\.inf_' "$ORCHESTRATOR"
grep -Fq 'cmp -s "$DISM_STAGE_WSL/$package_file" "$WORK/driverstore-check/$package_file"' "$ORCHESTRATOR"
grep -Fq 'fltmc >nul 2>&1' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Mount-Image' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Image:"%MOUNT%" /Add-Driver /Driver:"%INF%" /ForceUnsigned' "$DISM_INSTALLER"
grep -Fq 'dism.exe /Unmount-Image /MountDir:"%MOUNT%" /Commit' "$DISM_INSTALLER"
grep -Fq "where.exe /r \"%REPOSITORY%\" b2lufs.inf 2^>nul" "$DISM_INSTALLER"
if grep -Fq 'for /r "%MOUNT%\Windows\System32\DriverStore\FileRepository"' "$DISM_INSTALLER"; then
  echo "HATA: var olmayan hayali INF yolunu secebilen sabit FOR /R geri gelmis." >&2
  exit 1
fi

dism_call_line="$(grep -n '^install_b2lufs_driverstore$' "$ORCHESTRATOR" | cut -d: -f1)"
prepare_call_line="$(grep -n 'bash "$ROOT/tools_ufsboot_prepare.sh"' "$ORCHESTRATOR" | cut -d: -f1)"
[[ -n "$dism_call_line" && -n "$prepare_call_line" && "$dism_call_line" -lt "$prepare_call_line" ]] || {
  echo "HATA: DISM DriverStore kurulumu boot-critical hive adimindan once degil." >&2
  exit 1
}
grep -Fq "WIM DriverStore'unda b2lufs.\$package_ext yok" "$PREPARE"

# UFS disk-boot zinciri: boot-critical WIM -> NTFS SYSTEM ve yalniz EFI/BCD
# tasiyan CACHE. Bu kapilar gevserse paket uretilse bile cihaz UFS'ten acilmaz.
[[ -s "$PARTITION_GEOMETRY" ]] || {
  echo "HATA: PIT/BPB geometri yardimcisi yok." >&2
  exit 1
}
# shellcheck source=../tools_partition_geometry.sh
source "$PARTITION_GEOMETRY"
read -r system_start_lba system_block_count < <(
  b2l_pit_partition_geometry "$PIT" SYSTEM
)
read -r cache_start_lba cache_block_count < <(
  b2l_pit_partition_geometry "$PIT" CACHE
)
[[ "$system_start_lba" == 83968 && "$system_block_count" == 1459200 ]] || {
  echo "HATA: PIT SYSTEM geometrisi yanlis: $system_start_lba $system_block_count" >&2
  exit 1
}
[[ "$cache_start_lba" == 1983488 && "$cache_block_count" == 153600 ]] || {
  echo "HATA: PIT CACHE geometrisi yanlis: $cache_start_lba $cache_block_count" >&2
  exit 1
}
bpb_fixture="$(mktemp)"
dd if=/dev/zero of="$bpb_fixture" bs=64 count=1 2>/dev/null
printf '\x00\x10' | dd of="$bpb_fixture" bs=1 seek=11 conv=notrunc 2>/dev/null
printf '\x01' | dd of="$bpb_fixture" bs=1 seek=13 conv=notrunc 2>/dev/null
printf '\x3f\x00' | dd of="$bpb_fixture" bs=1 seek=24 conv=notrunc 2>/dev/null
printf '\xff\x00' | dd of="$bpb_fixture" bs=1 seek=26 conv=notrunc 2>/dev/null
printf '\x00\x48\x01\x00' | dd of="$bpb_fixture" bs=1 seek=28 conv=notrunc 2>/dev/null
b2l_validate_bpb_geometry "$bpb_fixture" 83968 1 "test SYSTEM" || {
  rm -f "$bpb_fixture"
  echo "HATA: dogru BPB geometrisi reddedildi." >&2
  exit 1
}
if b2l_validate_bpb_geometry "$bpb_fixture" 1983488 1 "test yanlis CACHE" \
    >/dev/null 2>&1; then
  rm -f "$bpb_fixture"
  echo "HATA: yanlis BPB baslangic LBA'si kabul edildi." >&2
  exit 1
fi
rm -f "$bpb_fixture"
grep -Fq 'bash "$HERE/tools_ufsboot_prepare.sh" --check' "$WINDISK"
grep -Fq 'SYSTEM imajinda NTFS BPB imzasi yok' "$WINDISK"
grep -Fq 'b2l_pit_partition_geometry "$PIT" SYSTEM' "$WINDISK"
grep -Fq -- '-H "$B2L_BPB_HEADS"' "$WINDISK"
grep -Fq -- '-S "$B2L_BPB_SECTORS_PER_TRACK"' "$WINDISK"
grep -Fq -- '-p "$SYSTEM_START_LBA"' "$WINDISK"
grep -Fq 'b2l_validate_bpb_geometry "$IMG" "$SYSTEM_START_LBA"' "$WINDISK"
grep -Fq 'SYSTEM NTFS boot geometrisi dogrulandi' "$WINDISK"
grep -Fq 'b2lufs.sys (boot-critical)' "$WINDISK"
grep -Fq 'b2l_pit_partition_geometry "$PIT" "$PARTITION_NAME"' "$CACHE_ESP"
grep -Fq -- '-g "$B2L_BPB_HEADS/$B2L_BPB_SECTORS_PER_TRACK"' "$CACHE_ESP"
grep -Fq -- '-h "$PARTITION_START_LBA"' "$CACHE_ESP"
grep -Fq 'b2l_validate_bpb_geometry "$IMG" "$PARTITION_START_LBA"' "$CACHE_ESP"
if grep -Fq 'grep -vi "not a block device' "$WINDISK"; then
  echo "HATA: mkntfs boot uyarilarini gizleyen filtre geri gelmis." >&2
  exit 1
fi
if grep -Eq 'ntfsls .*2>/dev/null' "$WINDISK"; then
  echo "HATA: NTFS icerik dogrulama hatalarini gizleyen yonlendirme geri gelmis." >&2
  exit 1
fi
if grep -Eq 'mkfs\.vfat .*([12]?> */dev/null|&> */dev/null)' "$CACHE_ESP"; then
  echo "HATA: mkfs.vfat ciktisini gizleyen yonlendirme geri gelmis." >&2
  exit 1
fi
grep -Fq 'rm -f "$ESP/sources/boot.wim" "$ESP/boot/boot.sdi"' "$WINBOOT"
grep -Fq '::/EFI/Microsoft/Boot/BCD' "$CACHE_ESP"
grep -Fq 'disk-boot CACHE imajina sources/ sizmis' "$CACHE_ESP"
grep -Fq 'ACPI\BYND9820' "$UFS_INF"
grep -Fq 'Name (_CID, "BYND9820")' "$UFS_DSDT"
grep -Fq 'CriticalDeviceDatabase\ACPI#BYND9820' "$REG"

# Microsoft boot-drive StorPort sözleşmesi: boot aygıtı HwStorInitialize
# dönmeden erişilebilir olmalıdır. Kapasite/GPT problarını PASSIVE callback'e
# bırakmak ilk taramayı NO_DEVICE ile kapatır ve INACCESSIBLE_BOOT_DEVICE
# üretir. PASSIVE yol yalnız telemetri ve isteğe bağlı yeniden tarama içindir.
initialize_function="$(awk '
  /^B2LUfsInitialize\(/ { in_function=1 }
  in_function { print }
  in_function && /^}/ { exit }
' "$UFS_STORPORT")"
passive_function="$(awk '
  /^B2LUfsPassiveInitialize\(/ { in_function=1 }
  in_function { print }
  in_function && /^}/ { exit }
' "$UFS_STORPORT")"
for synchronous_probe in \
  B2LUfsProbeLun0Capacity \
  B2LUfsProbeLuns \
  B2LUfsProbeMediaHeaders; do
  grep -Fq "$synchronous_probe(DeviceExtension, Adapter);" \
    <<<"$initialize_function" || {
    echo "HATA: boot-kritik $synchronous_probe HwInitialize icinde degil." >&2
    exit 1
  }
  if grep -Fq "$synchronous_probe(DeviceExtension, Adapter);" \
      <<<"$passive_function"; then
    echo "HATA: boot-kritik $synchronous_probe hala PASSIVE init'e bagli." >&2
    exit 1
  fi
done
grep -Fq 'if (!Adapter->EnumerationReady)' <<<"$initialize_function"
grep -Fq 'Adapter->MediaWindowsSystemEntryFound;' <<<"$initialize_function"
grep -Fq '(void)StorPortEnablePassiveInitialization(' <<<"$initialize_function"
transport_start_line="$(grep -n 'B2LUfsStartTransport' <<<"$initialize_function" | head -n1 | cut -d: -f1)"
capacity_probe_line="$(grep -n 'B2LUfsProbeLun0Capacity' <<<"$initialize_function" | cut -d: -f1)"
media_probe_line="$(grep -n 'B2LUfsProbeMediaHeaders' <<<"$initialize_function" | cut -d: -f1)"
ready_gate_line="$(grep -n 'if (!Adapter->EnumerationReady)' <<<"$initialize_function" | cut -d: -f1)"
passive_enable_line="$(grep -n 'StorPortEnablePassiveInitialization' <<<"$initialize_function" | cut -d: -f1)"
[[ "$transport_start_line" -lt "$capacity_probe_line" &&
   "$capacity_probe_line" -lt "$media_probe_line" &&
   "$media_probe_line" -lt "$ready_gate_line" &&
   "$ready_gate_line" -lt "$passive_enable_line" ]] || {
  echo "HATA: HwInitialize boot-medya hazirlik sirasi bozuldu." >&2
  exit 1
}

# INACCESSIBLE_BOOT_DEVICE, dosya sistemi baglanmadan once oldugu icin ETW,
# registry veya X: uzerindeki kayitlar bu turda geri alinamaz. B2LUFS erken
# yasam dongusunu firmware'in ayirdigi, sicak reset boyunca kalan RAM'e yazar;
# sonraki UEFI acilisi ayni ABI'yi buyuk puntolu ekranda yorumlar.
[[ -s "$UFS_FLIGHT" && -s "$UFS_FLIGHT_HEADER" ]] || {
  echo "HATA: B2LUFS sicak-reset ucus kaydi kaynaklari yok." >&2
  exit 1
}
grep -Fq '#define B2LUFS_FLIGHT_PHYSICAL_BASE' "$UFS_FLIGHT_HEADER"
grep -Fq 'UINT64_C(0xCF050000)' "$UFS_FLIGHT_HEADER"
grep -Fq '#define B2LUFS_FLIGHT_BYTES' "$UFS_FLIGHT_HEADER"
grep -Fq 'UINT32_C(0x00010000)' "$UFS_FLIGHT_HEADER"
grep -Fq 'B2LUfsFlightInitialize' "$UFS_FLIGHT"
grep -Fq 'MmMapIoSpace' "$UFS_FLIGHT"
grep -Fq 'InterlockedCompareExchange(&B2LUfsFlightLock' "$UFS_FLIGHT"
if grep -Fq 'InterlockedIncrement(&B2LUfsFlightHeader' "$UFS_FLIGHT"; then
  echo "HATA: MmNonCached ucus kaydinda yasak Interlocked islemi kullaniliyor." >&2
  exit 1
fi
grep -Fq 'B2LUfsFlightLog' "$UFS_STORPORT"
grep -Fq 'B2LUfsFlightLog' "$UFS_TRANSPORT"
grep -Fq "'b2lufs_flight.c'" "$UFS_BUILD"
grep -Fq 'b2lufs_flight.obj' "$UFS_BUILD"
grep -Fq 'B2LUFS UCUS KAYDI' "$UFS_GOSTER"
grep -Fq 'B2LUFS_FLIGHT_PHYSICAL_BASE' "$UFS_GOSTER"
grep -Fq 'InvalidateDataCacheRange ((VOID *)Header, B2LUFS_FLIGHT_BYTES)' "$UFS_GOSTER"
grep -Fq 'CacheMaintenanceLib' "$UFS_GOSTER_INF"
[[ -s "$UFS_DEBUG_STARTUP" ]] || {
  echo "HATA: UFS disk-boot tani startup betigi yok." >&2
  exit 1
}
grep -Fq -- '--debug' "$ORCHESTRATOR"
grep -Fq -- '--ufs-debug' "$ORCHESTRATOR"
grep -Fq 'export B2L_BUGCHECK_AUTOREBOOT="$DEBUG_MODE"' "$ORCHESTRATOR"
grep -Fq 'BUGCHECK_AUTOREBOOT="${B2L_BUGCHECK_AUTOREBOOT:-0}"' "$WINBOOT"
grep -Fq 'DOGRULANDI: bugcheck sicak yeniden baslatacak' "$WINBOOT"
grep -Fq -- '--ufs-debug' "$CACHE_ESP"
grep -Fq 'B2LGOSTR.EFI' "$UFS_DEBUG_STARTUP"
grep -Fq 'bootmgfw.efi' "$UFS_DEBUG_STARTUP"
grep -Fq 'DriverVer=08/18/2026,0.0.8.0' "$UFS_INF"

python3 "$ROOT/tests/test_b2lufs_flight_decode.py"

unicode_desktop_b64='QzpcVXNlcnNcb3JodW5cT25lRHJpdmVcTWFzYcO8c3TDvA=='
unicode_desktop="$(printf '%s' "$unicode_desktop_b64" | base64 --decode)"
[[ "$unicode_desktop" == 'C:\Users\orhun\OneDrive\Masaüstü' ]] || {
  echo "HATA: Unicode Windows Masaustu yolu UTF-8 olarak geri acilamadi." >&2
  exit 1
}

echo "UFS boot script regresyon testleri BASARILI."
