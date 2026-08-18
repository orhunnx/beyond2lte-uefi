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
set "REQUEST=%~dp0request.txt"
set "REQUEST_ID="
del /f /q "%READY%" "%BCD%" >nul 2>&1

if not exist "%REQUEST%" (
  echo HATA: WSL tarafinin request.txt dosyasi bulunamadi.
  goto :hata
)
for /f "tokens=2 delims==" %%I in ('findstr /b "B2L_BCD_REQUEST_ID=" "%REQUEST%"') do set "REQUEST_ID=%%I"
if not defined REQUEST_ID (
  echo HATA: request.txt icinde B2L_BCD_REQUEST_ID yok.
  goto :hata
)

echo.
echo === beyond2lte UFS disk-boot BCD hazirlaniyor ===
echo.
call "%~dp0bcd_olustur.cmd" disk "%BCD%"
if errorlevel 1 goto :hata

if not exist "%BCD%" goto :hata
for %%F in ("%BCD%") do set "BCD_SIZE=%%~zF"
if "%BCD_SIZE%"=="0" goto :hata

:: Yalniz bcdedit /set basarisina guvenme; uretilen hive'i yeniden acip iki
:: ayri aygit sozlesmesini geri oku. Etiketler yerellestirilebilir, fakat
:: "device"/"osdevice" oge adlari ve degerleri BCD semasinin parcasidir.
set "DEVICE_OK="
set "OSDEVICE_OK="
for /f "tokens=1,*" %%A in ('bcdedit /store "%BCD%" /enum all ^| findstr /b /i "device"') do if /i "%%B"=="locate=\Windows\System32\Boot\winload.efi" set "DEVICE_OK=1"
for /f "tokens=1,*" %%A in ('bcdedit /store "%BCD%" /enum all ^| findstr /b /i "osdevice"') do if /i "%%B"=="locate=\Windows" set "OSDEVICE_OK=1"
if not defined DEVICE_OK (
  echo HATA: BCD device, winload.efi locate degerine geri okunamadi.
  goto :hata
)
if not defined OSDEVICE_OK (
  echo HATA: BCD osdevice, \Windows sistem kokune geri okunamadi.
  goto :hata
)

>"%READY%" echo B2L_BCD_STAGE_VERSION=2
>>"%READY%" echo B2L_BCD_VARIANT=disk
>>"%READY%" echo B2L_BCD_REQUEST_ID=%REQUEST_ID%
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
