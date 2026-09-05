@echo off
setlocal
title beyond2lte CACHE BCD Desktop gecisi
color 1F
mode con: cols=72 lines=32 >nul 2>&1

echo.
echo === CACHE BCD - OTOMATIK DESKTOP GECISI ===
echo Klavye gerekmez. CACHE partitionina flash atilmayacak.
echo.

%SystemRoot%\System32\wpeinit.exe
if exist "%SystemRoot%\System32\mountvol.exe" mountvol /N >nul 2>&1
mountvol W: /D >nul 2>&1

set "DPSCRIPT=%SystemDrive%\B2L-DESKTOP-DISKPART.TXT"
set "DPLOG=%SystemDrive%\B2L-DESKTOP-DISKPART-OUT.TXT"
set "BCDLOG=%SystemDrive%\B2L-DESKTOP-BCD.TXT"
> "%DPSCRIPT%" (
  echo select disk 0
  echo select partition 28
  echo online volume noerr
  echo assign letter=W noerr
)

echo CACHE baglaniyor: Disk 0 / Partition 28 -^> W:
diskpart /s "%DPSCRIPT%" > "%DPLOG%" 2>&1
if not exist W:\NUL goto :mount_fail

vol W: > "%SystemDrive%\B2L-DESKTOP-VOL.TXT" 2>&1
%SystemRoot%\System32\find.exe /i "CACHE" < "%SystemDrive%\B2L-DESKTOP-VOL.TXT" >nul 2>&1
if errorlevel 1 goto :label_fail

set "BCD=W:\EFI\Microsoft\Boot\BCD"
if not exist "%BCD%" goto :bcd_missing
if not exist "%BCD%.winpe.bak" copy /y "%BCD%" "%BCD%.winpe.bak" >nul || goto :backup_fail

echo BCD Desktop profiline cevriliyor...
bcdedit /store "%BCD%" /deletevalue {default} winpe > "%BCDLOG%" 2>&1
rem Daha once gecis yapildiysa deletevalue hata verebilir; enum kapisi karar verir.
bcdedit /store "%BCD%" /set {default} path \Windows\System32\winload.efi >> "%BCDLOG%" 2>&1
if errorlevel 1 goto :bcd_write_fail
bcdedit /store "%BCD%" /enum {default} >> "%BCDLOG%" 2>&1
if errorlevel 1 goto :bcd_read_fail

findstr /b /i /c:"winpe" "%BCDLOG%" >nul 2>&1
if not errorlevel 1 goto :winpe_still_set
%SystemRoot%\System32\find.exe /i "\Windows\System32\winload.efi" < "%BCDLOG%" >nul 2>&1
if errorlevel 1 goto :path_missing

> W:\B2L-DESKTOP-BCD-READY.TXT (
  echo B2L_DESKTOP_BCD_READY=1
  echo B2L_CACHE_FLASHED=0
  echo B2L_WINPE_VALUE=REMOVED
  echo B2L_LOADER_PATH=\Windows\System32\winload.efi
)
if not exist W:\B2L-DESKTOP-BCD-READY.TXT goto :marker_fail

color 2F
cls
echo.
echo ================================================================
echo   BASARILI: CACHE BCD DESKTOP PROFILINE CEVRILDI
echo ================================================================
echo.
echo   CACHE flash edilmedi; BCD yerinde guncellendi.
echo   Telefon 8 saniye sonra kapanacak.
echo   Normal acma; dogrudan Download Mode ve Odin'e gec.
echo.
type "%BCDLOG%"
ping 127.0.0.1 -n 9 >nul
wpeutil shutdown
goto :eof

:mount_fail
set "WHY=Disk 0 / Partition 28 W: olarak baglanamadi"
goto :fail
:label_fail
set "WHY=W: biriminin etiketi CACHE degil"
goto :fail
:bcd_missing
set "WHY=CACHE icinde EFI\Microsoft\Boot\BCD yok"
goto :fail
:backup_fail
set "WHY=BCD.winpe.bak olusturulamadi"
goto :fail
:bcd_write_fail
set "WHY=BCD normal winload.efi yolu yazilamadi"
goto :fail
:bcd_read_fail
set "WHY=BCD geri okunamadi"
goto :fail
:winpe_still_set
set "WHY=BCD enum ciktisinda winpe hala etkin"
goto :fail
:path_missing
set "WHY=BCD enum ciktisinda normal winload.efi yolu yok"
goto :fail
:marker_fail
set "WHY=CACHE basari isareti yazilamadi"
goto :fail

:fail
color 4F
cls
echo.
echo ================================================================
echo   HATA: CACHE BCD GECISI TAMAMLANAMADI
echo ================================================================
echo.
echo   %WHY%
echo   CACHE BCD yedegi varsa: EFI\Microsoft\Boot\BCD.winpe.bak
echo   Ekranin fotografini cek. Telefon otomatik kapanmayacak.
echo.
if exist "%DPLOG%" type "%DPLOG%"
if exist "%BCDLOG%" type "%BCDLOG%"
:hold
ping 127.0.0.1 -n 30 >nul
goto :hold
