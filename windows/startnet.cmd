@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE automatic diagnostics
mode con: cols=112 lines=45 >nul 2>&1

rem Bu portta henuz klavye, fare veya dokunmatik girisi yoktur. Dolayisiyla
rem bu betik her seyi kendiliginden calistirir; `pause` veya kullanici
rem girdisi bekleyen bir komut kullanmaz.
set "LOG=%SystemDrive%\B2LWINPE.TXT"
set "DPSCRIPT=%SystemDrive%\B2LDISK.TXT"

echo.
echo === Beyond2LTE WinPE: otomatik tani ===
echo PnP/WMI baslatiliyor; bu asama biraz surebilir.
%SystemRoot%\System32\wpeinit.exe

rem b2lcmd, Windows kullanici-moduna ulasildigini kalici b2lfb kaydina
rem damgalar. Tani komutlarindan once kalmasi kasitlidir.
%SystemRoot%\System32\b2lcmd.exe

> "%DPSCRIPT%" (
  echo list disk
  echo list volume
)

(
  echo === Beyond2LTE WinPE otomatik tani ===
  echo.
  echo [Windows]
  ver
  echo.
  echo [Fiziksel bellek]
  wmic computersystem get TotalPhysicalMemory
  echo.
  echo [Mantiksal birimler]
  wmic logicaldisk get DeviceID,DriveType,FileSystem,FreeSpace,Size,VolumeName
  echo.
  echo [DiskPart: diskler ve birimler]
  diskpart /s "%DPSCRIPT%"
  echo.
  echo [PnP: klavye]
  wmic path Win32_PnPEntity where "PNPClass='Keyboard'" get Name,Status
  echo.
  echo [PnP: fare/dokunmatik HID]
  wmic path Win32_PnPEntity where "PNPClass='Mouse'" get Name,Status
  echo.
  echo [PnP: USB aygitlari]
  wmic path Win32_PnPEntity where "PNPDeviceID like 'USB%%'" get Name,Status,PNPDeviceID
  echo.
  echo [Ag bagdastiricilari]
  wmic path Win32_NetworkAdapter where "PhysicalAdapter=true" get Name,NetConnectionStatus,PNPDeviceID
  echo.
  echo [IP yapilandirmasi]
  ipconfig /all
  echo.
  echo === Tani sonu ===
) > "%LOG%" 2>&1

type "%LOG%"

echo.
echo Rapor yukarida; istem acik kalacak. Giris aygiti gerekmez.
cmd.exe /k
