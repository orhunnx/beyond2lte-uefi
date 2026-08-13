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
  echo [DiskPart: diskler ve birimler]
  diskpart /s "%DPSCRIPT%"
  echo.
  echo [PnP: klavye/fare/USB/ag]
  if exist "%SystemRoot%\System32\pnputil.exe" (
    pnputil /enum-devices /connected /class Keyboard /drivers
    pnputil /enum-devices /connected /class Mouse /drivers
    pnputil /enum-devices /connected /bus USB /drivers
    pnputil /enum-devices /connected /class Net /drivers
  ) else (
    echo PnPUtil yok; HID/USB agaci olculemedi.
    reg query "HKLM\SYSTEM\CurrentControlSet\Enum\HID" /s
    reg query "HKLM\SYSTEM\CurrentControlSet\Enum\USB" /s
  )
  echo.
  echo [IP yapilandirmasi]
  ipconfig /all
  echo.
  echo [UFS: ACPI UFSHCI ve storufs]
  if exist "%SystemRoot%\System32\pnputil.exe" (
    pnputil /enum-devices /connected /class SCSIAdapter /drivers
    pnputil /enum-devices /connected /problem
  ) else (
    reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820" /s
  )
  sc.exe query storufs
  echo.
  echo === Tani sonu ===
) > "%LOG%" 2>&1

type "%LOG%"

echo.
echo Rapor yukarida; istem acik kalacak. Giris aygiti gerekmez.
cmd.exe /k
