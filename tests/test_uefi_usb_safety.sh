#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.c"
PROBE="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LDwc3Probe.c"
PROBE_HEADER="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LDwc3Probe.h"
MAX="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LMax77705.c"
INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.inf"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
MAP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.c"

grep -q 'BASE_NAME[[:space:]]*= B2LUsbTest' "$INF"
grep -q 'Applications/B2LUsbTest/B2LUsbTest.inf' "$DSC"
grep -q -- '--usb-test' "$ROOT/yap.sh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup.nsh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup-baseline.nsh"
grep -q 'beyond2lte-usb-diag.tar' "$ROOT/cache_esp.sh"
grep -q 'DWC3_IP_DWC31[[:space:]]*0x33310000U' "$APP"
grep -q 'DWC3_VER_NUMBER[[:space:]]*0xC1A0' "$APP"
grep -q 'B2L_USB_TEST_REVISION[[:space:]]*13' "$PROBE_HEADER"
grep -q 'B2L_USB_PCS_ABORT_OFFSET_0[[:space:]]*0x0000' "$APP"
grep -q 'B2L_USB_PCS_ABORT_OFFSET_C[[:space:]]*0x000C' "$APP"
grep -q 'B2L_USB_PMA_ABORT_OFFSET_0[[:space:]]*0x0000' "$APP"
grep -q 'ShowSkippedComboPcs' "$APP"
grep -q 'ShowSkippedComboPma' "$APP"
grep -q 'Value = Read32 (B2L_USB_PHY_BASE, Offset);' "$APP"
grep -q 'ShowUsbAttachMonitor' "$APP"
grep -q 'CaptureUsbAttachSnapshot' "$APP"
grep -q 'WaitForMonitorTick' "$APP"
grep -q 'gBS->Stall (100000)' "$APP"
grep -q 'Once kablo TAKILI fotograf; sonra CIKAR' "$APP"
grep -q 'DWC3_OSTS_VBUSVLD' "$APP"
grep -q 'XHCI_PORTSC_CSC' "$APP"
grep -q 'B2LDwc3Probe.c' "$INF"
grep -q 'B2LDwc3RunEventProbe' "$APP"
grep -q 'DWC3_DSTS_SOFFN_MASK' "$APP"
grep -q 'Previous->Dsts & ~DWC3_DSTS_SOFFN_MASK' "$APP"
grep -q 'AllocatePages' "$PROBE"
grep -q 'AllocateMaxAddress' "$PROBE"
grep -q 'BufferAddress = MAX_UINT32' "$PROBE"
grep -q 'EfiReservedMemoryType' "$PROBE"
grep -q 'WriteBackInvalidateDataCacheRange' "$PROBE"
grep -q 'DWC3_DALEPENA' "$PROBE"
grep -q 'DWC3_DEPCMD_CMDACT' "$PROBE"
grep -q 'FailStoppedWithReservedDma' "$PROBE"
grep -q 'DWC3_STOP_TIMEOUT_MS[[:space:]]*2000' "$PROBE"
grep -q 'DWC3_EVENT_BUFFER_SIZE & DWC3_GEVNTSIZ_SIZE_MASK' "$PROBE"
grep -q 'DWC3_DCTL_ULSTCHNGREQ_MASK' "$PROBE"
grep -q 'DWC3_DCTL_ULSTCHNG_RX_DETECT' "$PROBE"
grep -q 'Dctl & ~DWC3_DCTL_RUN_STOP' "$PROBE"
grep -q 'Saved.DeviceConfig & ~DWC3_DCFG_DEVSPD_MASK' "$PROBE"
grep -q 'DWC3_DCFG_DEVSPD_HS[[:space:]]*0U' "$PROBE"
grep -q 'Dwc3ProbeWrite32 (DWC3_DCFG, Saved.DeviceConfig);' "$PROBE"
grep -q 'EnableHsPhy' "$PROBE"
grep -q 'RestoreHsPhy' "$PROBE"
grep -q 'EXYNOS_USBCON_CTRL_VER_03[[:space:]]*0x00030000U' "$PROBE"
grep -q 'B2L_PMU_USB_PHY_CTRL[[:space:]]*0x072C' "$PROBE"
grep -q 'HSP_TUNE_DEVICE_VALUE' "$PROBE"
grep -q 'FIFO_EVENT' "$PROBE"
if grep -q 'B2LMax77705.c' "$INF"; then
  echo 'HATA: R12 USB uygulamasina MAX77705 aktif kodu baglanmis' >&2
  exit 1
fi
grep -q 'bash yap.sh --touch --usb-test --clean' "$ROOT/BUILD.md"
grep -q 'bash yap.sh --touch --usb-test --clean' \
  "$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/README.md"
grep -q 'USB tani paketi Windows dokunmatigi korumali; b2ltouch.sys yok' "$ROOT/cache_esp.sh"

for Address in 10C00000 10B00000 10AE0000 10AF0000 15860000 15862200; do
  grep -q "$Address" "$APP"
done
grep -q 'B2L_HSI2C45_BASE[[:space:]]*0x15D60000UL' "$MAX"
grep -q 'B2L_MAX77705_PMIC_ADDRESS[[:space:]]*0x66' "$MAX"
grep -q 'B2L_MAX77705_FG_ADDRESS[[:space:]]*0x36' "$MAX"
grep -q 'B2L_HSI2C_TIMEOUT_US[[:space:]]*20000' "$MAX"

grep -q 'reg = <0x00 0x10c00000 0x10000>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'interrupts = <0x00 0xf1 0x00>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'reg = <0x00 0x10b00000 0x200 0x00 0x10ae0000 0x2800 0x00 0x10af0000 0x800>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'hsi2c@15D60000' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'max77705@66' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"

# DT'nin etkin USB pencereleri CPU sayfa tablolarinda yalniz gereken 4 KiB
# granuler kapsama yuvarlanmalidir. DRAM/reserved-memory descriptor'lari bu
# test tarafindan degistirilmez.
grep -Eq '\{"USB DRD PHY",[[:space:]]*0x10B00000,[[:space:]]*0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB Combo PCS",[[:space:]]*0x10AE0000,[[:space:]]*0x00003000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB Combo PMA",[[:space:]]*0x10AF0000,[[:space:]]*0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB DWC3 xHCI",[[:space:]]*0x10C00000,[[:space:]]*0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"MAX77705 HSI2C45",[[:space:]]*0x15D60000,[[:space:]]*0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"

if rg -n 'MmioWrite|IoWrite|PciWrite|WriteRegister|MmioBitFieldWrite|MmioAnd|MmioOr' "$APP"; then
  echo 'HATA: USB/PHY tanisinin pasif ana kaynagi register yaziyor' >&2
  exit 1
fi

# R12 aktif helper'inda DWC3, ana HS PHY ve PMU icin ayri dar runtime
# allow-list'ler vardir. Combo PHY, DWC3 rol/core-reset/endpoint yazilamaz.
if [ "$(rg -c 'MmioWrite32' "$PROBE")" -ne 3 ]; then
  echo 'HATA: R12 helper uc allow-list disinda MMIO write iceriyor' >&2
  exit 1
fi
for Register in DWC3_GEVNTADRLO0 DWC3_GEVNTADRHI0 DWC3_GEVNTSIZ0 \
                DWC3_GEVNTCOUNT0 DWC3_DEVTEN DWC3_DCFG DWC3_DCTL; do
  grep -q "case $Register:" "$PROBE"
done
if rg -n 'case DWC3_(GCTL|GSTS|GUSB|OCFG|OCTL|OEVT|OEVTEN|OSTS|DALEPENA|DEP)' "$PROBE"; then
  echo 'HATA: R12 DWC3 allow-list rol/core-reset/endpoint yazmasina genislemis' >&2
  exit 1
fi
for Register in EXYNOS_USBCON_LINK_CTRL EXYNOS_USBCON_LINK_PORT \
                EXYNOS_USBCON_CLKRST EXYNOS_USBCON_UTMI \
                EXYNOS_USBCON_HSP EXYNOS_USBCON_HSP_TUNE \
                EXYNOS_USBCON_HSP_TEST; do
  grep -q "case $Register:" "$PROBE"
done
grep -q 'if (Offset != B2L_PMU_USB_PHY_CTRL)' "$PROBE"
if rg -n 'B2L_USB_(PCS|PMA)_BASE|B2L_PMU_USBDP_CTRL' "$PROBE"; then
  echo 'HATA: R12 aktif helper combo PHY veya USB3 PMU penceresine genislemis' >&2
  exit 1
fi
if rg -n 'MmioWrite|IoWrite|PciWrite|MmioBitFieldWrite|MmioAnd|MmioOr' "$PROBE" | \
   grep -v -e 'MmioWrite32 (Dwc3Register (Offset), Value);' \
           -e 'MmioWrite32 (PhyRegister (Offset), Value);' \
           -e 'MmioWrite32 ((UINTN)(B2L_PMU_BASE + Offset), Value);'; then
  echo 'HATA: R12 helper allow-list disinda yazma iceriyor' >&2
  exit 1
fi

# R8 yardimcisi kaynak agacinda saklanir fakat R9 uygulamasina baglanmaz.
# Yeniden etkinlestirilirse PMIC'e giden write mesaji tam bir byte register
# pointer olmali; MAX77705 config/rol/reset verisi yazan API bulunmamalidir.
grep -q 'RegisterPointer = Register;' "$MAX"
grep -q 'TransferMessage (Slave, &RegisterPointer, 1, FALSE)' "$MAX"
grep -q 'RestoreControllerState (&Saved);' "$MAX"
grep -q 'MAX77705_FG_STATUS[[:space:]]*0x00' "$MAX"
grep -q 'MAX77705_FG_REPSOC[[:space:]]*0x06' "$MAX"
grep -q 'MAX77705_FG_TEMPERATURE[[:space:]]*0x08' "$MAX"
grep -q 'MAX77705_FG_VCELL[[:space:]]*0x09' "$MAX"
grep -q 'MAX77705_FG_CURRENT[[:space:]]*0x0A' "$MAX"
grep -q 'MAX77705_FG_AVG_CURRENT[[:space:]]*0x0B' "$MAX"
grep -q 'MAX77705_FG_AVG_VCELL[[:space:]]*0x19' "$MAX"
grep -q 'MAX77705_FG_OCV[[:space:]]*0xEE' "$MAX"

if rg -n 'MAX77705.*(SW_RESET|USBC_RESET)|ResetController|I2cWrite|WriteRegister' "$MAX"; then
  echo 'HATA: MAX77705 tani yolu reset veya aygit-verisi yazma APIsi iceriyor' >&2
  exit 1
fi

if rg -n 'for .*Register|Register\+\+|Register \+=' "$MAX"; then
  echo 'HATA: MAX77705 hedefli liste yerine register taramasi yapiyor' >&2
  exit 1
fi

if rg -n 'B2L_PD_FSYS0A_BASE, 0x1[04]' "$APP"; then
  echo 'HATA: cihazda external abort veren FSYS0A duration alani okunuyor' >&2
  exit 1
fi

if rg -n 'Read32 \(B2L_USB_(PCS|PMA)_BASE|ShowPhyWindow' "$APP"; then
  echo 'HATA: kablosuz testte external abort veren combo PHY penceresi okunuyor' >&2
  exit 1
fi

echo 'B2LUsbTest safety checks passed'
