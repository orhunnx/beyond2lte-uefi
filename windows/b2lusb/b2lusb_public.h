#ifndef B2LUSB_PUBLIC_H
#define B2LUSB_PUBLIC_H

#include <guiddef.h>

// {43EB6F1D-81B9-4E9A-93D4-76C7E02C1BF4}
DEFINE_GUID(
    GUID_DEVINTERFACE_B2L_USB,
    0x43eb6f1d, 0x81b9, 0x4e9a, 0x93, 0xd4, 0x76, 0xc7, 0xe0, 0x2c, 0x1b, 0xf4
    );

#define B2L_USB_IOCTL_VERSION 4UL

typedef enum _B2L_USB_ROLE {
    B2lUsbRoleNone = 0,
    B2lUsbRoleHost = 1,
    B2lUsbRoleDevice = 2
} B2L_USB_ROLE;

#define B2L_USB_STATUS_HARDWARE_READY 0x00000001UL
#define B2L_USB_STATUS_USB2_ONLY      0x00000002UL
#define B2L_USB_STATUS_MANUAL_URS     0x00000004UL
#define B2L_USB_STATUS_XHCI_HALTED    0x00000008UL
#define B2L_USB_STATUS_CORE_RESET     0x00000010UL

typedef struct _B2L_USB_SET_ROLE_INPUT {
    ULONG Version;
    ULONG Size;
    B2L_USB_ROLE Role;
} B2L_USB_SET_ROLE_INPUT;

typedef struct _B2L_USB_STATUS {
    ULONG Version;
    ULONG Size;
    B2L_USB_ROLE RequestedRole;
    B2L_USB_ROLE CurrentRole;
    LONG LastRoleStatus;
    ULONG Flags;
    ULONG CoreResetCount;
    ULONG GsnpsId;
    ULONG Dwc3VerNumber;
    ULONG Gctl;
    ULONG Gsts;
    ULONG Dctl;
    ULONG Dsts;
    ULONG Dwc3Gusb2PhyCfg;
    ULONG Dwc3Gusb3PipeCtl;
    ULONG XhciCapability;
    ULONG XhciHcsParams1;
    ULONG XhciHcsParams2;
    ULONG XhciHcsParams3;
    ULONG XhciHccParams1;
    ULONG XhciHccParams2;
    ULONG XhciDbOff;
    ULONG XhciRtsOff;
    ULONG XhciUsbCmd;
    ULONG XhciUsbSts;
    ULONG XhciPageSize;
    ULONG XhciDnCtrl;
    ULONG XhciCrcrLow;
    ULONG XhciCrcrHigh;
    ULONG XhciDcbaapLow;
    ULONG XhciDcbaapHigh;
    ULONG XhciConfig;
    ULONG XhciPortSc1;
    ULONG XhciPortSc2;
    ULONG XhciExtCapOffset;
    ULONG XhciExtCap[12];
    ULONG PhyLinkCtrl;
    ULONG PhyClkRst;
    ULONG PhyUtmi;
    ULONG PhyHsp;
    ULONG PhyHspTune;
    ULONG PmuUsbPhy;
} B2L_USB_STATUS;

#define IOCTL_B2L_USB_GET_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_B2L_USB_SET_ROLE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#endif
