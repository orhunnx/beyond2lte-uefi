@echo off
setlocal

fltmc >nul 2>&1
if errorlevel 1 (
  echo.
  echo HATA: Bu betik Yonetici Komut Istemi icinde calistirilmalidir.
  echo.
  pause
  exit /b 1
)

set "STAGE=%~dp0"
set "WIM=%STAGE%boot.wim"
set "INF=%STAGE%b2lufs.inf"
set "MOUNT=%STAGE%mount"
set "REQUEST=%STAGE%request.txt"
set "READY=%STAGE%ready.txt"
set "REPOSITORY=%MOUNT%\Windows\System32\DriverStore\FileRepository"
set "MOUNTED=0"
set "B2L_INF="
set "B2L_DIR="

del /f /q "%READY%" >nul 2>&1
if not exist "%WIM%" goto :eksik
if not exist "%INF%" goto :eksik
if not exist "%STAGE%b2lufs.sys" goto :eksik
if not exist "%STAGE%b2lufs.cat" goto :eksik
if not exist "%REQUEST%" goto :eksik

if exist "%MOUNT%\Windows" (
  dism.exe /Unmount-Image /MountDir:"%MOUNT%" /Discard
  if errorlevel 1 goto :hata
)
if exist "%MOUNT%" rmdir /s /q "%MOUNT%"
mkdir "%MOUNT%" || goto :hata

echo.
echo === B2LUFS paketi offline WinPE DriverStore'a kuruluyor ===
echo.
dism.exe /Mount-Image /ImageFile:"%WIM%" /Index:1 /MountDir:"%MOUNT%"
if errorlevel 1 goto :hata
set "MOUNTED=1"

dism.exe /Image:"%MOUNT%" /Add-Driver /Driver:"%INF%" /ForceUnsigned
if errorlevel 1 goto :hata

for /f "delims=" %%F in ('where.exe /r "%REPOSITORY%" b2lufs.inf 2^>nul') do set "B2L_INF=%%~fF"
if not defined B2L_INF (
  echo HATA: DISM basarili dondu ama b2lufs.inf DriverStore'da bulunamadi.
  goto :hata
)
for %%F in ("%B2L_INF%") do set "B2L_DIR=%%~dpF"
echo   DriverStore paketi: %B2L_DIR%
if not exist "%B2L_DIR%b2lufs.sys" (
  echo HATA: DriverStore paketinde b2lufs.sys bulunamadi.
  goto :hata
)
if not exist "%B2L_DIR%b2lufs.cat" (
  echo HATA: DriverStore paketinde b2lufs.cat bulunamadi.
  goto :hata
)

dism.exe /Unmount-Image /MountDir:"%MOUNT%" /Commit
if errorlevel 1 goto :hata
set "MOUNTED=0"

copy /y "%REQUEST%" "%READY%" >nul || goto :hata
for %%F in ("%WIM%") do >>"%READY%" echo B2L_DISM_WIM_SIZE=%%~zF

echo.
echo BASARILI: B2LUFS DriverStore'a kuruldu ve boot.wim kaydedildi.
echo WSL terminaline donup ENTER'a basin.
echo.
pause
exit /b 0

:eksik
echo.
echo HATA: DISM sahne klasorunde boot.wim veya B2LUFS paketi eksik.
goto :son

:hata
echo.
echo HATA: B2LUFS offline DriverStore kurulumu basarisiz.
if "%MOUNTED%"=="1" dism.exe /Unmount-Image /MountDir:"%MOUNT%" /Discard

:son
del /f /q "%READY%" >nul 2>&1
echo Yukaridaki ilk DISM hatasini kaydedin.
echo.
pause
exit /b 1
