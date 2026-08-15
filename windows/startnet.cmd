@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE UFS
mode con: cols=112 lines=72 >nul 2>&1

rem Klavye/dokunmatik zorunlu degildir; tani ve guvenli CACHE testi otomatiktir.
set "DPSCRIPT=%SystemDrive%\B2LDISK.TXT"
set "DPPOSTSCRIPT=%SystemDrive%\B2LDISK-POST.TXT"
set "B2LUFSDIR=%SystemRoot%\B2LUFS"
set "B2LCACHEPROBE=%SystemDrive%\B2LCACHE-PROBE.TXT"
set "B2LCACHEEXPOSE=%SystemDrive%\B2LCACHE-EXPOSE.TXT"
set "B2LCACHEVOLUMES=%SystemDrive%\B2LCACHE-VOLUMES.TXT"
set "B2LCACHEMOUNT=%SystemDrive%\B2LCACHE-MOUNT.TXT"
set "B2LCACHEVOLREPORT=%SystemDrive%\B2LCACHE-VOL.TXT"

echo.
echo === Beyond2LTE WinPE UFS ===
echo PnP baslatiliyor...
%SystemRoot%\System32\wpeinit.exe
if exist "%SystemRoot%\System32\b2lcmd.exe" "%SystemRoot%\System32\b2lcmd.exe" >nul 2>&1

> "%DPSCRIPT%" (
  echo list disk
  echo list volume
)
> "%DPPOSTSCRIPT%" (
  echo list disk
  echo select disk 0
  echo detail disk
  echo list partition
  echo list volume
)

echo.
echo [Surucu oncesi]
diskpart /s "%DPSCRIPT%"
echo Not: X: WinPE RAM diskidir; dahili UFS degildir.

echo.
echo [B2LUFS kurulumu]
call :B2LUfsInstall
if errorlevel 1 goto :Finish

echo.
echo [Disk ve GPT]
diskpart /s "%DPPOSTSCRIPT%"

echo.
call :B2LUfsSummary

echo.
echo [CACHE: W: baglama ve yaz/geri-oku]
call :B2LUfsMountCache

:Finish
echo.
echo === Tani sonu ===
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
type "%B2LUFSDIR%\manifest.txt"
if not exist "%SystemRoot%\System32\pnputil.exe" (
  echo HATA: PnPUtil yok.
  exit /b 1
)
rem Ayrintili BLS2 kayitlari ekrana dokulmez; gerekirse sonradan okunabilsin.
if exist "%SystemRoot%\System32\wevtutil.exe" wevtutil sl "Microsoft-Windows-Storage-Storport/Operational" /e:true >nul 2>&1
pnputil /add-driver "%B2LUFSDIR%\b2lufs.inf" /install
if errorlevel 1 exit /b 1
pnputil /scan-devices
timeout /t 5 /nobreak >nul 2>&1
exit /b 0

:B2LUfsSummary
set "B2L_LUN_KEY=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0\Device Parameters\B2LUFS"
echo [B2LUFS ozet: Result/Scsi=0, Valid/imzalar=1 beklenir]
pnputil /enum-devices /instanceid "ACPI\SMSN9820\0" /drivers /services
reg query "%B2L_LUN_KEY%" /v SchemaVersion 2>nul
reg query "%B2L_LUN_KEY%" /v HciStatus 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityResult 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityScsiStatus 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityValid 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityBlockBytes 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityRaw 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeResult 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeScsiStatus 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeReturnedBytes 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeValid 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeMbrSignature 2>nul
reg query "%B2L_LUN_KEY%" /v MediaProbeGptSignature 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsEntryCount 2>nul
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
type "%B2LCACHEPROBE%.OUT"
findstr /i /c:"Partition 28" "%B2LCACHEPROBE%.OUT" >nul 2>&1
if errorlevel 1 (
  echo CACHE guvenlik kapisi: Partition 28 yok; yazilmadi.
  exit /b 1
)
findstr /i /c:"600 MB" "%B2LCACHEPROBE%.OUT" >nul 2>&1
if errorlevel 1 (
  echo CACHE guvenlik kapisi: 600 MB boyut kaniti yok; yazilmadi.
  exit /b 1
)

rem Odin'in yazdigi FAT32 icerigi ve bolum sinirlari korunur. Yalniz Android
rem CACHE type GUID'i Windows Basic Data yapilir ve gizli/readonly niteligi acilir.
> "%B2LCACHEEXPOSE%" (
  echo select disk 0
  echo select partition 28
  echo set id=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 override
  echo gpt attributes=0x0000000000000000
  echo rescan
)
diskpart /s "%B2LCACHEEXPOSE%"

> "%B2LCACHEVOLUMES%" echo list volume
diskpart /s "%B2LCACHEVOLUMES%" > "%B2LCACHEVOLUMES%.OUT" 2>&1
type "%B2LCACHEVOLUMES%.OUT"
set "B2LCACHEVOL="
for /f "tokens=2" %%V in ('findstr /i /c:"CACHE" "%B2LCACHEVOLUMES%.OUT"') do set "B2LCACHEVOL=%%V"
if not defined B2LCACHEVOL (
  echo CACHE FAT32 volume etiketi bulunamadi.
  exit /b 1
)

> "%B2LCACHEMOUNT%" (
  echo select volume %B2LCACHEVOL%
  echo attributes volume clear readonly
  echo attributes volume clear hidden
  echo assign letter=W noerr
)
diskpart /s "%B2LCACHEMOUNT%"
if not exist W:\NUL (
  echo CACHE W: olarak baglanamadi.
  exit /b 1
)
vol W: > "%B2LCACHEVOLREPORT%" 2>&1
type "%B2LCACHEVOLREPORT%"
findstr /i /c:"CACHE" "%B2LCACHEVOLREPORT%" >nul 2>&1
if errorlevel 1 (
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
