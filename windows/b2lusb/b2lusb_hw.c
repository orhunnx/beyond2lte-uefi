#include "b2lusb.h"

#define B2L_DWC3_GCTL                 0xC110UL
#define B2L_DWC3_GSTS                 0xC118UL
#define B2L_DWC3_GSNPSID              0xC120UL
#define B2L_DWC3_VER_NUMBER           0xC1A0UL
#define B2L_DWC3_GUSB2PHYCFG0         0xC200UL
#define B2L_DWC3_GUSB3PIPECTL0        0xC2C0UL
#define B2L_DWC3_DCTL                 0xC704UL
#define B2L_DWC3_DSTS                 0xC70CUL

#define B2L_DWC3_ID_MASK              0xFFFF0000UL
#define B2L_DWC3_ID_USB3              0x55330000UL
#define B2L_DWC3_ID_USB31             0x33310000UL
#define B2L_DWC3_ID_USB32             0x33320000UL
#define B2L_DWC3_GCTL_CORESOFTRESET   (1UL << 11)
#define B2L_DWC3_GCTL_PRTCAP_MASK     (3UL << 12)
#define B2L_DWC3_GCTL_PRTCAP_HOST     (1UL << 12)
#define B2L_DWC3_GCTL_PRTCAP_DEVICE   (2UL << 12)
#define B2L_DWC3_GSTS_CURMOD_MASK     3UL
#define B2L_DWC3_GSTS_CURMOD_DEVICE   0UL
#define B2L_DWC3_GSTS_CURMOD_HOST     1UL
#define B2L_DWC3_DCTL_RUN_STOP        (1UL << 31)
#define B2L_DWC3_DSTS_DEVCTRLHLT      (1UL << 22)

#define B2L_XHCI_CAPLENGTH            0x00UL
#define B2L_XHCI_HCSPARAMS1           0x04UL
#define B2L_XHCI_HCSPARAMS2           0x08UL
#define B2L_XHCI_HCSPARAMS3           0x0CUL
#define B2L_XHCI_HCCPARAMS1           0x10UL
#define B2L_XHCI_DBOFF                0x14UL
#define B2L_XHCI_RTSOFF               0x18UL
#define B2L_XHCI_HCCPARAMS2           0x1CUL
#define B2L_XHCI_USBCMD               0x00UL
#define B2L_XHCI_USBSTS               0x04UL
#define B2L_XHCI_PAGESIZE             0x08UL
#define B2L_XHCI_DNCTRL               0x14UL
#define B2L_XHCI_CRCR_LOW             0x18UL
#define B2L_XHCI_CRCR_HIGH            0x1CUL
#define B2L_XHCI_DCBAAP_LOW           0x30UL
#define B2L_XHCI_DCBAAP_HIGH          0x34UL
#define B2L_XHCI_CONFIG               0x38UL
#define B2L_XHCI_PORTSC_BASE          0x400UL
#define B2L_XHCI_PORTSC_STRIDE        0x10UL
#define B2L_XHCI_USBSTS_HCH           (1UL << 0)
#define B2L_XHCI_USBCMD_RUN           (1UL << 0)
#define B2L_XHCI_USBCMD_HCRST         (1UL << 1)
#define B2L_XHCI_USBSTS_HSE           (1UL << 2)
#define B2L_XHCI_USBSTS_CNR           (1UL << 11)
#define B2L_XHCI_CAPLENGTH_MIN        0x20U
#define B2L_XHCI_CAPLENGTH_MAX        0x80U
#define B2L_XHCI_HCC_XECP_SHIFT       16U
#define B2L_XHCI_HCC_XECP_MASK        0xFFFFUL
#define B2L_XHCI_EXT_CAP_DWORDS       12UL
#define B2L_XHCI_DWORD_BYTES          4UL

#define B2L_PHY_LINK_CTRL              0x04UL
#define B2L_PHY_LINK_PORT              0x08UL
#define B2L_PHY_CLKRST                 0x20UL
#define B2L_PHY_DUALPHYSEL             0x28UL
#define B2L_PHY_UTMI                   0x50UL
#define B2L_PHY_HSP                    0x54UL
#define B2L_PHY_HSP_TUNE               0x58UL
#define B2L_PHY_HSP_TEST               0x5CUL

#define B2L_LINK_DIS_QACT_LINKGATE     (1UL << 12)
#define B2L_LINK_DIS_QACT_ID0          (1UL << 11)
#define B2L_LINK_DIS_QACT_VBUS_VALID   (1UL << 10)
#define B2L_LINK_DIS_QACT_BVALID       (1UL << 9)
#define B2L_LINK_FORCE_QACT            (1UL << 8)
#define B2L_LINK_BUS_FILTER_MASK       (0xFUL << 4)

#define B2L_LINK_PORT_U3_DISABLE       (1UL << 9)
#define B2L_LINK_PORT_U2_DISABLE       (1UL << 8)
#define B2L_LINK_PORT_SEL_OCD_U3       (1UL << 3)
#define B2L_LINK_PORT_SEL_OCD_U2       (1UL << 2)

#define B2L_CLKRST_PHY20_SW_RST        (1UL << 13)
#define B2L_CLKRST_PHY20_RST_SEL       (1UL << 12)
#define B2L_CLKRST_PHY30_SW_RST        (1UL << 3)
#define B2L_CLKRST_PHY30_RST_SEL       (1UL << 2)
#define B2L_CLKRST_PORT_RST            (1UL << 1)

#define B2L_DUALPHYSEL_PORT_BITS       ((1UL << 8) | (1UL << 4) | \
                                        (1UL << 1) | (1UL << 0))

#define B2L_UTMI_FORCE_VBUSVALID       (1UL << 5)
#define B2L_UTMI_FORCE_BVALID          (1UL << 4)
#define B2L_UTMI_DP_PULLDOWN           (1UL << 3)
#define B2L_UTMI_DM_PULLDOWN           (1UL << 2)
#define B2L_UTMI_FORCE_SUSPEND         (1UL << 1)
#define B2L_UTMI_FORCE_SLEEP           (1UL << 0)

#define B2L_HSP_VBUSVLDEXTSEL          (1UL << 13)
#define B2L_HSP_VBUSVLDEXT             (1UL << 12)
#define B2L_HSP_EN_UTMISUSPEND         (1UL << 9)
#define B2L_HSP_COMMONONN              (1UL << 8)
#define B2L_HSP_TEST_SIDDQ             (1UL << 24)

#define B2L_HSP_TUNE_TXVREF_MASK       (0xFUL << 28)
#define B2L_HSP_TUNE_TXRES_MASK        (3UL << 21)
#define B2L_HSP_TUNE_TXPREEMPA_MASK    (3UL << 18)
#define B2L_HSP_TUNE_SQRX_MASK         (7UL << 8)
#define B2L_HSP_TUNE_COMPDIS_MASK      (7UL << 0)
#define B2L_HSP_TUNE_TXVREF(Value)     (((Value) & 0xFUL) << 28)
#define B2L_HSP_TUNE_TXRES(Value)      (((Value) & 3UL) << 21)
#define B2L_HSP_TUNE_TXPREEMPA(Value)  (((Value) & 3UL) << 18)
#define B2L_HSP_TUNE_SQRX(Value)       (((Value) & 7UL) << 8)
#define B2L_HSP_TUNE_COMPDIS(Value)    (((Value) & 7UL) << 0)

#define B2L_ROLE_WAIT_ITERATIONS       2000UL
#define B2L_ROLE_WAIT_US               10UL
#define B2L_DWC3_ROLE_RESET_MS          100UL

static ULONG
B2lRead32(
    PUCHAR Base,
    ULONG Offset
    )
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Offset));
}

static VOID
B2lWrite32(
    PUCHAR Base,
    ULONG Offset,
    ULONG Value
    )
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Offset), Value);
}

static BOOLEAN
B2lDwc3IdIsValid(
    ULONG Id
    )
{
    ULONG Family;

    Family = Id & B2L_DWC3_ID_MASK;
    return (Family == B2L_DWC3_ID_USB3) ||
           (Family == B2L_DWC3_ID_USB31) ||
           (Family == B2L_DWC3_ID_USB32);
}

VOID
B2lUsbHardwareObserveXhci(
    PB2L_USB_CONTEXT Context
    )
{
    UCHAR CapLength;
    ULONG UsbCmd;
    ULONG UsbSts;
    ULONG CrcrLow;
    ULONG CrcrHigh;
    ULONG DcbaapLow;
    ULONG DcbaapHigh;
    ULONG Config;

    if (!Context->HardwareReady || (Context->Dwc3Registers == NULL)) {
        return;
    }

    CapLength = READ_REGISTER_UCHAR(
        (volatile UCHAR *)Context->Dwc3Registers
        );
    if ((CapLength < B2L_XHCI_CAPLENGTH_MIN) ||
        (CapLength > B2L_XHCI_CAPLENGTH_MAX) ||
        (((ULONG)CapLength + B2L_XHCI_CONFIG + sizeof(ULONG)) >
            Context->Dwc3Length)) {
        return;
    }

    // These xHCI operational-register reads have no clear-on-read behavior.
    // The bounded observer never changes controller, PHY, PMU or guard state.
    UsbCmd = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_USBCMD
        );
    UsbSts = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_USBSTS
        );
    CrcrLow = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_CRCR_LOW
        );
    CrcrHigh = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_CRCR_HIGH
        );
    DcbaapLow = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_DCBAAP_LOW
        );
    DcbaapHigh = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_DCBAAP_HIGH
        );
    Config = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_CONFIG
        );

    if (Context->XhciObserveSamples == 0) {
        Context->XhciObserveFirstUsbCmd = UsbCmd;
        Context->XhciObserveFirstUsbSts = UsbSts;
    } else if ((UsbCmd != Context->XhciObserveLastUsbCmd) ||
               (UsbSts != Context->XhciObserveLastUsbSts)) {
        ++Context->XhciObserveTransitions;
    }
    ++Context->XhciObserveSamples;
    Context->XhciObserveLastUsbCmd = UsbCmd;
    Context->XhciObserveLastUsbSts = UsbSts;
    Context->XhciObserveUsbCmdOr |= UsbCmd;
    Context->XhciObserveUsbStsOr |= UsbSts;

    if ((UsbCmd & B2L_XHCI_USBCMD_HCRST) != 0) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_HCRST;
    }
    if ((UsbCmd & B2L_XHCI_USBCMD_RUN) != 0) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_RUN;
    }
    if ((UsbSts & B2L_XHCI_USBSTS_HCH) == 0) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_HCH_CLEAR;
    }
    if ((UsbSts & B2L_XHCI_USBSTS_CNR) != 0) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_CNR;
    }
    if ((UsbSts & B2L_XHCI_USBSTS_HSE) != 0) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_HSE;
    }
    if (((CrcrLow | CrcrHigh) != 0) &&
        ((Context->XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CRCR) == 0)) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_CRCR;
        Context->XhciObserveCrcrLow = CrcrLow;
        Context->XhciObserveCrcrHigh = CrcrHigh;
    }
    if (((DcbaapLow | DcbaapHigh) != 0) &&
        ((Context->XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_DCBAAP) == 0)) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_DCBAAP;
        Context->XhciObserveDcbaapLow = DcbaapLow;
        Context->XhciObserveDcbaapHigh = DcbaapHigh;
    }
    if ((Config != 0) &&
        ((Context->XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CONFIG) == 0)) {
        Context->XhciObserveFlags |= B2L_XHCI_OBSERVE_SEEN_CONFIG;
        Context->XhciObserveConfig = Config;
    }
}

static NTSTATUS
B2lWaitDwc3Mode(
    PB2L_USB_CONTEXT Context,
    ULONG Mode
    )
{
    ULONG Index;

    for (Index = 0; Index < B2L_ROLE_WAIT_ITERATIONS; ++Index) {
        if ((B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSTS) &
             B2L_DWC3_GSTS_CURMOD_MASK) == Mode) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(B2L_ROLE_WAIT_US);
    }

    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
B2lResetDwc3RoleCore(
    PB2L_USB_CONTEXT Context
    )
{
    LARGE_INTEGER Delay;
    ULONG Gctl;

    // DWC_usb31 needs its internal clocks synchronized when changing roles.
    // Keep the core in reset for the conservative interval used by the
    // upstream DWC3 role-switch path, then hand a quiescent core to USBXHCI.
    Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
    B2lWrite32(
        Context->Dwc3Registers,
        B2L_DWC3_GCTL,
        Gctl | B2L_DWC3_GCTL_CORESOFTRESET
        );
    if ((B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL) &
         B2L_DWC3_GCTL_CORESOFTRESET) == 0) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Delay.QuadPart = -((LONGLONG)B2L_DWC3_ROLE_RESET_MS * 10LL * 1000LL);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
    B2lWrite32(
        Context->Dwc3Registers,
        B2L_DWC3_GCTL,
        Gctl & ~B2L_DWC3_GCTL_CORESOFTRESET
        );
    if ((B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL) &
         B2L_DWC3_GCTL_CORESOFTRESET) != 0) {
        return STATUS_IO_TIMEOUT;
    }

    ++Context->CoreResetCount;
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lStopDeviceController(
    PB2L_USB_CONTEXT Context
    )
{
    ULONG Dctl;
    ULONG Index;

    Dctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_DCTL);
    if ((Dctl & B2L_DWC3_DCTL_RUN_STOP) != 0) {
        B2lWrite32(
            Context->Dwc3Registers,
            B2L_DWC3_DCTL,
            Dctl & ~B2L_DWC3_DCTL_RUN_STOP
            );
    }

    for (Index = 0; Index < B2L_ROLE_WAIT_ITERATIONS; ++Index) {
        if ((B2lRead32(Context->Dwc3Registers, B2L_DWC3_DSTS) &
             B2L_DWC3_DSTS_DEVCTRLHLT) != 0) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(B2L_ROLE_WAIT_US);
    }

    return STATUS_DEVICE_BUSY;
}

static NTSTATUS
B2lReadXhciUsbStatus(
    PB2L_USB_CONTEXT Context,
    ULONG *UsbStatus
    )
{
    UCHAR CapLength;

    CapLength = READ_REGISTER_UCHAR(
        (volatile UCHAR *)Context->Dwc3Registers
        );
    if ((CapLength < B2L_XHCI_CAPLENGTH_MIN) ||
        (CapLength > B2L_XHCI_CAPLENGTH_MAX)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    *UsbStatus = B2lRead32(
        Context->Dwc3Registers,
        (ULONG)CapLength + B2L_XHCI_USBSTS
        );
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lRequireXhciHalted(
    PB2L_USB_CONTEXT Context
    )
{
    ULONG UsbStatus;
    NTSTATUS Status;

    Status = B2lReadXhciUsbStatus(Context, &UsbStatus);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    return ((UsbStatus & B2L_XHCI_USBSTS_HCH) != 0) ?
        STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}

static VOID
B2lUsb2PhyEnable(
    PB2L_USB_CONTEXT Context,
    B2L_USB_ROLE Role
    )
{
    ULONG Value;

    // DT pmu_mask=0 means bit 0.  Setting it removes USB2 PHY isolation.
    Value = READ_REGISTER_ULONG((volatile ULONG *)Context->PmuUsbPhy);
    WRITE_REGISTER_ULONG((volatile ULONG *)Context->PmuUsbPhy, Value | 1UL);

    // Samsung exynos_cal_usbphy_q_ch(..., 1).
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Value |= B2L_LINK_DIS_QACT_ID0 |
             B2L_LINK_DIS_QACT_VBUS_VALID |
             B2L_LINK_DIS_QACT_BVALID |
             B2L_LINK_DIS_QACT_LINKGATE;
    Value &= ~B2L_LINK_FORCE_QACT;
    KeStallExecutionProcessor(500);
    B2lWrite32(Context->PhyRegisters, B2L_PHY_LINK_CTRL, Value);
    KeStallExecutionProcessor(500);
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Value |= B2L_LINK_FORCE_QACT;
    KeStallExecutionProcessor(500);
    B2lWrite32(Context->PhyRegisters, B2L_PHY_LINK_CTRL, Value);

    // Exynos USB3P1 v3.1 PHY POR high.
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_CLKRST);
    Value |= B2L_CLKRST_PHY20_SW_RST |
             B2L_CLKRST_PHY20_RST_SEL |
             B2L_CLKRST_PHY30_SW_RST |
             B2L_CLKRST_PHY30_RST_SEL;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_CLKRST, Value);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
    Value &= ~(B2L_UTMI_FORCE_SUSPEND |
               B2L_UTMI_FORCE_SLEEP |
               B2L_UTMI_DP_PULLDOWN |
               B2L_UTMI_DM_PULLDOWN);
    B2lWrite32(Context->PhyRegisters, B2L_PHY_UTMI, Value);

    // Stock DT common_block_disable=1.
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP);
    Value |= B2L_HSP_EN_UTMISUSPEND | B2L_HSP_COMMONONN;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP, Value);
    KeStallExecutionProcessor(100);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Value |= B2L_LINK_BUS_FILTER_MASK;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_LINK_CTRL, Value);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
    Value |= B2L_UTMI_FORCE_BVALID | B2L_UTMI_FORCE_VBUSVALID;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_UTMI, Value);
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP);
    Value |= B2L_HSP_VBUSVLDEXTSEL | B2L_HSP_VBUSVLDEXT;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP, Value);

    // Tune values are the device/host columns from beyond2lte.dts.
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP_TUNE);
    Value &= ~(B2L_HSP_TUNE_TXVREF_MASK |
               B2L_HSP_TUNE_TXRES_MASK |
               B2L_HSP_TUNE_TXPREEMPA_MASK |
               B2L_HSP_TUNE_SQRX_MASK |
               B2L_HSP_TUNE_COMPDIS_MASK);
    Value |= B2L_HSP_TUNE_TXVREF(12) |
             B2L_HSP_TUNE_TXRES((Role == B2lUsbRoleHost) ? 1 : 3) |
             B2L_HSP_TUNE_TXPREEMPA(3) |
             B2L_HSP_TUNE_SQRX((Role == B2lUsbRoleHost) ? 5 : 3) |
             B2L_HSP_TUNE_COMPDIS(7);
    B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP_TUNE, Value);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP_TEST);
    Value &= ~B2L_HSP_TEST_SIDDQ;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP_TEST, Value);
    KeStallExecutionProcessor(10);

    // PHY POR low; the USB3 reset is released but its port remains disabled.
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_CLKRST);
    Value |= B2L_CLKRST_PHY20_RST_SEL | B2L_CLKRST_PHY30_RST_SEL;
    Value &= ~(B2L_CLKRST_PHY20_SW_RST |
               B2L_CLKRST_PHY30_SW_RST |
               B2L_CLKRST_PORT_RST);
    B2lWrite32(Context->PhyRegisters, B2L_PHY_CLKRST, Value);
    KeStallExecutionProcessor(75);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_DUALPHYSEL);
    Value &= ~B2L_DUALPHYSEL_PORT_BITS;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_DUALPHYSEL, Value);

    // Stock DT use_io_for_ovc=0.  Hide U3 until combo PCS/PMA is mapped.
    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_PORT);
    Value |= B2L_LINK_PORT_SEL_OCD_U3 |
             B2L_LINK_PORT_SEL_OCD_U2 |
             B2L_LINK_PORT_U3_DISABLE;
    Value &= ~B2L_LINK_PORT_U2_DISABLE;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_LINK_PORT, Value);

    if (Role == B2lUsbRoleHost) {
        Value = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
        Value |= B2L_UTMI_DP_PULLDOWN | B2L_UTMI_DM_PULLDOWN;
        Value &= ~(B2L_UTMI_FORCE_BVALID | B2L_UTMI_FORCE_VBUSVALID);
        B2lWrite32(Context->PhyRegisters, B2L_PHY_UTMI, Value);

        Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP);
        Value &= ~(B2L_HSP_VBUSVLDEXTSEL | B2L_HSP_VBUSVLDEXT);
        B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP, Value);
    }
}

static VOID
B2lUsb2PhyDisable(
    PB2L_USB_CONTEXT Context
    )
{
    ULONG Value;

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
    Value |= B2L_UTMI_FORCE_SUSPEND | B2L_UTMI_FORCE_SLEEP;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_UTMI, Value);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP_TEST);
    Value |= B2L_HSP_TEST_SIDDQ;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_HSP_TEST, Value);

    Value = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Value &= ~B2L_LINK_FORCE_QACT;
    Value |= B2L_LINK_DIS_QACT_ID0 |
             B2L_LINK_DIS_QACT_VBUS_VALID |
             B2L_LINK_DIS_QACT_BVALID |
             B2L_LINK_DIS_QACT_LINKGATE;
    B2lWrite32(Context->PhyRegisters, B2L_PHY_LINK_CTRL, Value);

    Value = READ_REGISTER_ULONG((volatile ULONG *)Context->PmuUsbPhy);
    WRITE_REGISTER_ULONG((volatile ULONG *)Context->PmuUsbPhy, Value & ~1UL);
}

NTSTATUS
B2lUsbHardwareValidate(
    PB2L_USB_CONTEXT Context
    )
{
    ULONG Id;

    if ((Context->Dwc3Registers == NULL) ||
        (Context->PhyRegisters == NULL) ||
        (Context->PmuUsbPhy == NULL)) {
        return STATUS_DEVICE_NOT_READY;
    }

    Id = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSNPSID);
    return B2lDwc3IdIsValid(Id) ? STATUS_SUCCESS :
        STATUS_DEVICE_HARDWARE_ERROR;
}

NTSTATUS
B2lUsbHardwareSetRole(
    PB2L_USB_CONTEXT Context,
    B2L_USB_ROLE Role
    )
{
    ULONG CurrentMode;
    ULONG Gctl;
    NTSTATUS Status;

    Status = B2lUsbHardwareValidate(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if ((Role != B2lUsbRoleNone) && (Role == Context->CurrentRole)) {
        return STATUS_SUCCESS;
    }

    CurrentMode = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSTS) &
        B2L_DWC3_GSTS_CURMOD_MASK;

    if (Role == B2lUsbRoleNone) {
        if (CurrentMode == B2L_DWC3_GSTS_CURMOD_DEVICE) {
            Status = B2lStopDeviceController(Context);
        } else {
            Status = B2lRequireXhciHalted(Context);
        }
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        B2lUsb2PhyDisable(Context);
        return STATUS_SUCCESS;
    }

    if (Role == B2lUsbRoleHost) {
        if (CurrentMode == B2L_DWC3_GSTS_CURMOD_DEVICE) {
            Status = B2lStopDeviceController(Context);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        } else {
            Status = B2lRequireXhciHalted(Context);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }

        B2lUsb2PhyEnable(Context, Role);
        Status = B2lResetDwc3RoleCore(Context);
        if (!NT_SUCCESS(Status)) {
            B2lUsb2PhyDisable(Context);
            return Status;
        }
        Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
        Gctl &= ~B2L_DWC3_GCTL_PRTCAP_MASK;
        Gctl |= B2L_DWC3_GCTL_PRTCAP_HOST;
        B2lWrite32(Context->Dwc3Registers, B2L_DWC3_GCTL, Gctl);
        return B2lWaitDwc3Mode(Context, B2L_DWC3_GSTS_CURMOD_HOST);
    }

    if (Role == B2lUsbRoleDevice) {
        if (CurrentMode == B2L_DWC3_GSTS_CURMOD_HOST) {
            Status = B2lRequireXhciHalted(Context);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        } else {
            Status = B2lStopDeviceController(Context);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }

        B2lUsb2PhyEnable(Context, Role);
        Status = B2lResetDwc3RoleCore(Context);
        if (!NT_SUCCESS(Status)) {
            B2lUsb2PhyDisable(Context);
            return Status;
        }
        Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
        Gctl &= ~B2L_DWC3_GCTL_PRTCAP_MASK;
        Gctl |= B2L_DWC3_GCTL_PRTCAP_DEVICE;
        B2lWrite32(Context->Dwc3Registers, B2L_DWC3_GCTL, Gctl);
        return B2lWaitDwc3Mode(Context, B2L_DWC3_GSTS_CURMOD_DEVICE);
    }

    return STATUS_INVALID_PARAMETER;
}

VOID
B2lUsbHardwareGetStatus(
    PB2L_USB_CONTEXT Context,
    B2L_USB_STATUS *Status
    )
{
    ULONG ExtCapOffset;
    ULONG HccParams1;
    ULONG Index;
    ULONG UsbStatus;

    if (!Context->HardwareReady) {
        return;
    }

    Status->Flags |= B2L_USB_STATUS_HARDWARE_READY |
                     B2L_USB_STATUS_USB2_ONLY |
                     B2L_USB_STATUS_MANUAL_URS;
    Status->CoreResetCount = Context->CoreResetCount;
    if (Context->CoreResetCount != 0) {
        Status->Flags |= B2L_USB_STATUS_CORE_RESET;
    }
    Status->GsnpsId = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSNPSID);
    Status->Dwc3VerNumber = B2lRead32(
        Context->Dwc3Registers,
        B2L_DWC3_VER_NUMBER
        );
    Status->Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
    Status->Gsts = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSTS);
    Status->Dctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_DCTL);
    Status->Dsts = B2lRead32(Context->Dwc3Registers, B2L_DWC3_DSTS);
    Status->Dwc3Gusb2PhyCfg = B2lRead32(
        Context->Dwc3Registers,
        B2L_DWC3_GUSB2PHYCFG0
        );
    Status->Dwc3Gusb3PipeCtl = B2lRead32(
        Context->Dwc3Registers,
        B2L_DWC3_GUSB3PIPECTL0
        );
    Status->PhyLinkCtrl = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Status->PhyClkRst = B2lRead32(Context->PhyRegisters, B2L_PHY_CLKRST);
    Status->PhyUtmi = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
    Status->PhyHsp = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP);
    Status->PhyHspTune = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP_TUNE);
    Status->PmuUsbPhy = READ_REGISTER_ULONG(
        (volatile ULONG *)Context->PmuUsbPhy
        );
    WdfSpinLockAcquire(Context->XhciObserveLock);
    Status->XhciObserveFlags = Context->XhciObserveFlags;
    Status->XhciObserveSamples = Context->XhciObserveSamples;
    Status->XhciObserveTransitions = Context->XhciObserveTransitions;
    Status->XhciObserveFirstUsbCmd = Context->XhciObserveFirstUsbCmd;
    Status->XhciObserveFirstUsbSts = Context->XhciObserveFirstUsbSts;
    Status->XhciObserveLastUsbCmd = Context->XhciObserveLastUsbCmd;
    Status->XhciObserveLastUsbSts = Context->XhciObserveLastUsbSts;
    Status->XhciObserveUsbCmdOr = Context->XhciObserveUsbCmdOr;
    Status->XhciObserveUsbStsOr = Context->XhciObserveUsbStsOr;
    Status->XhciObserveCrcrLow = Context->XhciObserveCrcrLow;
    Status->XhciObserveCrcrHigh = Context->XhciObserveCrcrHigh;
    Status->XhciObserveDcbaapLow = Context->XhciObserveDcbaapLow;
    Status->XhciObserveDcbaapHigh = Context->XhciObserveDcbaapHigh;
    Status->XhciObserveConfig = Context->XhciObserveConfig;
    WdfSpinLockRelease(Context->XhciObserveLock);

    UsbStatus = 0;
    if (NT_SUCCESS(B2lReadXhciUsbStatus(Context, &UsbStatus))) {
        UCHAR CapLength;

        CapLength = READ_REGISTER_UCHAR(
            (volatile UCHAR *)Context->Dwc3Registers
            );
        Status->XhciCapability = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_CAPLENGTH
            );
        Status->XhciHcsParams1 = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_HCSPARAMS1
            );
        Status->XhciHcsParams2 = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_HCSPARAMS2
            );
        Status->XhciHcsParams3 = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_HCSPARAMS3
            );
        HccParams1 = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_HCCPARAMS1
            );
        Status->XhciHccParams1 = HccParams1;
        Status->XhciDbOff = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_DBOFF
            );
        Status->XhciRtsOff = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_RTSOFF
            );
        Status->XhciHccParams2 = B2lRead32(
            Context->Dwc3Registers,
            B2L_XHCI_HCCPARAMS2
            );
        Status->XhciUsbCmd = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_USBCMD
            );
        Status->XhciUsbSts = UsbStatus;
        Status->XhciPageSize = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_PAGESIZE
            );
        Status->XhciDnCtrl = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_DNCTRL
            );
        Status->XhciCrcrLow = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_CRCR_LOW
            );
        Status->XhciCrcrHigh = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_CRCR_HIGH
            );
        Status->XhciDcbaapLow = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_DCBAAP_LOW
            );
        Status->XhciDcbaapHigh = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_DCBAAP_HIGH
            );
        Status->XhciConfig = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_CONFIG
            );
        Status->XhciPortSc1 = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_PORTSC_BASE
            );
        Status->XhciPortSc2 = B2lRead32(
            Context->Dwc3Registers,
            (ULONG)CapLength + B2L_XHCI_PORTSC_BASE +
                B2L_XHCI_PORTSC_STRIDE
            );

        // xECP is a DWORD offset from the xHCI base.  Copy only a bounded
        // read-only window: enough to see the legacy-support block plus the
        // USB2 and USB3 four-DWORD Supported Protocol capabilities without
        // following corrupt links.
        ExtCapOffset = ((HccParams1 >> B2L_XHCI_HCC_XECP_SHIFT) &
                        B2L_XHCI_HCC_XECP_MASK) * B2L_XHCI_DWORD_BYTES;
        if ((ExtCapOffset != 0) &&
            ((ExtCapOffset & (B2L_XHCI_DWORD_BYTES - 1UL)) == 0) &&
            (ExtCapOffset <= Context->Dwc3Length -
                (B2L_XHCI_EXT_CAP_DWORDS * B2L_XHCI_DWORD_BYTES))) {
            Status->XhciExtCapOffset = ExtCapOffset;
            for (Index = 0; Index < B2L_XHCI_EXT_CAP_DWORDS; ++Index) {
                Status->XhciExtCap[Index] = B2lRead32(
                    Context->Dwc3Registers,
                    ExtCapOffset + (Index * B2L_XHCI_DWORD_BYTES)
                    );
            }
        }
        if ((UsbStatus & B2L_XHCI_USBSTS_HCH) != 0) {
            Status->Flags |= B2L_USB_STATUS_XHCI_HALTED;
        }
    }
}
