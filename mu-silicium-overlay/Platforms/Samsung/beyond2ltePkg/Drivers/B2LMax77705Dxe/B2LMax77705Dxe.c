/** @file
  Prepare the stock Exynos9820 HSI2C45 path used by MAX77705.

  Windows owns MAX77705 policy.  This DXE driver only performs the same SoC
  bus setup that the Samsung HSI2C driver performs (CMGP pinmux, USI I2C mode
  and 200 MHz timing), keeps the three stock USB PHY boot-on regulators on,
  and proves that the three stock MAX77705 I2C clients answer.  It never
  changes charger, CC/PD or firmware registers.
**/

#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationHelperLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/EFIHsI2c.h>
#include <Protocol/EFIPmicRegulator.h>

#define B2L_MAX77705_BUS             45
#define B2L_MAX77705_USI_PAGE        0x15C22000ULL
#define B2L_MAX77705_USI_PAGE_SIZE   0x00001000ULL

#define B2L_MAX77705_TOP_ADDR        0x66
#define B2L_MAX77705_USBC_ADDR       0x25
#define B2L_MAX77705_CHARGER_ADDR    0x69

#define MAX77705_TOP_PMIC_ID1        0x00
#define MAX77705_TOP_PMIC_REV        0x01
#define MAX77705_USBC_HW_REV         0x00
#define MAX77705_USBC_FW_REV         0x01
#define MAX77705_USBC_FW_MINOR       0x09
#define MAX77705_CHG_CNFG_00         0xB7

#define B2L_USB_PHY_LDO10            10
#define B2L_USB_PHY_LDO11            11
#define B2L_USB_PHY_LDO12            12
#define B2L_USB_PHY_LDO_SETTLE_US    1000

extern EFI_GUID  gEfiHsI2cProtocolGuid;

STATIC
EFI_STATUS
EnableUsbPhyPower (
  VOID
  )
{
  EFI_PMIC_REGULATOR_PROTOCOL  *Pmic;
  EFI_STATUS                    Status;
  CONST UINT8                   UsbPhyLdos[] = {
    B2L_USB_PHY_LDO10,
    B2L_USB_PHY_LDO11,
    B2L_USB_PHY_LDO12
  };
  UINTN                         Index;

  Status = gBS->LocateProtocol (
                  &gEfiPmicRegulatorProtocolGuid,
                  NULL,
                  (VOID **)&Pmic
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // exynos_usbdrd_phy_conn() enables S2MPS19 LDO10M/11M/12M together and
  // waits 1 ms before DWC3 initialization.  The beyond2lte DT marks all
  // three rails boot-on.  SetLdo performs a masked read/modify/write, so the
  // configured voltages are preserved.  Never disable or power-cycle them:
  // Windows has no native ACPM/S2MPS19 regulator provider yet.
  for (Index = 0;
       Index < (sizeof (UsbPhyLdos) / sizeof (UsbPhyLdos[0]));
       ++Index) {
    Status = Pmic->SetLdo (
                     ID_S2MPS19,
                     UsbPhyLdos[Index],
                     S2MPS_MODE_NORMAL,
                     TRUE
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "B2LMax77705: USB PHY LDO%u enable failed: %r\n",
        (UINT32)UsbPhyLdos[Index],
        Status
        ));
      return Status;
    }
  }

  gBS->Stall (B2L_USB_PHY_LDO_SETTLE_US);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
B2LMax77705Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HSI2C_PROTOCOL  *HsI2c;
  EFI_STATUS           Status;
  UINT8                PmicId;
  UINT8                PmicRev;
  UINT8                UsbcHw;
  UINT8                UsbcFw;
  UINT8                UsbcMinor;
  UINT8                ChargerMode;

  // Use the native EDK2 unused-parameter convention.
  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = EnableUsbPhyPower ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (
                  &gEfiHsI2cProtocolGuid,
                  NULL,
                  (VOID **)&HsI2c
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // UsiDxe writes 0x15C22030 while selecting I2C mode.  The page is device
  // MMIO, not reserved DRAM, and must be mapped before InitBus(45).
  Status = MapMemoryRegion (
             B2L_MAX77705_USI_PAGE,
             B2L_MAX77705_USI_PAGE_SIZE,
             EfiMemoryMappedIO
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "B2LMax77705: USI page map failed: %r\n", Status));
    return Status;
  }

  Status = HsI2c->InitBus (B2L_MAX77705_BUS);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "B2LMax77705: HSI2C45 init failed: %r\n", Status));
    return Status;
  }

  // Match max77705_i2c_probe(): identity must be readable before creating
  // the MUIC and charger clients.  All accesses below are read-only.
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_TOP_ADDR,
                    MAX77705_TOP_PMIC_ID1,
                    &PmicId
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_TOP_ADDR,
                    MAX77705_TOP_PMIC_REV,
                    &PmicRev
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_USBC_ADDR,
                    MAX77705_USBC_HW_REV,
                    &UsbcHw
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_USBC_ADDR,
                    MAX77705_USBC_FW_REV,
                    &UsbcFw
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_USBC_ADDR,
                    MAX77705_USBC_FW_MINOR,
                    &UsbcMinor
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = HsI2c->Read (
                    B2L_MAX77705_BUS,
                    B2L_MAX77705_CHARGER_ADDR,
                    MAX77705_CHG_CNFG_00,
                    &ChargerMode
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "B2LMax77705: TOP=%02x/%02x USBC=%02x FW=%02x.%02x CHG_MODE=%x\n",
    PmicId,
    PmicRev,
    UsbcHw,
    UsbcFw,
    UsbcMinor,
    ChargerMode & 0x0F
    ));
  return EFI_SUCCESS;
}
