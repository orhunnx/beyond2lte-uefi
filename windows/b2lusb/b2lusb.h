#ifndef B2LUSB_H
#define B2LUSB_H

#include <ntddk.h>
#include <wdf.h>

#include "b2lusb_public.h"

#define B2L_DWC3_PHYSICAL_BASE 0x10C00000ULL
#define B2L_DWC3_LENGTH        0x00010000UL
#define B2L_USBPHY_PHYSICAL_BASE 0x10B00000ULL
#define B2L_USBPHY_LENGTH        0x00000200UL
#define B2L_USBPMU_PHYSICAL_BASE 0x1586072CULL
#define B2L_USBPMU_LENGTH        0x00000004UL
#define B2L_XHCI_OBSERVE_PERIOD_MS 20UL
#define B2L_XHCI_OBSERVE_WINDOW_100NS (5ULL * 1000ULL * 10000ULL)

typedef struct _B2L_USB_CONTEXT {
    WDFDEVICE Device;
    WDFWAITLOCK RoleLock;
    WDFSPINLOCK XhciObserveLock;
    WDFTIMER XhciObserveTimer;
    PUCHAR Dwc3Registers;
    PUCHAR PhyRegisters;
    PULONG PmuUsbPhy;
    ULONG Dwc3Length;
    ULONG PhyLength;
    ULONG CoreResetCount;
    BOOLEAN HardwareReady;
    B2L_USB_ROLE RequestedRole;
    B2L_USB_ROLE CurrentRole;
    NTSTATUS LastRoleStatus;
    ULONGLONG XhciObserveDeadline;
    ULONG XhciObserveFlags;
    ULONG XhciObserveSamples;
    ULONG XhciObserveTransitions;
    ULONG XhciObserveFirstUsbCmd;
    ULONG XhciObserveFirstUsbSts;
    ULONG XhciObserveLastUsbCmd;
    ULONG XhciObserveLastUsbSts;
    ULONG XhciObserveUsbCmdOr;
    ULONG XhciObserveUsbStsOr;
    ULONG XhciObserveCrcrLow;
    ULONG XhciObserveCrcrHigh;
    ULONG XhciObserveDcbaapLow;
    ULONG XhciObserveDcbaapHigh;
    ULONG XhciObserveConfig;
} B2L_USB_CONTEXT, *PB2L_USB_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(B2L_USB_CONTEXT, B2lUsbGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lUsbEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lUsbEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lUsbEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lUsbEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT B2lUsbEvtD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL B2lUsbEvtIoDeviceControl;
EVT_WDF_TIMER B2lUsbEvtXhciObserveTimer;

NTSTATUS B2lUsbHardwareSetRole(
    PB2L_USB_CONTEXT Context,
    B2L_USB_ROLE Role
    );

NTSTATUS B2lUsbHardwareValidate(
    PB2L_USB_CONTEXT Context
    );

VOID B2lUsbHardwareGetStatus(
    PB2L_USB_CONTEXT Context,
    B2L_USB_STATUS *Status
    );

VOID B2lUsbHardwareObserveXhci(
    PB2L_USB_CONTEXT Context
    );

#endif
