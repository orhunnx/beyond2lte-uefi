@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE UFS + Touch
mode con: cols=112 lines=72 >nul 2>&1

rem Klavye/dokunmatik zorunlu degildir; guvenli CACHE yazma testi otomatiktir.
set "B2LUFSDIR=%SystemRoot%\B2LUFS"
set "B2LTOUCHDIR=%SystemRoot%\B2LTOUCH"
set "B2LCACHEMOUNT=%SystemDrive%\B2LCACHE-MOUNT.TXT"
set "B2LCACHEVOLREPORT=%SystemDrive%\B2LCACHE-VOL.TXT"

echo.
echo === Beyond2LTE WinPE UFS ===
echo PnP baslatiliyor...
%SystemRoot%\System32\wpeinit.exe

echo.
echo [B2LTOUCH kurulumu]
call :B2LTouchInstall

rem Android RAW bolumlerinin C:-Z: harflerini tuketmesini engelle. Bu ayar
rem yalniz calisan WinPE oturumunun mount manager durumunu degistirir.
if exist "%SystemRoot%\System32\mountvol.exe" mountvol /N >nul 2>&1

echo.
echo [B2LUFS kurulumu]
call :B2LUfsInstall
if errorlevel 1 goto :Finish

echo.
echo [CACHE yaz/geri-oku]
call :B2LUfsMountCache

:Finish
echo.
echo === Test sonu ===
echo Yazilimsal yeniden baslatma bu platformda donuyor; burada bekleniyor.
cmd.exe /k
goto :eof

:B2LTouchInstall
if not exist "%B2LTOUCHDIR%\b2ltouch.inf" (
  echo B2LTOUCH paketi bu WIM'de yok; atlandi.
  exit /b 0
)
if not exist "%B2LTOUCHDIR%\manifest.txt" (
  echo HATA: B2LTOUCH manifest yok; yari paket reddedildi.
  exit /b 1
)
echo Paket kimligi:
type "%B2LTOUCHDIR%\manifest.txt"
call :B2LTouchEnsureVhfService
if errorlevel 1 exit /b 1
pnputil /add-driver "%B2LTOUCHDIR%\b2ltouch.inf" /install > "%SystemDrive%\B2LTOUCH-PNP.TXT" 2>&1
if errorlevel 1 (
  type "%SystemDrive%\B2LTOUCH-PNP.TXT"
  exit /b 1
)
pnputil /scan-devices >> "%SystemDrive%\B2LTOUCH-PNP.TXT" 2>&1
timeout /t 3 /nobreak >nul 2>&1
echo PnP kurulum sonucu:
type "%SystemDrive%\B2LTOUCH-PNP.TXT"
pnputil /enum-devices /deviceid "ACPI\BYN0001" 2>nul
echo.
echo B2LTOUCH VHF/PnP tani:
if exist "%SystemRoot%\System32\drivers\vhf.sys" (
  echo VHF_SYS=VAR
) else (
  echo VHF_SYS=YOK
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\vhf" >nul 2>&1
if errorlevel 1 (
  echo VHF_SERVICE=YOK
) else (
  echo VHF_SERVICE=VAR
  reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\vhf" 2>nul
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch" /v B2LDriverStatus 2>nul || echo B2L_DRIVER_STATUS=YOK
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\BYN0001\0" /v LowerFilters 2>nul || echo LOWER_FILTERS=YOK
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch" /v B2LTouchStage 2>nul || echo B2L_STAGE=YOK
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch" /v B2LTouchStatus 2>nul || echo B2L_STATUS=YOK
echo Asama: 01=AddEntry 02=DeviceCreate 10=Device 20=Timer 30=VhfCreate 40=VhfStart 50=Resources 60-64=sec_ts 70=D0Ready
exit /b 0

:B2LTouchEnsureVhfService
set "B2LVHFSVC=HKLM\SYSTEM\CurrentControlSet\Services\vhf"
reg.exe query "%B2LVHFSVC%" >nul 2>&1
if not errorlevel 1 exit /b 0
if not exist "%SystemRoot%\System32\drivers\vhf.sys" (
  echo HATA: Microsoft VHF surucusu yok: %SystemRoot%\System32\drivers\vhf.sys
  exit /b 1
)
echo WinPE VHF dosyasini iceriyor fakat servis kaydi yok; oturumluk kayit olusturuluyor...
> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" echo VHF servis kaydi reg.exe ile olusturuluyor
reg.exe add "%B2LVHFSVC%" /v Type /t REG_DWORD /d 1 /f >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
reg.exe add "%B2LVHFSVC%" /v Start /t REG_DWORD /d 3 /f >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
reg.exe add "%B2LVHFSVC%" /v ErrorControl /t REG_DWORD /d 1 /f >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
reg.exe add "%B2LVHFSVC%" /v ImagePath /t REG_EXPAND_SZ /d "\SystemRoot\System32\drivers\vhf.sys" /f >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
reg.exe add "%B2LVHFSVC%" /v DisplayName /t REG_SZ /d "Virtual HID Framework (VHF) Driver" /f >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
reg.exe query "%B2LVHFSVC%" >> "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT" 2>&1
if errorlevel 1 goto :B2LTouchVhfServiceFail
type "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT"
exit /b 0

:B2LTouchVhfServiceFail
type "%SystemDrive%\B2LTOUCH-VHF-SERVICE.TXT"
exit /b 1

:B2LUfsInstall
if not exist "%B2LUFSDIR%\b2lufs.inf" (
  echo HATA: B2LUFS paketi WIM'de yok.
  exit /b 1
)
if not exist "%B2LUFSDIR%\manifest.txt" (
  echo HATA: B2LUFS manifest yok; eski veya yari paketlenmis WIM reddedildi.
  exit /b 1
)
if not exist "%SystemRoot%\System32\pnputil.exe" (
  echo HATA: PnPUtil yok.
  exit /b 1
)
echo Paket kimligi:
type "%B2LUFSDIR%\manifest.txt"
echo.
echo PnP surucusu kuruluyor ve aygitlar yeniden taraniyor...
pnputil /add-driver "%B2LUFSDIR%\b2lufs.inf" /install > "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
if errorlevel 1 (
  type "%SystemDrive%\B2LUFS-PNP.TXT"
  exit /b 1
)
pnputil /scan-devices >> "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
timeout /t 5 /nobreak >nul 2>&1
echo.
echo PnP kurulum ve tarama sonucu:
type "%SystemDrive%\B2LUFS-PNP.TXT"
exit /b 0

:B2LUfsMountCache
rem Automount kapaliyken CACHE volume Healthy/Offline kalir. Stok haritadaki
rem Disk 0 / Partition 28'e odaklan, iliskili volume'u online yap ve W: ata.
rem Daha once atanmis bir W: varsa yalniz mount pointini kaldir.
if exist "%SystemRoot%\System32\mountvol.exe" mountvol W: /D >nul 2>&1

> "%B2LCACHEMOUNT%" (
  echo select disk 0
  echo select partition 28
  echo online volume noerr
  echo assign letter=W noerr
)
diskpart /s "%B2LCACHEMOUNT%" > "%B2LCACHEMOUNT%.OUT" 2>&1
echo.
echo DiskPart: Disk 0 / Partition 28 online ve W: atama sonucu:
type "%B2LCACHEMOUNT%.OUT"
if not exist W:\NUL (
  echo CACHE guvenlik kapisi: Disk 0 / Partition 28 online edilip W: olarak baglanamadi.
  exit /b 1
)
vol W: > "%B2LCACHEVOLREPORT%" 2>&1
echo.
echo W: volume bilgisi:
type "%B2LCACHEVOLREPORT%"
%SystemRoot%\System32\find.exe /i "CACHE" < "%B2LCACHEVOLREPORT%" >nul 2>&1
if errorlevel 1 (
  echo W: etiketi CACHE degil; yazma atlandi.
  exit /b 1
)
echo CACHE etiketi dogrulandi.

echo.
echo Yaziliyor: W:\B2LUFS-WRITE-OK.TXT
> W:\B2LUFS-WRITE-OK.TXT echo B2LUFS CACHE WRITE OK
if errorlevel 1 (
  echo CACHE yazma basarisiz.
  exit /b 1
)
if not exist W:\B2LUFS-WRITE-OK.TXT (
  echo CACHE yazma basarisiz: dosya olusmadi.
  exit /b 1
)
set "B2LCACHEREAD="
set /p "B2LCACHEREAD="<W:\B2LUFS-WRITE-OK.TXT
echo Geri okunan: %B2LCACHEREAD%
if not "%B2LCACHEREAD%"=="B2LUFS CACHE WRITE OK" (
  echo CACHE geri-okuma basarisiz.
  exit /b 1
)
echo CACHE basarili: W:\B2LUFS-WRITE-OK.TXT yazildi ve geri okundu.
exit /b 0
