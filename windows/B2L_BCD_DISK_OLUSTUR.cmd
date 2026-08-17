@echo off
setlocal

fltmc >nul 2>&1
if errorlevel 1 (
  echo.
  echo HATA: Bu betik Yonetici Komut Istemi icinde calistirilmalidir.
  echo Baslat menusunde cmd ara, Yonetici olarak calistir ve tekrar dene.
  echo.
  pause
  exit /b 1
)

set "BCD=%~dp0BCD.disk"
set "READY=%~dp0BCD.disk.ready"
del /f /q "%READY%" "%BCD%" >nul 2>&1

echo.
echo === beyond2lte UFS disk-boot BCD hazirlaniyor ===
echo.
call "%~dp0bcd_olustur.cmd" disk "%BCD%"
if errorlevel 1 goto :hata

if not exist "%BCD%" goto :hata
for %%F in ("%BCD%") do set "BCD_SIZE=%%~zF"
if "%BCD_SIZE%"=="0" goto :hata

>"%READY%" echo B2L_BCD_STAGE_VERSION=1
>>"%READY%" echo B2L_BCD_VARIANT=disk
>>"%READY%" echo B2L_BCD_SIZE=%BCD_SIZE%

echo.
echo BASARILI: %BCD%
echo Bu pencereyi kapatip WSL terminaline donun ve ENTER'a basin.
echo.
pause
exit /b 0

:hata
del /f /q "%READY%" >nul 2>&1
echo.
echo HATA: Disk BCD uretilemedi. Yukaridaki ilk bcdedit hatasini kaydedin.
echo "Secure Boot policy" hatasi varsa derleme bilgisayarinin Secure Boot
echo ayarini gecici kapatip yeniden deneyin; testsigning bu BCD icin zorunlu.
echo.
pause
exit /b 1
