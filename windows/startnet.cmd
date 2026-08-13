@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE automatic diagnostics
rem Daha yuksek konsol, taninin tek bir fotografla okunabilmesini saglar.
mode con: cols=112 lines=64 >nul 2>&1

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
  echo [UFS: ACPI UFSHCI kimligi ve storufs]
  if exist "%SystemRoot%\System32\pnputil.exe" (
    echo --- UFS0: CID, eslesen surucu ve stack ---
    pnputil /enum-devices /instanceid "ACPI\SMSN9820\0" /deviceids /drivers /services
    echo --- SCSIAdapter sinifi ---
    pnputil /enum-devices /connected /class SCSIAdapter /drivers
    echo --- Tum sorunlu PnP aygitlari ---
    pnputil /enum-devices /connected /problem
  ) else (
    reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820" /s
  )
  echo --- storufs: inbox dosya, DriverStore ve servis kaydi ---
  if exist "%SystemRoot%\System32\drivers\storufs.sys" (
    echo storufs.sys bulundu:
    dir /-c "%SystemRoot%\System32\drivers\storufs.sys"
  ) else (
    echo storufs.sys YOK: bu WinPE imajinda UFS miniportu bulunmuyor olabilir.
  )
  dir /b /s "%SystemRoot%\System32\DriverStore\FileRepository\storufs.inf_*" 2^>nul
  echo --- storufs INF: CC_010901 esleme satirlari ---
  for /d %%D in ("%SystemRoot%\System32\DriverStore\FileRepository\storufs.inf_*") do findstr /i /c:"CC_010901" "%%~fD\storufs.inf" 2^>nul
  reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs" /s
  echo.
  echo === Tani sonu ===
) > "%LOG%" 2>&1

type "%LOG%"

echo.
echo Rapor yukarida; istem acik kalacak. Giris aygiti gerekmez.
cmd.exe /k
