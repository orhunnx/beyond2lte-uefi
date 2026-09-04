#include <initguid.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>

#include "b2lusb_public.h"
#include "b2lmax77705_events.h"

#define B2L_XHCI_PORTSC_CCS          (1UL << 0)
#define B2L_XHCI_PORTSC_PED          (1UL << 1)
#define B2L_XHCI_PORTSC_OCA          (1UL << 3)
#define B2L_XHCI_PORTSC_PLS_SHIFT    5U
#define B2L_XHCI_PORTSC_PLS_MASK     0xFUL
#define B2L_XHCI_PORTSC_PP           (1UL << 9)
#define B2L_XHCI_PORTSC_SPEED_SHIFT  10U
#define B2L_XHCI_PORTSC_SPEED_MASK   0xFUL
#define B2L_XHCI_PORTSC_CHANGE_MASK  0x00FE0000UL

static HANDLE
B2lOpenController(VOID)
{
    HDEVINFO DeviceInfo;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    DWORD Required;
    DWORD Error;
    HANDLE Handle;

    DeviceInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_B2L_USB,
        NULL,
        NULL,
        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT
        );
    if (DeviceInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = (DWORD)sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(
            DeviceInfo,
            NULL,
            &GUID_DEVINTERFACE_B2L_USB,
            0,
            &InterfaceData)) {
        Error = GetLastError();
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    Required = 0;
    (VOID)SetupDiGetDeviceInterfaceDetailW(
        DeviceInfo,
        &InterfaceData,
        NULL,
        0,
        &Required,
        NULL
        );
    if (Required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }

    Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        Required
        );
    if (Detail == NULL) {
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    Detail->cbSize = (DWORD)sizeof(*Detail);
    if (!SetupDiGetDeviceInterfaceDetailW(
            DeviceInfo,
            &InterfaceData,
            Detail,
            Required,
            NULL,
            NULL)) {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Detail);
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    Handle = CreateFileW(
        Detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    Error = GetLastError();
    HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
    SetLastError(Error);
    return Handle;
}

static const wchar_t *
B2lRoleName(
    B2L_USB_ROLE Role
    )
{
    if (Role == B2lUsbRoleHost) {
        return L"HOST";
    }
    if (Role == B2lUsbRoleDevice) {
        return L"DEVICE";
    }
    return L"NONE";
}

static const wchar_t *
B2lPortLinkStateName(
    ULONG PortLinkState
    )
{
    static const wchar_t *Names[16] = {
        L"U0", L"U1", L"U2", L"U3",
        L"Disabled", L"RxDetect", L"Inactive", L"Polling",
        L"Recovery", L"HotReset", L"Compliance", L"Test",
        L"Reserved12", L"Reserved13", L"Reserved14", L"Resume"
    };

    return Names[PortLinkState & B2L_XHCI_PORTSC_PLS_MASK];
}

static VOID
B2lShowPortStatus(
    ULONG PortNumber,
    ULONG PortSc
    )
{
    ULONG PortLinkState;
    ULONG Speed;

    PortLinkState = (PortSc >> B2L_XHCI_PORTSC_PLS_SHIFT) &
        B2L_XHCI_PORTSC_PLS_MASK;
    Speed = (PortSc >> B2L_XHCI_PORTSC_SPEED_SHIFT) &
        B2L_XHCI_PORTSC_SPEED_MASK;
    wprintf(
        L"xHCI port%lu raw=%08lX ccs=%lu ped=%lu pp=%lu oca=%lu pls=%lu(%ls) speed=%lu change=%08lX\n",
        PortNumber,
        PortSc,
        (ULONG)((PortSc & B2L_XHCI_PORTSC_CCS) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_PED) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_PP) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_OCA) != 0),
        PortLinkState,
        B2lPortLinkStateName(PortLinkState),
        Speed,
        PortSc & B2L_XHCI_PORTSC_CHANGE_MASK
        );
}

static int
B2lShowStatus(
    HANDLE Handle
    )
{
    B2L_USB_STATUS Status;
    DWORD Returned;

    ZeroMemory(&Status, sizeof(Status));
    Returned = 0;
    if (!DeviceIoControl(
            Handle,
            IOCTL_B2L_USB_GET_STATUS,
            NULL,
            0,
            &Status,
            (DWORD)sizeof(Status),
            &Returned,
            NULL)) {
        fwprintf(stderr, L"GET_STATUS basarisiz: Win32=%lu\n", GetLastError());
        return 1;
    }
    if ((Returned != (DWORD)sizeof(Status)) ||
        (Status.Version != B2L_USB_IOCTL_VERSION) ||
        (Status.Size != (ULONG)sizeof(Status))) {
        fwprintf(stderr, L"GET_STATUS surum/boyut uyusmazligi\n");
        return 1;
    }

    wprintf(L"requested=%ls current=%ls last_ntstatus=0x%08lX flags=0x%08lX\n",
        B2lRoleName(Status.RequestedRole),
        B2lRoleName(Status.CurrentRole),
        (ULONG)Status.LastRoleStatus,
        Status.Flags);
    wprintf(L"DWC3 id=%08lX ver=%08lX resets=%lu\n",
        Status.GsnpsId, Status.Dwc3VerNumber, Status.CoreResetCount);
    wprintf(L"DWC3 gctl=%08lX gsts=%08lX dctl=%08lX dsts=%08lX\n",
        Status.Gctl, Status.Gsts, Status.Dctl, Status.Dsts);
    wprintf(L"DWC3 gusb2=%08lX gusb3=%08lX\n",
        Status.Dwc3Gusb2PhyCfg, Status.Dwc3Gusb3PipeCtl);
    wprintf(L"xHCI cap=%08lX hcs1=%08lX hcc1=%08lX\n",
        Status.XhciCapability, Status.XhciHcsParams1, Status.XhciHccParams1);
    wprintf(L"xHCI hcs2=%08lX hcs3=%08lX hcc2=%08lX\n",
        Status.XhciHcsParams2, Status.XhciHcsParams3, Status.XhciHccParams2);
    wprintf(L"xHCI db=%08lX rts=%08lX xecp=%08lX\n",
        Status.XhciDbOff, Status.XhciRtsOff, Status.XhciExtCapOffset);
    wprintf(L"xHCI cmd=%08lX sts=%08lX page=%08lX\n",
        Status.XhciUsbCmd, Status.XhciUsbSts, Status.XhciPageSize);
    wprintf(L"xHCI dn=%08lX cfg=%08lX crcr=%08lX%08lX\n",
        Status.XhciDnCtrl, Status.XhciConfig,
        Status.XhciCrcrHigh, Status.XhciCrcrLow);
    wprintf(L"xHCI dcbaap=%08lX%08lX portsc=%08lX/%08lX\n",
        Status.XhciDcbaapHigh, Status.XhciDcbaapLow,
        Status.XhciPortSc1, Status.XhciPortSc2);
    B2lShowPortStatus(1, Status.XhciPortSc1);
    B2lShowPortStatus(2, Status.XhciPortSc2);
    wprintf(L"xECP +00=%08lX +04=%08lX +08=%08lX +0C=%08lX\n",
        Status.XhciExtCap[0], Status.XhciExtCap[1],
        Status.XhciExtCap[2], Status.XhciExtCap[3]);
    wprintf(L"xECP +10=%08lX +14=%08lX +18=%08lX +1C=%08lX\n",
        Status.XhciExtCap[4], Status.XhciExtCap[5],
        Status.XhciExtCap[6], Status.XhciExtCap[7]);
    wprintf(L"xECP +20=%08lX +24=%08lX +28=%08lX +2C=%08lX\n",
        Status.XhciExtCap[8], Status.XhciExtCap[9],
        Status.XhciExtCap[10], Status.XhciExtCap[11]);
    wprintf(L"xHCI obs flags=%08lX samples=%lu transitions=%lu\n",
        Status.XhciObserveFlags, Status.XhciObserveSamples,
        Status.XhciObserveTransitions);
    wprintf(L"xHCI obs first=%08lX/%08lX last=%08lX/%08lX or=%08lX/%08lX\n",
        Status.XhciObserveFirstUsbCmd, Status.XhciObserveFirstUsbSts,
        Status.XhciObserveLastUsbCmd, Status.XhciObserveLastUsbSts,
        Status.XhciObserveUsbCmdOr, Status.XhciObserveUsbStsOr);
    wprintf(L"xHCI obs seen reset=%lu run=%lu hch0=%lu cnr=%lu hse=%lu ring=%lu dcbaa=%lu cfg=%lu\n",
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HCRST) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_RUN) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HCH_CLEAR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CNR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HSE) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CRCR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_DCBAAP) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CONFIG) != 0));
    wprintf(L"xHCI obs ring=%08lX%08lX dcbaa=%08lX%08lX cfg=%08lX\n",
        Status.XhciObserveCrcrHigh, Status.XhciObserveCrcrLow,
        Status.XhciObserveDcbaapHigh, Status.XhciObserveDcbaapLow,
        Status.XhciObserveConfig);
    wprintf(L"PHY link=%08lX clkrst=%08lX utmi=%08lX\n",
        Status.PhyLinkCtrl, Status.PhyClkRst, Status.PhyUtmi);
    wprintf(L"PHY hsp=%08lX tune=%08lX  PMU[1586072C]=%08lX\n",
        Status.PhyHsp, Status.PhyHspTune, Status.PmuUsbPhy);
    return 0;
}

static int
B2lShowMaxEvents(VOID)
{
    B2L_MAX_EVENT_LOG Log;
    const B2L_MAX_EVENT *Event;
    DWORD Size = (DWORD)sizeof(Log);
    DWORD Type = 0;
    LSTATUS Error;
    ULONG Order;
    ULONG Index;
    ULONG TailCount;

    // One read of one binary value: no per-field process or registry loop,
    // and no dependency on BYN0002 being ready to expose its interface.
    ZeroMemory(&Log, sizeof(Log));
    Error = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705",
        B2L_MAX_EVENT_VALUE, RRF_RT_REG_BINARY, &Type, &Log, &Size);
    if (Error != ERROR_SUCCESS) {
        fwprintf(stderr, L"MAX_EVENT_READ_ERROR=%ld\n", Error);
        return 1;
    }
    if ((Type != REG_BINARY) || (Size != (DWORD)sizeof(Log)) ||
        (Log.Size != (ULONG)sizeof(Log)) ||
        (Log.Version != B2L_MAX_EVENT_VERSION) ||
        (Log.Count > B2L_MAX_EVENT_CAPACITY) ||
        (Log.NextTail >= B2L_MAX_EVENT_TAIL)) {
        fwprintf(stderr, L"MAX_EVENT_FORMAT_ERROR\n");
        return 1;
    }
    wprintf(L"MAX_EVENTS v=%lu count=%lu total=%llu overwritten=%llu\n",
        Log.Version, Log.Count, Log.Total, Log.Overwritten);
    wprintf(L"MAX_EVENT_START_100NS=%llu OPCODE_UIC=%02lX\n",
        Log.StartTime, Log.OpcodeUicSeen);
    wprintf(L"MAX_SYSMSG_IRQS=%lu ATTACH_ERR_IRQS=%lu SNAP_FAIL=%lu\n",
        Log.SysmsgEvents, Log.AttachErrorEvents, Log.SnapshotFailures);
    wprintf(L"origin: 1=baseline 2=opcode-drain 3=opcode-wait 4=poll\n");
    wprintf(L"        5=policy-CC-read-failed (policy unavailable)\n");
    wprintf(L"irq=VDM/PD/CC/UIC (only UIC for 2/3); policy=CC1/CC0\n");
    wprintf(L"raw=USBC1/SYSMSG/BC/FWMIN/CC0/CC1/PD0/PD1; v=0 invalid\n");
    for (Order = 0UL; Order < Log.Count; ++Order) {
        Index = Order;
        if ((Order >= B2L_MAX_EVENT_FIRST) &&
            (Log.Count == B2L_MAX_EVENT_CAPACITY)) {
            TailCount = Order - B2L_MAX_EVENT_FIRST;
            Index = B2L_MAX_EVENT_FIRST +
                ((Log.NextTail + TailCount) % B2L_MAX_EVENT_TAIL);
        }
        Event = &Log.Events[Index];
        wprintf(L"#%llu t=%llu ms o=%lu op=%02lX irq=%08lX policy=%08lX\n",
            Event->Sequence, (Event->Time - Log.StartTime) / 10000ULL,
            Event->Origin, Event->Opcode, Event->Interrupts, Event->PolicyCc);
        wprintf(L"  v=%lu nt=%08lX raw=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X\n",
            Event->Valid, Event->ReadStatus,
            (unsigned int)Event->Status[0], (unsigned int)Event->Status[1],
            (unsigned int)Event->Status[2], (unsigned int)Event->Status[3],
            (unsigned int)Event->Status[4], (unsigned int)Event->Status[5],
            (unsigned int)Event->Status[6], (unsigned int)Event->Status[7]);
        if (Order == Log.Count - 1UL) {
            wprintf(L"MAX_LAST v=%lu SYSMSG=%02X CC=%02X/%02X\n",
                Event->Valid, (unsigned int)Event->Status[1],
                (unsigned int)Event->Status[4], (unsigned int)Event->Status[5]);
        }
    }
    return 0;
}

int
wmain(
    int ArgumentCount,
    wchar_t **Arguments
    )
{
    B2L_USB_SET_ROLE_INPUT Input;
    B2L_USB_ROLE Role;
    HANDLE Handle;
    DWORD Returned;
    int Result;

    if ((ArgumentCount == 2) && (_wcsicmp(Arguments[1], L"maxevents") == 0)) {
        return B2lShowMaxEvents();
    }
    if ((ArgumentCount != 2) ||
        ((_wcsicmp(Arguments[1], L"status") != 0) &&
         (_wcsicmp(Arguments[1], L"host") != 0) &&
         (_wcsicmp(Arguments[1], L"observe") != 0))) {
        fwprintf(stderr, L"Kullanim: b2lusbctl.exe status|observe|host|maxevents\n");
        return 2;
    }

    Handle = B2lOpenController();
    if (Handle == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"BYN0002 arayuzu acilamadi: Win32=%lu\n", GetLastError());
        return 1;
    }

    if (_wcsicmp(Arguments[1], L"status") == 0) {
        Result = B2lShowStatus(Handle);
        CloseHandle(Handle);
        return Result;
    }

    if (_wcsicmp(Arguments[1], L"observe") == 0) {
        Returned = 0;
        if (!DeviceIoControl(
                Handle,
                IOCTL_B2L_USB_ARM_XHCI_OBSERVER,
                NULL,
                0,
                NULL,
                0,
                &Returned,
                NULL)) {
            fwprintf(stderr, L"XHCI_OBSERVER basarisiz: Win32=%lu\n",
                GetLastError());
            CloseHandle(Handle);
            return 1;
        }
        wprintf(L"xHCI startup observer 5 saniye icin kuruldu.\n");
        CloseHandle(Handle);
        return 0;
    }

    Role = B2lUsbRoleHost;

    ZeroMemory(&Input, sizeof(Input));
    Input.Version = B2L_USB_IOCTL_VERSION;
    Input.Size = (ULONG)sizeof(Input);
    Input.Role = Role;
    Returned = 0;
    if (!DeviceIoControl(
            Handle,
            IOCTL_B2L_USB_SET_ROLE,
            &Input,
            (DWORD)sizeof(Input),
            NULL,
            0,
            &Returned,
            NULL)) {
        fwprintf(stderr, L"SET_ROLE basarisiz: Win32=%lu\n", GetLastError());
        CloseHandle(Handle);
        return 1;
    }

    wprintf(L"Statik HOST durumu dogrulandi.\n");
    Result = B2lShowStatus(Handle);
    CloseHandle(Handle);
    return Result;
}
