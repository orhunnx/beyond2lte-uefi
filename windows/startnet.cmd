@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE UFS
mode con: cols=112 lines=72 >nul 2>&1

rem Klavye/dokunmatik zorunlu degildir; guvenli CACHE yazma testi otomatiktir.
set "B2LUFSDIR=%SystemRoot%\B2LUFS"
set "B2LCACHEPROBE=%SystemDrive%\B2LCACHE-PROBE.TXT"
set "B2LCACHEVOLUMES=%SystemDrive%\B2LCACHE-VOLUMES.TXT"
set "B2LCACHEMOUNT=%SystemDrive%\B2LCACHE-MOUNT.TXT"
set "B2LCACHEVOLREPORT=%SystemDrive%\B2LCACHE-VOL.TXT"

echo.
echo === Beyond2LTE WinPE UFS ===
echo PnP baslatiliyor...
%SystemRoot%\System32\wpeinit.exe

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
pnputil /add-driver "%B2LUFSDIR%\b2lufs.inf" /install > "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
if errorlevel 1 (
  type "%SystemDrive%\B2LUFS-PNP.TXT"
  exit /b 1
)
pnputil /scan-devices >> "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
timeout /t 5 /nobreak >nul 2>&1
exit /b 0

:B2LUfsMountCache
rem Stok beyond2lte GPT'sinde CACHE tam olarak disk 0 / partition 28 / 600 MiB.
rem Bu uc kanit olmadan GPT'ye veya medyaya yazilmaz.
> "%B2LCACHEPROBE%" (
  echo select disk 0
  echo list partition
  echo select partition 28
  echo detail partition
)
diskpart /s "%B2LCACHEPROBE%" > "%B2LCACHEPROBE%.OUT" 2>&1
findstr /i /c:"Partition 28" "%B2LCACHEPROBE%.OUT" >nul 2>&1
if errorlevel 1 (
  type "%B2LCACHEPROBE%.OUT"
  echo CACHE guvenlik kapisi: Partition 28 yok; yazilmadi.
  exit /b 1
)
findstr /i /c:"600 MB" "%B2LCACHEPROBE%.OUT" >nul 2>&1
if errorlevel 1 (
  type "%B2LCACHEPROBE%.OUT"
  echo CACHE guvenlik kapisi: 600 MB boyut kaniti yok; yazilmadi.
  exit /b 1
)

rem Surucu CACHE girdisini yalniz Windows'a Basic Data/attributes=0 olarak
rem gosterir. Samsung GPT medyada degismez; burada GPT yazma komutu yoktur.

> "%B2LCACHEVOLUMES%" echo list volume
diskpart /s "%B2LCACHEVOLUMES%" > "%B2LCACHEVOLUMES%.OUT" 2>&1
set "B2LCACHEVOL="
for /f "tokens=2" %%V in ('findstr /i /c:"CACHE" "%B2LCACHEVOLUMES%.OUT"') do set "B2LCACHEVOL=%%V"
if not defined B2LCACHEVOL (
  type "%B2LCACHEVOLUMES%.OUT"
  echo CACHE FAT32 volume etiketi bulunamadi.
  exit /b 1
)

rem Daha once otomatik atanmis bir RAW W: varsa yalniz mount pointini kaldir.
rem Medyaya, GPT'ye veya dosya sistemine yazmaz.
if exist "%SystemRoot%\System32\mountvol.exe" mountvol W: /D >nul 2>&1

> "%B2LCACHEMOUNT%" (
  echo select volume %B2LCACHEVOL%
  echo attributes volume clear readonly
  echo attributes volume clear hidden
  echo assign letter=W noerr
)
diskpart /s "%B2LCACHEMOUNT%" > "%B2LCACHEMOUNT%.OUT" 2>&1
if not exist W:\NUL (
  type "%B2LCACHEMOUNT%.OUT"
  echo CACHE W: olarak baglanamadi.
  exit /b 1
)
vol W: > "%B2LCACHEVOLREPORT%" 2>&1
findstr /i /c:"CACHE" "%B2LCACHEVOLREPORT%" >nul 2>&1
if errorlevel 1 (
  type "%B2LCACHEVOLREPORT%"
  echo W: etiketi CACHE degil; yazma atlandi.
  exit /b 1
)

> W:\B2LUFS-WRITE-OK.TXT echo B2LUFS CACHE WRITE OK
if errorlevel 1 (
  echo CACHE yazma basarisiz.
  exit /b 1
)
findstr /x /c:"B2LUFS CACHE WRITE OK" W:\B2LUFS-WRITE-OK.TXT >nul 2>&1
if errorlevel 1 (
  echo CACHE geri-okuma basarisiz.
  exit /b 1
)
echo CACHE basarili: W:\B2LUFS-WRITE-OK.TXT yazildi ve geri okundu.
exit /b 0
