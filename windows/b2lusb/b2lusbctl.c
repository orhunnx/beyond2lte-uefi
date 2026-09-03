#include <initguid.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>

#include "b2lusb_public.h"

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

    if ((ArgumentCount != 2) ||
        ((_wcsicmp(Arguments[1], L"status") != 0) &&
         (_wcsicmp(Arguments[1], L"host") != 0) &&
         (_wcsicmp(Arguments[1], L"device") != 0) &&
         (_wcsicmp(Arguments[1], L"none") != 0) &&
         (_wcsicmp(Arguments[1], L"observe") != 0))) {
        fwprintf(stderr, L"Kullanim: b2lusbctl.exe status|observe|host|device|none\n");
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
        wprintf(L"xHCI startup observer 12 saniye icin kuruldu.\n");
        CloseHandle(Handle);
        return 0;
    }

    Role = B2lUsbRoleNone;
    if (_wcsicmp(Arguments[1], L"host") == 0) {
        Role = B2lUsbRoleHost;
    } else if (_wcsicmp(Arguments[1], L"device") == 0) {
        Role = B2lUsbRoleDevice;
    }

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

    wprintf(L"URS istegi gonderildi: %ls\n", B2lRoleName(Role));
    Sleep(750);
    Result = B2lShowStatus(Handle);
    CloseHandle(Handle);
    return Result;
}
