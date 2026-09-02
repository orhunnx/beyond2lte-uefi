#include "b2lusb.h"

#define B2L_DWC3_GCTL                 0xC110UL
#define B2L_DWC3_GSTS                 0xC118UL
#define B2L_DWC3_GSNPSID              0xC120UL
#define B2L_DWC3_DCTL                 0xC704UL
#define B2L_DWC3_DSTS                 0xC70CUL

#define B2L_DWC3_ID_MASK              0xFFFF0000UL
#define B2L_DWC3_ID_USB3              0x55330000UL
#define B2L_DWC3_ID_USB31             0x33310000UL
#define B2L_DWC3_ID_USB32             0x33320000UL
#define B2L_DWC3_GCTL_PRTCAP_MASK     (3UL << 12)
#define B2L_DWC3_GCTL_PRTCAP_HOST     (1UL << 12)
#define B2L_DWC3_GCTL_PRTCAP_DEVICE   (2UL << 12)
#define B2L_DWC3_GSTS_CURMOD_MASK     3UL
#define B2L_DWC3_GSTS_CURMOD_DEVICE   0UL
#define B2L_DWC3_GSTS_CURMOD_HOST     1UL
#define B2L_DWC3_DCTL_RUN_STOP        (1UL << 31)
#define B2L_DWC3_DSTS_DEVCTRLHLT      (1UL << 22)

#define B2L_XHCI_USBSTS               0x04UL
#define B2L_XHCI_USBSTS_HCH           (1UL << 0)
#define B2L_XHCI_CAPLENGTH_MIN        0x20U
#define B2L_XHCI_CAPLENGTH_MAX        0x80U

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
    ULONG UsbStatus;

    if (!Context->HardwareReady) {
        return;
    }

    Status->Flags |= B2L_USB_STATUS_HARDWARE_READY |
                     B2L_USB_STATUS_USB2_ONLY |
                     B2L_USB_STATUS_MANUAL_URS;
    Status->GsnpsId = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSNPSID);
    Status->Gctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GCTL);
    Status->Gsts = B2lRead32(Context->Dwc3Registers, B2L_DWC3_GSTS);
    Status->Dctl = B2lRead32(Context->Dwc3Registers, B2L_DWC3_DCTL);
    Status->Dsts = B2lRead32(Context->Dwc3Registers, B2L_DWC3_DSTS);
    Status->PhyLinkCtrl = B2lRead32(Context->PhyRegisters, B2L_PHY_LINK_CTRL);
    Status->PhyClkRst = B2lRead32(Context->PhyRegisters, B2L_PHY_CLKRST);
    Status->PhyUtmi = B2lRead32(Context->PhyRegisters, B2L_PHY_UTMI);
    Status->PhyHsp = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP);
    Status->PhyHspTune = B2lRead32(Context->PhyRegisters, B2L_PHY_HSP_TUNE);
    Status->PmuUsbPhy = READ_REGISTER_ULONG(
        (volatile ULONG *)Context->PmuUsbPhy
        );

    UsbStatus = 0;
    if (NT_SUCCESS(B2lReadXhciUsbStatus(Context, &UsbStatus))) {
        Status->XhciUsbSts = UsbStatus;
        if ((UsbStatus & B2L_XHCI_USBSTS_HCH) != 0) {
            Status->Flags |= B2L_USB_STATUS_XHCI_HALTED;
        }
    }
}
