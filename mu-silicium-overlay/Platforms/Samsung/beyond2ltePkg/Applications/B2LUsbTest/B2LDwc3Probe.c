/** @file
  Guarded DWC_usb31 event-DMA and soft-connect probe for beyond2lte.

  This is deliberately not a USB gadget implementation.  It establishes the
  first active device-mode boundary needed before EP0 and bulk transfer work:
  a controller-owned event ring in allocated RAM and a short DCTL.RUN_STOP
  interval.  No PMU, PHY, role, reset, endpoint command or endpoint-enable
  register is written.

  The probe refuses to write unless the core is a halted DWC_usb31 in device
  mode with no enabled endpoints, no pending event and no inherited device
  event enable.  The event interrupt stays masked; firmware polls and
  acknowledges the ring.  On a normal stop every touched register is restored
  and the DMA page is freed.  If the controller cannot halt, the page is kept
  reserved and execution intentionally stops so later software cannot reclaim
  memory that may still be a DMA target.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

#include "B2LDwc3Probe.h"

#define B2L_DWC3_BASE                0x10C00000ULL

#define DWC3_GCTL                    0xC110
#define DWC3_GSTS                    0xC118
#define DWC3_GSNPSID                 0xC120
#define DWC3_GHWPARAMS0              0xC140
#define DWC3_GEVNTADRLO0             0xC400
#define DWC3_GEVNTADRHI0             0xC404
#define DWC3_GEVNTSIZ0               0xC408
#define DWC3_GEVNTCOUNT0             0xC40C
#define DWC3_DCTL                    0xC704
#define DWC3_DEVTEN                  0xC708
#define DWC3_DSTS                    0xC70C
#define DWC3_DALEPENA                0xC720
#define DWC3_DEPCMD0                 0xC80C
#define DWC3_DEPCMD1                 0xC81C

#define DWC3_GSNPSID_MASK            0xFFFF0000U
#define DWC3_IP_DWC31                0x33310000U
#define DWC3_GCTL_PRTCAP_SHIFT       12
#define DWC3_GCTL_PRTCAP_MASK        3U
#define DWC3_GCTL_PRTCAP_DEVICE      2U
#define DWC3_GCTL_PRTCAP_OTG         3U
#define DWC3_GCTL_CORESOFTRESET      BIT11
#define DWC3_GSTS_CURMOD_MASK        3U
#define DWC3_GSTS_CURMOD_DEVICE      0U
#define DWC3_GHWPARAMS0_AWIDTH_SHIFT 24

#define DWC3_GEVNTSIZ_INTMASK        BIT31
#define DWC3_GEVNTSIZ_SIZE_MASK      0xFFFFU
#define DWC3_GEVNTCOUNT_MASK         0xFFFCU
#define DWC3_GEVNTCOUNT_EHB          BIT31

#define DWC3_DCTL_RUN_STOP           BIT31
#define DWC3_DCTL_CSFTRST            BIT30
#define DWC3_DSTS_COREIDLE           BIT23
#define DWC3_DSTS_DEVCTRLHLT         BIT22
#define DWC3_DSTS_RXFIFOEMPTY        BIT17
#define DWC3_DSTS_SOFFN_MASK         (0x3FFFU << 3)
#define DWC3_DSTS_USBLNKST_SHIFT     18
#define DWC3_DSTS_USBLNKST_MASK      (0xFU << DWC3_DSTS_USBLNKST_SHIFT)
#define DWC3_DSTS_CONNECTSPD_MASK    7U
#define DWC3_DEPCMD_CMDACT           BIT10

#define DWC3_DEVTEN_EVNTOVERFLOWEN   BIT11
#define DWC3_DEVTEN_CMDCMPLTEN       BIT10
#define DWC3_DEVTEN_ERRTICERREN      BIT9
#define DWC3_DEVTEN_WKUPEVTEN        BIT4
#define DWC3_DEVTEN_CONNECTDONEEN    BIT2
#define DWC3_DEVTEN_USBRSTEN         BIT1
#define DWC3_DEVTEN_DISCONNEVTEN     BIT0
#define DWC3_PROBE_DEVICE_EVENTS     (DWC3_DEVTEN_EVNTOVERFLOWEN | \
                                      DWC3_DEVTEN_CMDCMPLTEN | \
                                      DWC3_DEVTEN_ERRTICERREN | \
                                      DWC3_DEVTEN_WKUPEVTEN | \
                                      DWC3_DEVTEN_CONNECTDONEEN | \
                                      DWC3_DEVTEN_USBRSTEN | \
                                      DWC3_DEVTEN_DISCONNEVTEN)

#define DWC3_EVENT_BUFFER_SIZE       EFI_PAGE_SIZE
#define DWC3_EVENT_WORD_COUNT        (DWC3_EVENT_BUFFER_SIZE / sizeof (UINT32))
#define DWC3_PROBE_SECONDS           30
#define DWC3_PROBE_LAST_EVENTS       8
#define DWC3_STOP_TIMEOUT_MS         2000

typedef struct {
  UINT32  EventAddressLow;
  UINT32  EventAddressHigh;
  UINT32  EventSize;
  UINT32  DeviceEventEnable;
  UINT32  DeviceControl;
} B2L_DWC3_PROBE_SAVED;

typedef struct {
  UINTN   ReadPosition;
  UINTN   Total;
  UINTN   EndpointTotal;
  UINTN   OtherTotal;
  UINTN   DeviceType[16];
  UINT32  Last[DWC3_PROBE_LAST_EVENTS];
  UINTN   LastCount;
  UINTN   LastWrite;
} B2L_DWC3_EVENT_STATS;

STATIC
UINTN
Dwc3Register (
  IN UINTN  Offset
  )
{
  return (UINTN)(B2L_DWC3_BASE + Offset);
}

STATIC
UINT32
Dwc3Read32 (
  IN UINTN  Offset
  )
{
  return MmioRead32 (Dwc3Register (Offset));
}

/**
  Runtime write allow-list.  Keep this switch narrower than the mapped DWC3
  page: event ring ownership, device-event enable and soft connect only.
**/
STATIC
BOOLEAN
Dwc3ProbeWrite32 (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  switch (Offset) {
    case DWC3_GEVNTADRLO0:
    case DWC3_GEVNTADRHI0:
    case DWC3_GEVNTSIZ0:
    case DWC3_GEVNTCOUNT0:
    case DWC3_DEVTEN:
    case DWC3_DCTL:
      MmioWrite32 (Dwc3Register (Offset), Value);
      return TRUE;
    default:
      Print (L"R10 IC HATA: izin verilmeyen DWC3 write offset=%x\n", Offset);
      return FALSE;
  }
}

STATIC
CONST CHAR16 *
Dwc3LinkStateName (
  IN UINT32  Dsts
  )
{
  switch ((Dsts & DWC3_DSTS_USBLNKST_MASK) >>
          DWC3_DSTS_USBLNKST_SHIFT) {
    case 0:
      return L"U0";
    case 1:
      return L"U1";
    case 2:
      return L"U2";
    case 3:
      return L"U3";
    case 4:
      return L"SS_DIS";
    case 5:
      return L"RX_DET";
    case 6:
      return L"SS_INACT";
    case 7:
      return L"POLL";
    case 8:
      return L"RECOV";
    case 9:
      return L"HOT_RST";
    case 10:
      return L"COMP";
    case 11:
      return L"LOOPBACK";
    case 14:
      return L"RESET";
    case 15:
      return L"RESUME";
    default:
      return L"UNKNOWN";
  }
}

STATIC
CONST CHAR16 *
Dwc3SpeedName (
  IN UINT32  Dsts
  )
{
  switch (Dsts & DWC3_DSTS_CONNECTSPD_MASK) {
    case 0:
      return L"HS";
    case 1:
      return L"FS";
    case 2:
      return L"LS";
    case 3:
      return L"FS48";
    case 4:
      return L"SS";
    case 5:
      return L"SSP";
    default:
      return L"UNKNOWN";
  }
}

STATIC
CONST CHAR16 *
DeviceEventName (
  IN UINTN  Type
  )
{
  switch (Type) {
    case 0:
      return L"DISCONNECT";
    case 1:
      return L"USB_RESET";
    case 2:
      return L"CONNECT_DONE";
    case 3:
      return L"LINK_CHANGE";
    case 4:
      return L"WAKEUP";
    case 6:
      return L"SUSPEND";
    case 7:
      return L"SOF";
    case 9:
      return L"ERRATIC";
    case 10:
      return L"CMD_COMPLETE";
    case 11:
      return L"OVERFLOW";
    default:
      return L"OTHER";
  }
}

STATIC
VOID
RememberEvent (
  IN OUT B2L_DWC3_EVENT_STATS  *Stats,
  IN     UINT32                 Event
  )
{
  UINTN  Type;

  Stats->Last[Stats->LastWrite] = Event;
  Stats->LastWrite = (Stats->LastWrite + 1) % DWC3_PROBE_LAST_EVENTS;
  if (Stats->LastCount < DWC3_PROBE_LAST_EVENTS) {
    Stats->LastCount++;
  }

  Stats->Total++;
  if ((Event & BIT0) != 0) {
    Stats->EndpointTotal++;
    return;
  }

  if (((Event >> 1) & 0x7F) != 0) {
    Stats->OtherTotal++;
    return;
  }

  Type = (Event >> 8) & 0xF;
  Stats->DeviceType[Type]++;
}

STATIC
UINT32
DrainEventBuffer (
  IN     EFI_PHYSICAL_ADDRESS   BufferAddress,
  IN OUT B2L_DWC3_EVENT_STATS  *Stats
  )
{
  volatile UINT32  *Events;
  UINT32            CountRegister;
  UINT32            EventBytes;
  UINTN             Index;
  UINTN             WordCount;

  CountRegister = Dwc3Read32 (DWC3_GEVNTCOUNT0);
  EventBytes    = CountRegister & DWC3_GEVNTCOUNT_MASK;
  if (EventBytes == 0) {
    return CountRegister;
  }

  if ((EventBytes > DWC3_EVENT_BUFFER_SIZE) ||
      ((EventBytes & (sizeof (UINT32) - 1)) != 0)) {
    Print (L"R10 EVENT COUNT gecersiz: %08x\n", CountRegister);
    return CountRegister;
  }

  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );
  Events    = (volatile UINT32 *)(UINTN)BufferAddress;
  WordCount = EventBytes / sizeof (UINT32);
  for (Index = 0; Index < WordCount; Index++) {
    RememberEvent (Stats, Events[Stats->ReadPosition]);
    Stats->ReadPosition = (Stats->ReadPosition + 1) % DWC3_EVENT_WORD_COUNT;
  }

  Dwc3ProbeWrite32 (DWC3_GEVNTCOUNT0, EventBytes);
  return CountRegister;
}

STATIC
VOID
PrintLastEvents (
  IN CONST B2L_DWC3_EVENT_STATS  *Stats
  )
{
  UINTN  Index;
  UINTN  Position;
  UINTN  Type;

  Print (L"SON OLAYLAR:");
  for (Index = 0; Index < Stats->LastCount; Index++) {
    Position = (Stats->LastWrite + DWC3_PROBE_LAST_EVENTS -
                Stats->LastCount + Index) % DWC3_PROBE_LAST_EVENTS;
    Print (L" %08x", Stats->Last[Position]);
  }
  Print (L"\n");

  if (Stats->LastCount == 0) {
    Print (L"SON TIP: henuz olay yok\n");
    return;
  }

  Position = (Stats->LastWrite + DWC3_PROBE_LAST_EVENTS - 1) %
             DWC3_PROBE_LAST_EVENTS;
  if ((Stats->Last[Position] & BIT0) != 0) {
    Print (L"SON TIP: ENDPOINT ep=%u tip=%u\n",
           (Stats->Last[Position] >> 1) & 0x1F,
           (Stats->Last[Position] >> 6) & 0xF);
  } else if (((Stats->Last[Position] >> 1) & 0x7F) == 0) {
    Type = (Stats->Last[Position] >> 8) & 0xF;
    Print (L"SON TIP: DEVICE %s(%u) bilgi=%x\n",
           DeviceEventName (Type), Type,
           (Stats->Last[Position] >> 16) & 0x1FF);
  } else {
    Print (L"SON TIP: DIGER core event\n");
  }
}

STATIC
BOOLEAN
WaitOneSecondOrQuit (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Slice;

  for (Slice = 0; Slice < 10; Slice++) {
    if ((gST->ConIn != NULL) && (gST->ConIn->ReadKeyStroke != NULL)) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status) &&
          ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == L'q') ||
           (Key.UnicodeChar == L'Q'))) {
        return FALSE;
      }
    }
    gBS->Stall (100000);
  }
  return TRUE;
}

STATIC
BOOLEAN
PreflightIsSafe (
  IN EFI_PHYSICAL_ADDRESS  BufferAddress
  )
{
  UINT32  AddressWidth;
  UINT32  CoreId;
  UINT32  Dctl;
  UINT32  Dsts;
  UINT32  EventCount;
  UINT32  Gctl;
  UINT32  Gsts;
  UINT32  HwParams0;
  UINT32  PortCapability;

  CoreId         = Dwc3Read32 (DWC3_GSNPSID);
  Gctl           = Dwc3Read32 (DWC3_GCTL);
  Gsts           = Dwc3Read32 (DWC3_GSTS);
  HwParams0      = Dwc3Read32 (DWC3_GHWPARAMS0);
  Dctl           = Dwc3Read32 (DWC3_DCTL);
  Dsts           = Dwc3Read32 (DWC3_DSTS);
  EventCount     = Dwc3Read32 (DWC3_GEVNTCOUNT0);
  AddressWidth   = HwParams0 >> DWC3_GHWPARAMS0_AWIDTH_SHIFT;
  PortCapability = (Gctl >> DWC3_GCTL_PRTCAP_SHIFT) &
                   DWC3_GCTL_PRTCAP_MASK;

  Print (L"R10 PREFLIGHT ID=%08x GCTL=%08x GSTS=%08x HWP0=%08x AW=%u\n",
         CoreId, Gctl, Gsts, HwParams0, AddressWidth);
  Print (L"DCTL=%08x DSTS=%08x DEVTEN=%08x DALEPENA=%08x\n",
         Dctl, Dsts, Dwc3Read32 (DWC3_DEVTEN),
         Dwc3Read32 (DWC3_DALEPENA));
  Print (L"EVT=%08x:%08x SIZ=%08x CNT=%08x EP0CMD=%08x EP1CMD=%08x\n",
         Dwc3Read32 (DWC3_GEVNTADRHI0),
         Dwc3Read32 (DWC3_GEVNTADRLO0),
         Dwc3Read32 (DWC3_GEVNTSIZ0), EventCount,
         Dwc3Read32 (DWC3_DEPCMD0), Dwc3Read32 (DWC3_DEPCMD1));
  Print (L"AYRILAN EVENT DMA=%lx boyut=%u\n",
         BufferAddress, DWC3_EVENT_BUFFER_SIZE);

  if ((CoreId & DWC3_GSNPSID_MASK) != DWC3_IP_DWC31) {
    Print (L"RED: cekirdek DWC_usb31 degil.\n");
    return FALSE;
  }
  if ((Gctl & DWC3_GCTL_CORESOFTRESET) != 0) {
    Print (L"RED: global core soft-reset aktif.\n");
    return FALSE;
  }
  if ((PortCapability != DWC3_GCTL_PRTCAP_DEVICE) &&
      (PortCapability != DWC3_GCTL_PRTCAP_OTG)) {
    Print (L"RED: port capability device/OTG degil.\n");
    return FALSE;
  }
  if ((Gsts & DWC3_GSTS_CURMOD_MASK) != DWC3_GSTS_CURMOD_DEVICE) {
    Print (L"RED: cekirdek device modunda degil.\n");
    return FALSE;
  }
  if (((Dctl & (DWC3_DCTL_RUN_STOP | DWC3_DCTL_CSFTRST)) != 0) ||
      ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0)) {
    Print (L"RED: device zaten aktif/resetli veya halted degil.\n");
    return FALSE;
  }
  if (Dwc3Read32 (DWC3_DALEPENA) != 0) {
    Print (L"RED: endpoint etkin; stale TRB DMA riski.\n");
    return FALSE;
  }
  if ((Dwc3Read32 (DWC3_DEPCMD0) & DWC3_DEPCMD_CMDACT) != 0 ||
      (Dwc3Read32 (DWC3_DEPCMD1) & DWC3_DEPCMD_CMDACT) != 0) {
    Print (L"RED: EP0/EP1 komutu halen aktif.\n");
    return FALSE;
  }
  if (Dwc3Read32 (DWC3_DEVTEN) != 0) {
    Print (L"RED: kalitilmis device-event enable sifir degil.\n");
    return FALSE;
  }
  if ((EventCount & (DWC3_GEVNTCOUNT_MASK | DWC3_GEVNTCOUNT_EHB)) != 0) {
    Print (L"RED: event buffer count/busy sifir degil.\n");
    return FALSE;
  }
  if (((BufferAddress >> 32) != 0) && (AddressWidth < 64)) {
    Print (L"RED: event DMA 4GiB ustunde fakat core AWIDTH=%u.\n",
           AddressWidth);
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
ShowLiveProbe (
  IN UINTN                         Second,
  IN UINT32                        CountRegister,
  IN CONST B2L_DWC3_EVENT_STATS   *Stats
  )
{
  UINT32  Dctl;
  UINT32  Dsts;

  Dctl = Dwc3Read32 (DWC3_DCTL);
  Dsts = Dwc3Read32 (DWC3_DSTS);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - AKTIF DWC3 EVENT PROBU ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"SANIYE=%u/%u  Q/ESC=guvenli durdur  KABLO PC'YE TAKILI\n",
         Second, DWC3_PROBE_SECONDS);
  Print (L"YAZMA: event DMA + DEVTEN + RUN_STOP. PHY/PMU/rol/reset/EP YOK.\n\n");
  Print (L"DCTL=%08x RUN=%u DSTS=%08x SOFN=%04x\n",
         Dctl, (Dctl & DWC3_DCTL_RUN_STOP) != 0, Dsts,
         (Dsts & DWC3_DSTS_SOFFN_MASK) >> 3);
  Print (L"SPD=%s LINK=%s HLT=%u IDLE=%u RXEMPTY=%u\n",
         Dwc3SpeedName (Dsts), Dwc3LinkStateName (Dsts),
         (Dsts & DWC3_DSTS_DEVCTRLHLT) != 0,
         (Dsts & DWC3_DSTS_COREIDLE) != 0,
         (Dsts & DWC3_DSTS_RXFIFOEMPTY) != 0);
  Print (L"DEVTEN=%08x DALEPENA=%08x EVTCNT=%08x READPOS=%u\n",
         Dwc3Read32 (DWC3_DEVTEN), Dwc3Read32 (DWC3_DALEPENA),
         CountRegister, Stats->ReadPosition);
  Print (L"TOPLAM=%u DEVICE=%u EP=%u DIGER=%u\n",
         Stats->Total, Stats->Total - Stats->EndpointTotal - Stats->OtherTotal,
         Stats->EndpointTotal, Stats->OtherTotal);
  Print (L"DIS=%u RST=%u CONN=%u LINK=%u WAKE=%u SUSP=%u\n",
         Stats->DeviceType[0], Stats->DeviceType[1], Stats->DeviceType[2],
         Stats->DeviceType[3], Stats->DeviceType[4], Stats->DeviceType[6]);
  Print (L"ERR=%u CMD=%u OVF=%u\n",
         Stats->DeviceType[9], Stats->DeviceType[10], Stats->DeviceType[11]);
  PrintLastEvents (Stats);
  Print (L"\nBeklenen ilk kanit: USB_RESET ve/veya CONNECT_DONE sayaci artar.\n");
  Print (L"Bu tur descriptor/EP0 sunmaz; PC'de gecici bilinmeyen aygit olabilir.\n");
}

STATIC
VOID
FailStoppedWithReservedDma (
  IN EFI_PHYSICAL_ADDRESS        BufferAddress,
  IN OUT B2L_DWC3_EVENT_STATS  *Stats
  )
{
  Print (L"\nKRITIK: DWC3 2 saniyede halt olmadi.\n");
  Print (L"DMA sayfasi %lx RESERVED birakildi; Windows'a devam ETME.\n",
         BufferAddress);
  Print (L"Telefonu donanim tusuyla yeniden baslat. Bu ekran bilerek cikmaz.\n");
  for (;;) {
    DrainEventBuffer (BufferAddress, Stats);
    gBS->Stall (100000);
  }
}

EFI_STATUS
B2LDwc3RunEventProbe (
  VOID
  )
{
  B2L_DWC3_EVENT_STATS  Stats;
  B2L_DWC3_PROBE_SAVED Saved;
  EFI_PHYSICAL_ADDRESS BufferAddress;
  EFI_STATUS           Status;
  UINT32               CountRegister;
  UINT32               Dctl;
  UINT32               Dsts;
  UINTN                Millisecond;
  UINTN                Second;

  // Keep the DMA target below 4 GiB even though this core reports AWIDTH=64.
  // That avoids depending on an undocumented Exynos interconnect high-address
  // path during the first active probe.
  BufferAddress = MAX_UINT32;
  Status = gBS->AllocatePages (
                  AllocateMaxAddress,
                  EfiReservedMemoryType,
                  EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE),
                  &BufferAddress
                  );
  if (EFI_ERROR (Status)) {
    Print (L"R10 event DMA sayfasi ayrilamadi: %r\n", Status);
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)BufferAddress, DWC3_EVENT_BUFFER_SIZE);
  ZeroMem (&Stats, sizeof (Stats));
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - AKTIF DWC3 PREFLIGHT ===\n\n",
         B2L_USB_TEST_REVISION);
  if (!PreflightIsSafe (BufferAddress)) {
    gBS->FreePages (
           BufferAddress,
           EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE)
           );
    Print (L"R10 yazma yapmadan iptal edildi.\n");
    return EFI_NOT_READY;
  }

  Saved.EventAddressLow  = Dwc3Read32 (DWC3_GEVNTADRLO0);
  Saved.EventAddressHigh = Dwc3Read32 (DWC3_GEVNTADRHI0);
  Saved.EventSize        = Dwc3Read32 (DWC3_GEVNTSIZ0);
  Saved.DeviceEventEnable = Dwc3Read32 (DWC3_DEVTEN);
  Saved.DeviceControl     = Dwc3Read32 (DWC3_DCTL);

  if (!Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, (UINT32)BufferAddress) ||
      !Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, (UINT32)(BufferAddress >> 32)) ||
      !Dwc3ProbeWrite32 (
         DWC3_GEVNTSIZ0,
         DWC3_GEVNTSIZ_INTMASK |
         (DWC3_EVENT_BUFFER_SIZE & DWC3_GEVNTSIZ_SIZE_MASK)
         ) ||
      !Dwc3ProbeWrite32 (DWC3_DEVTEN, DWC3_PROBE_DEVICE_EVENTS) ||
      !Dwc3ProbeWrite32 (
         DWC3_DCTL,
         Saved.DeviceControl | DWC3_DCTL_RUN_STOP
         )) {
    Print (L"R10 internal write allow-list hatasi.\n");
    Status = EFI_ACCESS_DENIED;
    goto RestoreHaltedController;
  }

  Status = EFI_SUCCESS;
  for (Second = 1; Second <= DWC3_PROBE_SECONDS; Second++) {
    CountRegister = DrainEventBuffer (BufferAddress, &Stats);
    ShowLiveProbe (Second, CountRegister, &Stats);
    if (!WaitOneSecondOrQuit ()) {
      break;
    }
  }

  Dctl = Dwc3Read32 (DWC3_DCTL);
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl & ~DWC3_DCTL_RUN_STOP);
  for (Millisecond = 0; Millisecond < DWC3_STOP_TIMEOUT_MS; Millisecond++) {
    DrainEventBuffer (BufferAddress, &Stats);
    Dsts = Dwc3Read32 (DWC3_DSTS);
    if ((Dsts & DWC3_DSTS_DEVCTRLHLT) != 0) {
      break;
    }
    gBS->Stall (1000);
  }
  if ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0) {
    FailStoppedWithReservedDma (BufferAddress, &Stats);
  }

RestoreHaltedController:
  Dwc3ProbeWrite32 (DWC3_DEVTEN, 0);
  DrainEventBuffer (BufferAddress, &Stats);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, DWC3_GEVNTSIZ_INTMASK);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, 0);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, 0);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, Saved.EventAddressLow);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, Saved.EventAddressHigh);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, Saved.EventSize);
  Dwc3ProbeWrite32 (DWC3_DEVTEN, Saved.DeviceEventEnable);
  Dwc3ProbeWrite32 (DWC3_DCTL, Saved.DeviceControl);
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );
  gBS->FreePages (
         BufferAddress,
         EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE)
         );

  Print (L"\nR10 PROB BITTI: HLT=%u olay=%u; DMA ve registerler geri alindi.\n",
         (Dwc3Read32 (DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) != 0,
         Stats.Total);
  Print (L"Sonraki adim icin bu ekranin ve canli ekranin fotografini cek.\n");
  return Status;
}
