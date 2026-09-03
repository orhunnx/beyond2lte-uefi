#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN="$ROOT/windows/b2lusb"
MAIN="$WIN/b2lmax77705.c"
HW="$WIN/b2lmax77705_hw.c"
HDR="$WIN/b2lmax77705.h"
INF="$WIN/b2lmax77705.inf.in"
DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"
DXE="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.c"
DXE_INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf"
DXE_INC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
HSI2C="$ROOT/mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/HsI2cLib/HsI2c.c"

# Stock beyond2lte HSI2C45/USI ownership is isolated in ACPI\BYN0004.
grep -q 'B2L_MAX_HSI2C_PHYSICAL_BASE 0x15D60000ULL' "$HDR"
grep -q 'B2L_MAX_USI_PHYSICAL_BASE    0x15C22000ULL' "$HDR"
grep -Fq 'ACPI\BYN0004' "$INF"
grep -Fq 'Name (_HID, "BYN0004")' "$DSDT"
grep -Fq '\_SB.USBC' "$DSDT"
test "$(rg -c 'MmMapIoSpaceEx' "$MAIN")" -eq 2
if rg -n '10C00000|10B00000|10AE0000|10AF0000|1586072C' "$MAIN" "$HW" "$HDR"; then
  echo 'HATA: MAX77705 surucusu DWC3/PHY/PMU kaynagina genislemis' >&2
  exit 1
fi

# Samsung MFD logical-client addresses: TOP, USBC/MUIC and charger.
grep -q 'B2L_MAX_TOP_ADDRESS          0x66U' "$HDR"
grep -q 'B2L_MAX_USBC_ADDRESS         0x25U' "$HDR"
grep -q 'B2L_MAX_CHARGER_ADDRESS      0x69U' "$HDR"
if rg -n 'B2L_MAX_FUEL|0x36U' "$MAIN" "$HW" "$HDR"; then
  echo 'HATA: USB policy surucusu kullanmadigi fuel-gauge istemcisini sahiplenmemeli' >&2
  exit 1
fi
grep -q 'MAX77705_TOP_PMIC_ID1      0x00U' "$MAIN"
grep -q 'MAX77705_TOP_PMIC_REV      0x01U' "$MAIN"
grep -q 'Context->PmicId == 0x05U' "$MAIN"
grep -q 'Context->PmicId == 0x15U' "$MAIN"

# CC is read from the MAX77705 firmware state machine.  Connector VBUS is
# enabled only for SOURCE; no-connection and SINK undo OTG.
grep -q 'MAX77705_USBC_CC_STATUS0   0x0AU' "$MAIN"
grep -q 'B2L_MAX_CC_STATUS_MASK       0x07U' "$HDR"
grep -q 'B2L_MAX_CC_SOURCE            0x02U' "$HDR"
grep -q 'CcState == B2L_MAX_CC_SOURCE' "$MAIN"
grep -q 'CcState == B2L_MAX_CC_NO_CONNECTION' "$MAIN"
grep -q 'CcState == B2L_MAX_CC_SINK' "$MAIN"

# Preserve Samsung charger OTG mode-state and interrupt-mask transitions.
grep -q 'MAX77705_CHG_INT_MASK      0xB1U' "$MAIN"
grep -q 'MAX77705_CHG_CNFG_00       0xB7U' "$MAIN"
grep -q 'MAX77705_CHG_OTG_INT_MASK  0x51U' "$MAIN"
grep -q 'MAX77705_CHG_OTG_INT_VALUE 0x50U' "$MAIN"
for Mode in \
  MAX77705_MODE_ALL_OFF MAX77705_MODE_BUCK MAX77705_MODE_BUCK_CHG \
  MAX77705_MODE_BOOST_UNO MAX77705_MODE_BOOST_OTG \
  MAX77705_MODE_BUCK_UNO MAX77705_MODE_BUCK_CHG_UNO \
  MAX77705_MODE_BUCK_OTG MAX77705_MODE_BUCK_CHG_OTG; do
  grep -q "$Mode" "$MAIN"
done
grep -q 'B2lMaxDelayMicroseconds(1500U)' "$MAIN"
grep -q 'B2lMaxDelayMicroseconds(50000U)' "$MAIN"
grep -q 'KeDelayExecutionThread' "$MAIN"
if rg -n 'KeStallExecutionProcessor\((1500|50000)U\)' "$MAIN"; then
  echo 'HATA: Samsung sleep araligi Windows CPU aktif beklemesine donusmus' >&2
  exit 1
fi
grep -q 'Context->ChargerIntMaskBaseline' "$MAIN"
grep -q 'Context->ChargerModeBaseline' "$MAIN"

# All MAX77705 register writes go through masked read/modify/write + readback.
test "$(rg -c 'B2lMaxWriteByte\(' "$MAIN")" -eq 2
grep -q 'Wanted = (UCHAR)((Current &' "$MAIN"
grep -Fq '((*ReadBack) & Mask) == (Wanted & Mask)' "$MAIN"
if rg -ni 'firmware.*(write|update|flash)|opcode|SW_RESET|USBC_RESET' "$MAIN" "$HDR"; then
  echo 'HATA: ilk MAX77705 Windows turu firmware/reset politikasina genislemis' >&2
  exit 1
fi

# HSI2C transfer path is bounded, serialized and uses repeated START for
# one-byte register reads, matching Samsung's register transport.
grep -q 'B2L_HSI2C_TIMEOUT_US    100000UL' "$HW"
grep -q 'HSI2C_FUNC_MODE_I2C     (1UL << 0)' "$HW"
grep -Fq 'HSI2C_FUNC_MODE_I2C | HSI2C_MASTER' "$HW"
grep -q 'WdfWaitLockAcquire(Context->BusLock' "$MAIN"
grep -q 'B2lMaxTransferMessage' "$HW"
grep -q 'FALSE,' "$HW"
grep -q '(BOOLEAN)(ReadLength == 0UL)' "$HW"
grep -q 'B2L_MAX_POLL_MS              200UL' "$HDR"
grep -q 'WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lMaxEvtPollTimer)' "$MAIN"
if grep -q 'WDF_TIMER_CONFIG_INIT_PERIODIC' "$MAIN"; then
  echo 'HATA: pasif MAX77705 timer periyodik WDF timer olarak olusturulamaz' >&2
  exit 1
fi
grep -q 'TimerConfig.AutomaticSerialization = FALSE' "$MAIN"
grep -q 'Context->PollingEnabled = TRUE' "$MAIN"
grep -q 'Context->PollingEnabled = FALSE' "$MAIN"
test "$(rg -c 'WdfTimerStart\(' "$MAIN")" -eq 2
test "$(rg -c '\(VOID\)B2lMaxServiceConnection\(Context\)' "$MAIN")" -eq 2
if sed -n '/B2lMaxEvtD0Entry/,/B2lMaxEvtD0Exit/p' "$MAIN" | grep -q 'return Status'; then
  echo 'HATA: gecici MAX77705 I2C hatasi ACPI parent D0 girisini dusurmemeli' >&2
  exit 1
fi

# UEFI only prepares/probes the exact stock bus; Windows remains policy owner.
grep -q '\[45\] = {' "$HSI2C"
grep -q 'SclBankNum = 12' "$HSI2C"
grep -q 'SdaBankNum = 13' "$HSI2C"
grep -q 'B2L_MAX77705_BUS             45' "$DXE"
test "$(rg -c 'HsI2c->Read \(' "$DXE")" -eq 6
grep -qx '  beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$DSC"
grep -qx '  # INF beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$DXE_INC"
grep -q 'MODULE_TYPE                    = DXE_DRIVER' "$DXE_INF"
grep -q '^  SiliciumPkg/SiliciumPkg.dec$' "$DXE_INF"
grep -q '^  MemoryAllocationHelperLib$' "$DXE_INF"
grep -q '^  (VOID)ImageHandle;$' "$DXE"
grep -q '^  (VOID)SystemTable;$' "$DXE"
if rg -n 'UNREFERENCED_PARAMETER|PAGED_CODE|NT_ASSERT|__pragma|__declspec' "$DXE"; then
  echo 'HATA: UEFI MAX77705 DXE kaynaginda Windows-only C makrosu bulundu' >&2
  exit 1
fi
if rg -n 'HsI2c->Write|CHG_INT_MASK|CC_STATUS0' "$DXE"; then
  echo 'HATA: UEFI hazirlik surucusu MAX77705 politikasina yazmamalı' >&2
  exit 1
fi

grep -q 'b2lmax77705.sys' "$WIN/build-b2lusb.ps1"
grep -q 'b2lmax77705.inf' "$ROOT/tools_b2lusb.sh"
grep -q 'b2lmax77705.inf' "$ROOT/windows/stage-winpe-driverstore.ps1"
grep -Fq 'B2LUSB-MAX77705.TXT' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxChgDetails2' "$ROOT/windows/startnet.cmd"

echo 'Windows MAX77705 safety checks: PASS'
