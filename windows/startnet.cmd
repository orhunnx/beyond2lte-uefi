@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE automatic diagnostics
rem Yatayda yazinin kuculmemesi icin genislik sabit; uzun fotograf icin yukseklik buyuk.
mode con: cols=112 lines=144 >nul 2>&1

rem Bu portta henuz klavye, fare veya dokunmatik girisi yoktur. Dolayisiyla
rem bu betik her seyi kendiliginden calistirir; `pause` veya kullanici
rem girdisi bekleyen bir komut kullanmaz.
set "DPSCRIPT=%SystemDrive%\B2LDISK.TXT"

echo.
echo === Beyond2LTE WinPE: otomatik tani ===
echo PnP/WMI baslatiliyor; bu asama biraz surebilir.
%SystemRoot%\System32\wpeinit.exe

rem b2lcmd istege bagli bir b2lfb damgasidir; UFS tanisinin parcasi degildir.
rem Bu WIM'e enjekte edilmediyse sessizce atlanir.
if exist "%SystemRoot%\System32\b2lcmd.exe" "%SystemRoot%\System32\b2lcmd.exe" >nul 2>&1

> "%DPSCRIPT%" (
  echo list disk
  echo list volume
)

rem Cikti dogrudan konsola gider: RAM-disk gunlugu resetten sonra kalmaz;
rem aradaki iki saniye fotograf cekmek icindir. Tum komutlar salt-okunurdur.
echo.
echo === Beyond2LTE WinPE otomatik tani ===
echo.
echo [Windows]
ver
call :PhotoPause

echo.
echo [DiskPart: diskler ve birimler - salt okunur]
diskpart /s "%DPSCRIPT%"
echo Not: X: WinPE'nin boot.wim RAM diski; dahili UFS diski degil.
call :PhotoPause

echo.
echo [PnP: klavye/fare/USB/ag]
if exist "%SystemRoot%\System32\pnputil.exe" (
  pnputil /enum-devices /connected /class Keyboard /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /class Mouse /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /bus USB /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /class Net /drivers
  call :PhotoPause
) else (
  echo PnPUtil yok; HID/USB agaci olculemedi.
  reg query "HKLM\SYSTEM\CurrentControlSet\Enum\HID" /s
  call :PhotoPause
  reg query "HKLM\SYSTEM\CurrentControlSet\Enum\USB" /s
  call :PhotoPause
)

echo.
echo [IP yapilandirmasi]
ipconfig /all
call :PhotoPause

echo.
echo [UFS: ACPI UFSHCI kimligi ve storufs]
call :UfsReport

echo.
echo --- storufs: inbox dosya, DriverStore ve servis kaydi ---
if exist "%SystemRoot%\System32\drivers\storufs.sys" (
  echo storufs.sys bulundu:
  dir /-c "%SystemRoot%\System32\drivers\storufs.sys"
) else (
  echo storufs.sys YOK: bu WinPE imajinda UFS miniportu bulunmuyor olabilir.
)
call :PhotoPause
dir /b /s "%SystemRoot%\System32\DriverStore\FileRepository\storufs.inf_*" 2>nul
call :PhotoPause
echo --- storufs servis kaydi ---
rem /s yerine ilgili dort anahtari ayri goster: uzun/ikili alt agac yok.
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs"
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\Parameters" 2>nul
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\Enum" 2>nul
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\StartOverride" 2>nul
call :PhotoPause

echo.
echo === Tani sonu ===

echo.
echo UFS kaydini sonraki UEFI ekraninda gostermek icin hemen yazilimsal
echo yeniden baslatma yapiliyor. Telefonu kapatma veya guc kesme.
%SystemRoot%\System32\wpeutil.exe reboot

rem wpeutil normalde bu noktaya donmez. Donerse ekrani kaybetmeden bekle;
rem kullaniciya soguk yeniden baslatma yaptirmak DRAM kaydini siler.
echo UYARI: wpeutil reboot dondu; otomatik reset istegi tamamlanmadi.
cmd.exe /k
goto :eof

:UfsReport
rem UFS raporu ana betikteki parantezli PnP raporundan bilerek ayridir.
rem Cihazdaki onceki ekranda bu basliktan hemen sonra ana betik bitti.
rem Foto tek basina sebebi ayirt etmiyor; bu alt yordam o belirsiz ana
rem blok akisini ortadan kaldirir ve her iki yolu exit /b ile ana rapora dondurur.
if not exist "%SystemRoot%\System32\pnputil.exe" goto :UfsWithoutPnP

echo --- UFS0: CID, eslesen surucu ve stack ---
pnputil /enum-devices /instanceid "ACPI\SMSN9820\0" /deviceids /drivers /services
call :PhotoPause
echo --- UFS0: yalniz PnP kok degerleri; Properties agaci atlandi ---
rem /s, Properties altindaki ikili verileri yuzlerce satir yaziyordu.
rem Kimlik/servis zaten yukaridaki PnPUtil satirinda gorunuyor.
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0\Control" 2>nul
call :PhotoPause
echo --- SCSIAdapter sinifi ---
pnputil /enum-devices /connected /class SCSIAdapter /drivers
call :PhotoPause
echo --- Tum sorunlu PnP aygitlari ---
pnputil /enum-devices /connected /problem
call :PhotoPause
exit /b 0

:UfsWithoutPnP
rem Eski WinPE'de de Properties alt agacini dokme.
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820"
call :PhotoPause
exit /b 0

:PhotoPause
if exist "%SystemRoot%\System32\timeout.exe" (
  "%SystemRoot%\System32\timeout.exe" /t 2 /nobreak >nul 2>&1
) else (
  ping.exe -n 3 127.0.0.1 >nul 2>&1
)
exit /b
