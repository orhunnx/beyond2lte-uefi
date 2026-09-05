@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE - UFS disk boot

rem WinPE kabugunu telefonda okunabilir tut.
reg.exe add "HKCU\Control Panel\Desktop" /v Win8DpiScaling /t REG_DWORD /d 1 /f >nul 2>&1
reg.exe add "HKCU\Control Panel\Desktop" /v LogPixels /t REG_DWORD /d 168 /f >nul 2>&1
mode con: cols=64 lines=41 >nul 2>&1

set "B2LUFSDIR=%SystemRoot%\B2LUFS"
set "B2LTOUCHDIR=%SystemRoot%\B2LTOUCH"

echo.
echo === Beyond2LTE WinPE - UFS disk boot ===
echo PnP baslatiliyor...
%SystemRoot%\System32\wpeinit.exe

rem Android RAW bolumlerinin surucu harflerini tuketmesini engelle.
if exist "%SystemRoot%\System32\mountvol.exe" mountvol /N >nul 2>&1

echo.
echo [B2LUFS boot-start]
call :B2LUfsVerifyBootStart
if errorlevel 1 goto :BootFailure

echo.
echo [B2LTOUCH/VHF]
call :B2LTouchVerify
if errorlevel 1 echo UYARI: Touch dogrulamasi basarisiz; UFS disk boot etkilenmedi.

echo.
echo === Diskten WinPE basarili ===
echo B2LUFS boot-start ve UFS/NTFS sistem birimi kullaniliyor.
echo Multi-touch testi: %B2LTOUCHDIR%\b2lmt.exe
cmd.exe /k
goto :eof

:BootFailure
echo.
echo HATA: B2LUFS boot-start dogrulamasi basarisiz.
echo Diskten acilis gerceklesti; bu ekran kaydini koruyun.
cmd.exe /k
goto :eof

:B2LUfsVerifyBootStart
if not exist "%B2LUFSDIR%\manifest.txt" (
  echo HATA: B2LUFS manifest yok.
  exit /b 1
)
if not exist "%B2LUFSDIR%\offline-driverstore.txt" (
  echo HATA: B2LUFS offline DriverStore isareti yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\b2lufs.sys" (
  echo HATA: b2lufs.sys System32\drivers altinda yok.
  exit /b 1
)
%SystemRoot%\System32\find.exe "B2L_BOOT_START=1" "%B2LUFSDIR%\offline-driverstore.txt" >nul 2>&1
if errorlevel 1 (
  echo HATA: B2LUFS boot-start marker gecersiz.
  exit /b 1
)

set "B2LUFSSTART="
for /f "tokens=3" %%V in ('reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lufs" /v Start 2^>nul ^| "%SystemRoot%\System32\find.exe" "REG_DWORD"') do set "B2LUFSSTART=%%V"
if /i not "%B2LUFSSTART%"=="0x0" (
  echo HATA: b2lufs Start=0 degil ^(bulunan: %B2LUFSSTART%^).
  exit /b 1
)

set "B2LUFSGROUP="
for /f "tokens=2,*" %%V in ('reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lufs" /v Group 2^>nul ^| "%SystemRoot%\System32\find.exe" "REG_SZ"') do set "B2LUFSGROUP=%%W"
if /i not "%B2LUFSGROUP%"=="SCSI Miniport" (
  echo HATA: b2lufs grubu SCSI Miniport degil ^(bulunan: %B2LUFSGROUP%^).
  exit /b 1
)

echo B2LUFS_START=%B2LUFSSTART%
echo B2LUFS_GROUP=%B2LUFSGROUP%
echo B2LUFS_SYS=VAR
exit /b 0

:B2LTouchVerify
if not exist "%B2LTOUCHDIR%\b2ltouch.inf" (
  echo B2LTOUCH paketi yok; atlandi.
  exit /b 0
)
if not exist "%B2LTOUCHDIR%\offline-driverstore.txt" (
  echo HATA: B2LTOUCH offline DriverStore isareti yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\vhf.sys" (
  echo HATA: VHF_SYS=YOK
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\vhf" >nul 2>&1
if errorlevel 1 (
  echo HATA: VHF_SERVICE=YOK
  exit /b 1
)

echo VHF_SYS=VAR
echo VHF_SERVICE=VAR
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch" /v B2LTouchStage 2>nul || echo B2L_STAGE=YOK
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch" /v B2LTouchStatus 2>nul || echo B2L_STATUS=YOK
exit /b 0
