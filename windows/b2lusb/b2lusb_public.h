#ifndef B2LUSB_PUBLIC_H
#define B2LUSB_PUBLIC_H

#include <guiddef.h>

// {43EB6F1D-81B9-4E9A-93D4-76C7E02C1BF4}
DEFINE_GUID(
    GUID_DEVINTERFACE_B2L_USB,
    0x43eb6f1d, 0x81b9, 0x4e9a, 0x93, 0xd4, 0x76, 0xc7, 0xe0, 0x2c, 0x1b, 0xf4
    );

#define B2L_USB_IOCTL_VERSION 1UL

typedef enum _B2L_USB_ROLE {
    B2lUsbRoleNone = 0,
    B2lUsbRoleHost = 1,
    B2lUsbRoleDevice = 2
} B2L_USB_ROLE;

#define B2L_USB_STATUS_HARDWARE_READY 0x00000001UL
#define B2L_USB_STATUS_USB2_ONLY      0x00000002UL
#define B2L_USB_STATUS_MANUAL_URS     0x00000004UL
#define B2L_USB_STATUS_XHCI_HALTED    0x00000008UL

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
    ULONG GsnpsId;
    ULONG Gctl;
    ULONG Gsts;
    ULONG Dctl;
    ULONG Dsts;
    ULONG XhciUsbSts;
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
