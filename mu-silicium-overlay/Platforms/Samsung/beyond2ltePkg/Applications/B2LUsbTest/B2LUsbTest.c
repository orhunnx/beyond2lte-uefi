/** @file
  Read-only staged USB diagnostic for Samsung beyond2lte.

  This first bring-up revision deliberately contains no MMIO writes, no reset,
  no clock or regulator programming, no MAX77705 I2C transaction and no USB
  transfer.  Every new MMIO window is opened only after an explicit key press,
  so a bus fault or a clock-gated register window can be localized from the
  last photographed page.

  Address contract (merged stock beyond2lte DT):
    DWC3/xHCI       0x10C00000 + 0x10000, SPI 241 (GSIV 273)
    USB DRD PHY     0x10B00000 + 0x00200
    combo PHY PCS   0x10AE0000 + 0x02800
    combo PHY PMA   0x10AF0000 + 0x00800
    PMU syscon      0x15860000; USB PHY offsets 0x72C and 0x704
    FSYS0A PD       0x15862200 + 0x20
**/

#include <Uefi.h>

#include <Library/IoLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

#define B2L_USB_TEST_REVISION       3

#define B2L_DWC3_BASE               0x10C00000ULL
#define B2L_USB_PHY_BASE            0x10B00000ULL
#define B2L_USB_PCS_BASE            0x10AE0000ULL
#define B2L_USB_PMA_BASE            0x10AF0000ULL
#define B2L_PMU_BASE                0x15860000ULL
#define B2L_PD_FSYS0A_BASE          0x15862200ULL

#define B2L_PMU_USBDP_CTRL          0x0704
#define B2L_PMU_USB_PHY_CTRL        0x072C

#define DWC3_GSBUSCFG0              0xC100
#define DWC3_GSBUSCFG1              0xC104
#define DWC3_GCTL                   0xC110
#define DWC3_GSTS                   0xC118
#define DWC3_GUCTL1                 0xC11C
#define DWC3_GSNPSID                0xC120
#define DWC3_GUID                   0xC128
#define DWC3_GUCTL                  0xC12C
#define DWC3_GHWPARAMS0             0xC140
#define DWC3_GHWPARAMS1             0xC144
#define DWC3_GHWPARAMS2             0xC148
#define DWC3_GHWPARAMS3             0xC14C
#define DWC3_GUSB2PHYCFG0           0xC200
#define DWC3_GUSB3PIPECTL0          0xC2C0
#define DWC3_DCFG                   0xC700
#define DWC3_DCTL                   0xC704
#define DWC3_DSTS                   0xC70C
#define DWC3_OCFG                   0xCC00
#define DWC3_OCTL                   0xCC04
#define DWC3_OEVT                   0xCC08
#define DWC3_OEVTEN                 0xCC0C
#define DWC3_OSTS                   0xCC10

#define DWC3_GCTL_PRTCAP_SHIFT      12
#define DWC3_GCTL_PRTCAP_MASK       3U
#define DWC3_GCTL_CORESOFTRESET     (1U << 11)
#define DWC3_GSTS_CURMOD_MASK       3U
#define DWC3_GUSB2PHYCFG_SOFTRST    (1U << 31)
#define DWC3_GUSB2PHYCFG_SUSPHY     (1U << 6)
#define DWC3_GUSB3PIPECTL_SOFTRST   (1U << 31)
#define DWC3_GUSB3PIPECTL_SUSPHY    (1U << 17)

#define XHCI_HCSPARAMS1             0x04
#define XHCI_HCSPARAMS2             0x08
#define XHCI_HCSPARAMS3             0x0C
#define XHCI_HCCPARAMS1             0x10
#define XHCI_DBOFF                  0x14
#define XHCI_RTSOFF                 0x18
#define XHCI_HCCPARAMS2             0x1C
#define XHCI_USBCMD                 0x00
#define XHCI_USBSTS                 0x04
#define XHCI_PAGESIZE               0x08
#define XHCI_DNCTRL                 0x14
#define XHCI_CRCR_LO                0x18
#define XHCI_CRCR_HI                0x1C
#define XHCI_DCBAAP_LO              0x30
#define XHCI_DCBAAP_HI              0x34
#define XHCI_CONFIG                 0x38
#define XHCI_PORTSC                 0x400
#define XHCI_PORT_STRIDE            0x10

#define XHCI_USBSTS_HCH             (1U << 0)
#define XHCI_USBSTS_HSE             (1U << 2)
#define XHCI_USBSTS_CNR             (1U << 11)
#define XHCI_USBSTS_HCE             (1U << 12)
#define XHCI_PORTSC_CCS             (1U << 0)
#define XHCI_PORTSC_PED             (1U << 1)
#define XHCI_PORTSC_OCA             (1U << 3)
#define XHCI_PORTSC_PP              (1U << 9)

STATIC
UINT32
Read32 (
  IN UINTN  Base,
  IN UINTN  Offset
  )
{
  return MmioRead32 (Base + Offset);
}

STATIC
CONST CHAR16 *
PortCapabilityName (
  IN UINT32  Value
  )
{
  switch (Value) {
    case 1:
      return L"HOST";
    case 2:
      return L"DEVICE";
    case 3:
      return L"OTG";
    default:
      return L"RESERVED";
  }
}

STATIC
CONST CHAR16 *
CurrentModeName (
  IN UINT32  Value
  )
{
  switch (Value) {
    case 0:
      return L"DEVICE";
    case 1:
      return L"HOST";
    default:
      return L"UNKNOWN";
  }
}

STATIC
BOOLEAN
PromptForNextStage (
  IN CONST CHAR16  *Stage
  )
{
  EFI_EVENT      Event;
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Index;

  Print (L"\nSONRAKI: %s\n", Stage);
  Print (L"Fotografla. Devam: herhangi bir tus. Cikis: ESC veya Q.\n");

  if ((gST->ConIn == NULL) || (gST->ConIn->WaitForKey == NULL)) {
    Print (L"Klavye olayi yok; guvenli cikis.\n");
    return FALSE;
  }

  Event = gST->ConIn->WaitForKey;
  for (;;) {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (!EFI_ERROR (Status)) {
      break;
    }

    Status = gBS->WaitForEvent (1, &Event, &Index);
    if (EFI_ERROR (Status)) {
      Print (L"Tus beklenemedi: %r\n", Status);
      return FALSE;
    }
  }

  if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == L'q') ||
      (Key.UnicodeChar == L'Q')) {
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
ShowPowerDomain (
  VOID
  )
{
  UINT32  Config;
  UINT32  Status;
  UINT32  Option;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 1 FSYS0A ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"HICBIR MMIO YAZMASI, RESET, I2C VEYA USB TRANSFERI YOK.\n\n");

  Config = Read32 (B2L_PD_FSYS0A_BASE, 0x00);
  Print (L"CONFIG [15862200]=%08x\n", Config);
  Status = Read32 (B2L_PD_FSYS0A_BASE, 0x04);
  Print (L"STATUS [15862204]=%08x\n", Status);
  Option = Read32 (B2L_PD_FSYS0A_BASE, 0x08);
  Print (L"OPTION [15862208]=%08x\n", Option);

  Print (L"\nDT PD FSYS0A BASE=15862200 LEN=20\n");
  Print (L"CFG_STAT_ESIT=%s  STAT_ALT_NIBBLE=%x\n",
         (Config == Status) ? L"EVET" : L"HAYIR", Status & 0xF);
  Print (L"15862210 ve 15862214 okunmadi: R1'de 15862214 external abort verdi.\n");
  Print (L"Bu sayfa FSYS0A disinda register okumadi.\n");
}

STATIC
VOID
ShowPmuUsbControls (
  VOID
  )
{
  UINT32  UsbPhy;
  UINT32  UsbDp;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 2 PMU USB ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"Her deger okunduktan hemen sonra yazdirilir.\n\n");

  UsbDp = Read32 (B2L_PMU_BASE, B2L_PMU_USBDP_CTRL);
  Print (L"PMU USBDP  [15860704]=%08x\n", UsbDp);
  UsbPhy = Read32 (B2L_PMU_BASE, B2L_PMU_USB_PHY_CTRL);
  Print (L"PMU USBPHY [1586072C]=%08x\n", UsbPhy);
  Print (L"\nBu sayfa PMU USB kontrolleri disinda register okumadi.\n");
}

STATIC
BOOLEAN
ShowDwc3AndXhci (
  VOID
  )
{
  UINT32  Cap;
  UINT32  Hcs1;
  UINT32  Hcs2;
  UINT32  Hcs3;
  UINT32  Hcc1;
  UINT32  Hcc2;
  UINT32  Gctl;
  UINT32  Gsts;
  UINT32  Id;
  UINT32  Usb2;
  UINT32  Usb3;
  UINT32  MaxPorts;
  UINT32  MaxSlots;
  UINT32  CapLength;
  UINT32  OpBase;
  UINT32  UsbCmd;
  UINT32  UsbSts;
  UINT32  Port;
  UINT32  PortSc;
  BOOLEAN CoreSane;
  BOOLEAN CapSane;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 3 DWC3 XHCI ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=10C00000 LEN=10000 IRQ SPI=241 GSIV=273\n\n");

  Id    = Read32 (B2L_DWC3_BASE, DWC3_GSNPSID);
  Gctl  = Read32 (B2L_DWC3_BASE, DWC3_GCTL);
  Gsts  = Read32 (B2L_DWC3_BASE, DWC3_GSTS);
  Usb2  = Read32 (B2L_DWC3_BASE, DWC3_GUSB2PHYCFG0);
  Usb3  = Read32 (B2L_DWC3_BASE, DWC3_GUSB3PIPECTL0);

  CoreSane = (Id & 0xFFFF0000U) == 0x55330000U;
  Print (L"GSNPSID=%08x  CORE_ID=%s\n", Id,
         CoreSane ? L"DWC3" : L"BEKLENMEDIK");
  Print (L"GCTL=%08x PRTCAP=%s CORE_SRST=%u\n", Gctl,
         PortCapabilityName ((Gctl >> DWC3_GCTL_PRTCAP_SHIFT) &
                             DWC3_GCTL_PRTCAP_MASK),
         (Gctl & DWC3_GCTL_CORESOFTRESET) != 0);
  Print (L"GSTS=%08x CURMOD=%s\n", Gsts,
         CurrentModeName (Gsts & DWC3_GSTS_CURMOD_MASK));
  Print (L"USB2PHYCFG=%08x SRST=%u SUSPHY=%u\n", Usb2,
         (Usb2 & DWC3_GUSB2PHYCFG_SOFTRST) != 0,
         (Usb2 & DWC3_GUSB2PHYCFG_SUSPHY) != 0);
  Print (L"USB3PIPECTL=%08x SRST=%u SUSPHY=%u\n", Usb3,
         (Usb3 & DWC3_GUSB3PIPECTL_SOFTRST) != 0,
         (Usb3 & DWC3_GUSB3PIPECTL_SUSPHY) != 0);
  Print (L"BUS0=%08x BUS1=%08x GUCTL1=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GSBUSCFG0),
         Read32 (B2L_DWC3_BASE, DWC3_GSBUSCFG1),
         Read32 (B2L_DWC3_BASE, DWC3_GUCTL1));
  Print (L"GUID=%08x GUCTL=%08x HWP0=%08x HWP1=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GUID),
         Read32 (B2L_DWC3_BASE, DWC3_GUCTL),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS0),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS1));
  Print (L"HWP2=%08x HWP3=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS2),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS3));

  Cap       = Read32 (B2L_DWC3_BASE, 0x00);
  Hcs1      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS1);
  Hcs2      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS2);
  Hcs3      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS3);
  Hcc1      = Read32 (B2L_DWC3_BASE, XHCI_HCCPARAMS1);
  Hcc2      = Read32 (B2L_DWC3_BASE, XHCI_HCCPARAMS2);
  CapLength = Cap & 0xFF;
  MaxSlots  = Hcs1 & 0xFF;
  MaxPorts  = (Hcs1 >> 24) & 0xFF;
  CapSane   = (CapLength >= 0x20) && (CapLength <= 0x100) &&
              (MaxSlots != 0) && (MaxPorts != 0) && (MaxPorts <= 16);

  Print (L"\nXHCI CAP=%08x CAPLEN=%x HCIVER=%04x SANE=%s\n",
         Cap, CapLength, Cap >> 16, CapSane ? L"EVET" : L"HAYIR");
  Print (L"HCS1=%08x SLOT=%u PORT=%u INTR=%u\n", Hcs1, MaxSlots,
         MaxPorts, (Hcs1 >> 8) & 0x7FF);
  Print (L"HCS2=%08x HCS3=%08x HCC1=%08x HCC2=%08x\n",
         Hcs2, Hcs3, Hcc1, Hcc2);
  Print (L"DBOFF=%08x RTSOFF=%08x\n",
         Read32 (B2L_DWC3_BASE, XHCI_DBOFF),
         Read32 (B2L_DWC3_BASE, XHCI_RTSOFF));

  if (!CapSane) {
    Print (L"XHCI turetilmis adresleri okunmadi: capability gecersiz.\n");
    Print (L"Device rolde bu ayri bir olcumdur; DWC3 kimligi gecerliyse devam.\n");
    return CoreSane;
  }

  OpBase = CapLength;
  UsbCmd = Read32 (B2L_DWC3_BASE, OpBase + XHCI_USBCMD);
  UsbSts = Read32 (B2L_DWC3_BASE, OpBase + XHCI_USBSTS);
  Print (L"CMD=%08x STS=%08x RUN=%u HCH=%u CNR=%u HSE=%u HCE=%u\n",
         UsbCmd, UsbSts, (UsbCmd & 1U) != 0,
         (UsbSts & XHCI_USBSTS_HCH) != 0,
         (UsbSts & XHCI_USBSTS_CNR) != 0,
         (UsbSts & XHCI_USBSTS_HSE) != 0,
         (UsbSts & XHCI_USBSTS_HCE) != 0);
  Print (L"PAGE=%08x DNCTRL=%08x CONFIG=%08x\n",
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_PAGESIZE),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DNCTRL),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CONFIG));
  Print (L"CRCR=%08x:%08x DCBAAP=%08x:%08x\n",
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CRCR_HI),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CRCR_LO),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DCBAAP_HI),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DCBAAP_LO));

  for (Port = 0; Port < MaxPorts; Port++) {
    PortSc = Read32 (B2L_DWC3_BASE,
                     OpBase + XHCI_PORTSC + (Port * XHCI_PORT_STRIDE));
    Print (L"PORT%u=%08x CCS=%u PED=%u PP=%u OCA=%u PLS=%x SPEED=%x\n",
           Port + 1, PortSc,
           (PortSc & XHCI_PORTSC_CCS) != 0,
           (PortSc & XHCI_PORTSC_PED) != 0,
           (PortSc & XHCI_PORTSC_PP) != 0,
           (PortSc & XHCI_PORTSC_OCA) != 0,
           (PortSc >> 5) & 0xF, (PortSc >> 10) & 0xF);
  }

  return CoreSane;
}

STATIC
VOID
ShowDwc3RoleRegisters (
  VOID
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 4 ROL ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"DWC3 device ve OTG registerleri; yalniz okuma.\n\n");
  Print (L"DCFG=%08x DCTL=%08x DSTS=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_DCFG),
         Read32 (B2L_DWC3_BASE, DWC3_DCTL),
         Read32 (B2L_DWC3_BASE, DWC3_DSTS));
  Print (L"OCFG=%08x OCTL=%08x OSTS=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_OCFG),
         Read32 (B2L_DWC3_BASE, DWC3_OCTL),
         Read32 (B2L_DWC3_BASE, DWC3_OSTS));
  Print (L"OEVT=%08x OEVTEN=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_OEVT),
         Read32 (B2L_DWC3_BASE, DWC3_OEVTEN));
  Print (L"\nMAX77705 henuz okunmadi; I2C/PD/rol komutu yok.\n");
}

STATIC
VOID
ShowPhyWindow (
  IN CONST CHAR16  *Name,
  IN UINTN          Base,
  IN UINTN          Length,
  IN UINTN          Stage
  )
{
  UINTN  Offset;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA %u %s ===\n",
         B2L_USB_TEST_REVISION, Stage, Name);
  Print (L"BASE=%08x DT_LEN=%x OKUNAN=0..3C\n\n", Base, Length);
  for (Offset = 0; Offset < 0x40; Offset += 0x10) {
    Print (L"%03x: %08x %08x %08x %08x\n", Offset,
           Read32 (Base, Offset + 0x00), Read32 (Base, Offset + 0x04),
           Read32 (Base, Offset + 0x08), Read32 (Base, Offset + 0x0C));
  }
  Print (L"\nGenis tarama yok; read-clear olabilecek bilinmeyen alanlar okunmadi.\n");
}

EFI_STATUS
EFIAPI
B2LUsbTestEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  BOOLEAN  Dwc3Sane;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  ShowPowerDomain ();
  if (!PromptForNextStage (L"PMU USB kontrol registerleri 15860704/1586072C")) {
    return EFI_SUCCESS;
  }
  ShowPmuUsbControls ();

  if (!PromptForNextStage (L"DWC3 ve xHCI penceresi 10C00000")) {
    return EFI_SUCCESS;
  }

  Dwc3Sane = ShowDwc3AndXhci ();
  if (!Dwc3Sane) {
    Print (L"\nDWC3 GSNPSID dogrulanmadi; PHY asamalari kapatildi.\n");
    PromptForNextStage (L"guvenli cikis");
    return EFI_DEVICE_ERROR;
  }

  if (!PromptForNextStage (L"DWC3 device OTG rol registerleri")) {
    return EFI_SUCCESS;
  }
  ShowDwc3RoleRegisters ();

  if (!PromptForNextStage (L"USB DRD PHY ana pencere 10B00000")) {
    return EFI_SUCCESS;
  }
  ShowPhyWindow (L"USB DRD PHY", B2L_USB_PHY_BASE, 0x200, 5);

  if (!PromptForNextStage (L"combo PHY PCS penceresi 10AE0000")) {
    return EFI_SUCCESS;
  }
  ShowPhyWindow (L"COMBO PHY PCS", B2L_USB_PCS_BASE, 0x2800, 6);

  if (!PromptForNextStage (L"combo PHY PMA penceresi 10AF0000")) {
    return EFI_SUCCESS;
  }
  ShowPhyWindow (L"COMBO PHY PMA", B2L_USB_PMA_BASE, 0x800, 7);

  Print (L"\nR%u SALT OKUMA TURLARI TAMAMLANDI.\n", B2L_USB_TEST_REVISION);
  Print (L"Son sayfayi fotografla; tusa basinca EFI Shell'e doner.\n");
  PromptForNextStage (L"EFI Shell'e donus");
  return EFI_SUCCESS;
}
