@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE USB + UFS + Touch
rem 96 DPI = yuzde 100; 168 DPI = yuzde 175. Offline DEFAULT hive ana ayari yapar;
rem bu iki deger WinPE SYSTEM kullanicisi ve sonradan acilan araclar icin de
rem ayni olcegi korur. 112x72'nin 1/1.75'i olan izgara okunabilirligi artirir.
reg.exe add "HKCU\Control Panel\Desktop" /v Win8DpiScaling /t REG_DWORD /d 1 /f >nul 2>&1
reg.exe add "HKCU\Control Panel\Desktop" /v LogPixels /t REG_DWORD /d 168 /f >nul 2>&1
mode con: cols=64 lines=41 >nul 2>&1

rem Klavye/dokunmatik zorunlu degildir; guvenli CACHE yazma testi otomatiktir.
set "B2LUFSDIR=%SystemRoot%\B2LUFS"
set "B2LTOUCHDIR=%SystemRoot%\B2LTOUCH"
set "B2LUSBDIR=%SystemRoot%\B2LUSB"
set "B2LTOUCHREADY=0"
set "B2LUSBTRACEACTIVE=0"
set "B2LCACHEMOUNT=%SystemDrive%\B2LCACHE-MOUNT.TXT"
set "B2LCACHEVOLREPORT=%SystemDrive%\B2LCACHE-VOL.TXT"

echo.
echo === Beyond2LTE WinPE UFS ===
echo PnP baslatiliyor...
%SystemRoot%\System32\wpeinit.exe

echo.
echo [B2LUSB kurulumu]
call :B2LUsbInstall
if errorlevel 1 (
  set "B2LUSBFAILED=1"
  echo UYARI: B2LUSB basarisiz; touch ve UFS tanisi yine de surdurulecek.
)

echo.
echo [B2LTOUCH kurulumu]
call :B2LTouchInstall

rem Android RAW bolumlerinin C:-Z: harflerini tuketmesini engelle. Bu ayar
rem yalniz calisan WinPE oturumunun mount manager durumunu degistirir.
if exist "%SystemRoot%\System32\mountvol.exe" mountvol /N >nul 2>&1

echo.
echo [B2LUFS boot-start dogrulamasi]
call :B2LUfsVerifyBootStart
if errorlevel 1 goto :Finish

echo.
echo [CACHE yaz/geri-oku]
call :B2LUfsMountCache
if not errorlevel 1 call :B2LUsbExportLogs

:Finish
if exist "%B2LUSBDIR%\auto-host.test" cls
echo.
echo === Test sonu ===
echo Yazilimsal yeniden baslatma bu platformda donuyor; burada bekleniyor.
if exist "%B2LUSBDIR%\b2lusbctl.exe" echo USB host kontrolu: %B2LUSBDIR%\b2lusbctl.exe status^|observe
if not exist "%B2LUSBDIR%\auto-host.test" call :B2LTouchLaunchKeyboard
if exist "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT" (
  if exist "%SystemDrive%\B2LUSB-USB-SUMMARY.TXT" (
    echo.
    echo === B2LUSB root hub / USB alici zinciri ===
    type "%SystemDrive%\B2LUSB-USB-SUMMARY.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" (
    echo.
    echo === B2LUSB inbox HID/fare zinciri ===
    type "%SystemDrive%\B2LUSB-INPUT-STACK.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-MOUSE.TXT" (
    echo.
    echo === B2LUSB fare aygitlari ===
    type "%SystemDrive%\B2LUSB-MOUSE.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-HOST-RESOURCES.TXT" (
    echo.
    echo === B2LUSB host atanmis kaynaklari ===
    type "%SystemDrive%\B2LUSB-HOST-RESOURCES.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-HOST-MONITOR.TXT" (
    echo.
    echo === B2LUSB USBXHCI gercek PnP kaynaklari ===
    type "%SystemDrive%\B2LUSB-HOST-MONITOR.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-SETUPAPI-SLICE.TXT" (
    echo.
    echo === B2LUSB SetupAPI Code 10 ozeti ===
    type "%SystemDrive%\B2LUSB-SETUPAPI-SLICE.TXT"
  )
  if exist "%SystemDrive%\B2LUSB-ETW-SUMMARY.TXT" (
    echo.
    echo === B2LUSB USBXHCI ETW hata ozeti ===
    type "%SystemDrive%\B2LUSB-ETW-SUMMARY.TXT"
  )
  echo.
  echo === B2LUSB otomatik HOST son durumu ===
  type "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT"
  if exist "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT" (
    echo.
    echo === B2LUSB MAX77705 kritik ozet ===
    type "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT"
  )
) else if exist "%SystemDrive%\B2LUSB-AUTOHOST.TXT" (
  echo.
  echo === B2LUSB otomatik HOST hata gunlugu ===
  type "%SystemDrive%\B2LUSB-AUTOHOST.TXT"
)
if exist "%B2LUSBDIR%\auto-host.test" echo Native OSK, USB tani ekranini kapatmamasi icin otomatik acilmadi.
echo Multi-touch testi gerekirse: %B2LTOUCHDIR%\b2lmt.exe
rem USB/klavye olmadan olay kaniti: son asamada otomatik foto sayfalari.
if exist "%B2LUSBDIR%\auto-host.test" if exist "%B2LUSBDIR%\b2lusbctl.exe" "%B2LUSBDIR%\b2lusbctl.exe" maxscreen
cmd.exe /k
goto :eof

:B2LUsbInstall
if not exist "%B2LUSBDIR%\b2lusb.inf" (
  echo B2LUSB Windows paketi bu WIM'de yok; atlandi.
  exit /b 0
)
if not exist "%B2LUSBDIR%\b2lusb-host.inf" (
  echo HATA: B2LUSB host INF yok; yari paket reddedildi.
  exit /b 1
)
if not exist "%B2LUSBDIR%\b2lmax77705.inf" (
  echo HATA: B2LUSB MAX77705 INF yok; yari paket reddedildi.
  exit /b 1
)
if not exist "%B2LUSBDIR%\manifest.txt" (
  echo HATA: B2LUSB manifest yok; yari paket reddedildi.
  exit /b 1
)
if not exist "%B2LUSBDIR%\offline-driverstore.txt" (
  echo HATA: B2LUSB offline DriverStore isareti yok; runtime INF kurulumu reddedildi.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\Ucx01000.sys" (
  echo HATA: Microsoft Ucx01000.sys xHCI class extension yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\USBXHCI.SYS" (
  echo HATA: Microsoft USBXHCI.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\USBHUB3.SYS" (
  echo HATA: Microsoft USBHUB3.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\HIDCLASS.SYS" (
  echo HATA: Microsoft HIDCLASS.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\HIDPARSE.SYS" (
  echo HATA: Microsoft HIDPARSE.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\HIDUSB.SYS" (
  echo HATA: Microsoft HIDUSB.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\MOUHID.SYS" (
  echo HATA: Microsoft MOUHID.SYS yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\MOUCLASS.SYS" (
  echo HATA: Microsoft MOUCLASS.SYS yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\Ucx01000" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft Ucx01000 servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\USBXHCI" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft USBXHCI servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\USBHUB3" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft USBHUB3 servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\HidUsb" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft HidUsb servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\mouhid" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft mouhid servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\mouclass" >nul 2>&1
if errorlevel 1 (
  echo HATA: Microsoft mouclass servis kaydi yok.
  exit /b 1
)
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lmax77705" >nul 2>&1
if errorlevel 1 (
  echo HATA: b2lmax77705 servis kaydi yok.
  exit /b 1
)
echo Paket kimligi:
type "%B2LUSBDIR%\manifest.txt"
> "%SystemDrive%\B2LUSB-PNP.TXT" echo B2LUSB_OFFLINE_DRIVERSTORE=1
pnputil /scan-devices >> "%SystemDrive%\B2LUSB-PNP.TXT" 2>&1
timeout /t 3 /nobreak >nul 2>&1
echo Offline PnP baslangic sonucu:
type "%SystemDrive%\B2LUSB-PNP.TXT"
pnputil /enum-devices /deviceid "ACPI\BYN0002" 2>nul
pnputil /enum-devices /deviceid "URS\BYN0002&HOST" /services /stack /drivers /resources 2>nul
pnputil /enum-devices /deviceid "URS\BYN0002&FUNCTION" /services /stack /drivers /resources 2>nul
pnputil /enum-devices /deviceid "ACPI\BYN0004" /services /stack /drivers /resources 2>nul
call :B2LUsbCollectMax77705State
if exist "%B2LUSBDIR%\b2lusbctl.exe" (
  echo.
  echo B2LUSB ilk durum:
  "%B2LUSBDIR%\b2lusbctl.exe" status
  if exist "%B2LUSBDIR%\auto-host.test" (
    echo.
    echo B2LUSB --usb-test gercek UCM/URS rol kontrolu:
    > "%SystemDrive%\B2LUSB-AUTOHOST.TXT" echo Rol MAX77705 - UCM - URS zinciriyle belirlenir.
    call :B2LUsbTraceStart
    "%B2LUSBDIR%\b2lusbctl.exe" observe >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    pnputil /scan-devices >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    timeout /t 5 /nobreak >nul 2>&1
    call :B2LUsbTraceStop
    pnputil /enum-devices /deviceid "URS\BYN0002&HOST" /services /stack /drivers /resources >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    pnputil /enum-devices /deviceid "URS\BYN0002&HOST" /resources > "%SystemDrive%\B2LUSB-HOST-RESOURCES.TXT" 2>&1
    call :B2LUsbCollectMax77705State
    reg.exe query "HKLM\SYSTEM\CurrentControlSet\Enum\URS" /s /f B2LMon > "%SystemDrive%\B2LUSB-HOST-MONITOR.TXT" 2>&1
    call :B2LUsbCollectSetupApiSummary
    call :B2LUsbCollectInputState
    "%B2LUSBDIR%\b2lusbctl.exe" status > "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT" 2>&1
    type "%SystemDrive%\B2LUSB-AUTOHOST.TXT"
    type "%SystemDrive%\B2LUSB-MOUSE.TXT"
    type "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT"
  ) else (
    echo Rol otomatik: gercek Type-C baglantisi bekleniyor.
  )
)
exit /b 0

:B2LUsbTraceStart
rem USBXHCI Code 10 erken baslatma hatasini klavye olmadan yakala. ETW yoksa
rem test akisini bozma; kayit CACHE'e :B2LUsbExportLogs ile aktarilir.
if not exist "%SystemRoot%\System32\logman.exe" (
  > "%SystemDrive%\B2LUSB-ETW.TXT" echo ETW_LOGMAN=YOK
  exit /b 0
)
logman delete -n B2LUSBTRACE >nul 2>&1
logman create trace -n B2LUSBTRACE -ow -o "%SystemDrive%\B2LUSB-USBXHCI.etl" -nb 16 64 -bs 64 > "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
if errorlevel 1 exit /b 0
logman update trace -n B2LUSBTRACE -p Microsoft-Windows-USB-USBXHCI "(Default,PartialDataBusTrace)" >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
if errorlevel 1 goto :B2LUsbTraceStartFail
logman update trace -n B2LUSBTRACE -p Microsoft-Windows-USB-UCX "(Default,PartialDataBusTrace)" >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
if errorlevel 1 goto :B2LUsbTraceStartFail
logman start -n B2LUSBTRACE >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
if errorlevel 1 goto :B2LUsbTraceStartFail
set "B2LUSBTRACEACTIVE=1"
exit /b 0

:B2LUsbTraceStartFail
logman delete -n B2LUSBTRACE >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
exit /b 0

:B2LUsbTraceStop
if not "%B2LUSBTRACEACTIVE%"=="1" exit /b 0
logman stop -n B2LUSBTRACE >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
if exist "%SystemRoot%\System32\tracerpt.exe" if exist "%SystemDrive%\B2LUSB-USBXHCI.etl" (
  tracerpt "%SystemDrive%\B2LUSB-USBXHCI.etl" -o "%SystemDrive%\B2LUSB-USBXHCI.csv" -of CSV -y >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
  if exist "%SystemDrive%\B2LUSB-USBXHCI.csv" "%B2LUSBDIR%\b2lusbctl.exe" filter "%SystemDrive%\B2LUSB-USBXHCI.csv" "error" "fail" "status" "0xC000009E" > "%SystemDrive%\B2LUSB-ETW-SUMMARY.TXT" 2>nul
)
logman delete -n B2LUSBTRACE >> "%SystemDrive%\B2LUSB-ETW.TXT" 2>&1
set "B2LUSBTRACEACTIVE=0"
exit /b 0

:B2LUsbExportLogs
rem CACHE etiketi/yazma geri-okumasi basarili olduktan sonra tanilari disari al.
call :B2LUsbCollectSetupApiSummary
for %%F in ("%SystemDrive%\B2LUSB-*.TXT" "%SystemDrive%\B2LUSB-*.ETL") do if exist "%%~fF" copy /y "%%~fF" "W:\%%~nxF" >nul 2>&1
exit /b 0

:B2LUsbCollectSetupApiSummary
rem USB aygit kimligiyle birlikte SetupAPI'nin Code 10/problem-status satirlarini
rem ekrana sigacak tek dosyada tut. Eslesme yok/hata ayrimi acikca yazilir.
if not exist "%SystemRoot%\INF\setupapi.dev.log" exit /b 0
"%B2LUSBDIR%\b2lusbctl.exe" filter "%SystemRoot%\INF\setupapi.dev.log" "BYN0002" "BYN0003" "BYN0004" "b2lmax77705" "USBXHCI" "Device not started" "CM_PROB_FAILED_START" "0xC000009E" > "%SystemDrive%\B2LUSB-SETUPAPI-SLICE.TXT" 2>nul
exit /b 0

:B2LUsbCollectMax77705State
> "%SystemDrive%\B2LUSB-MAX77705.TXT" echo MAX77705 ACPI/PnP durumu:
pnputil /enum-devices /deviceid "ACPI\BYN0004" /services /stack /drivers /resources >> "%SystemDrive%\B2LUSB-MAX77705.TXT" 2>&1
echo. >> "%SystemDrive%\B2LUSB-MAX77705.TXT"
echo Samsung uyumlu CC/VBUS/OTG tani alanlari: >> "%SystemDrive%\B2LUSB-MAX77705.TXT"
rem Bir key dokumu: her alan icin ayri reg.exe sureci acma.
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lmax77705" >> "%SystemDrive%\B2LUSB-MAX77705.TXT" 2>&1
> "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT" echo Eksik satir, surucunun o asamaya ulasmadigini gosterir.
call :B2LUsbAppendMax77705Summary STAGE B2LMaxStage
call :B2LUsbAppendMax77705Summary STATUS B2LMaxStatus
call :B2LUsbAppendMax77705Summary I2C_CTL B2LMaxHsI2cControl
call :B2LUsbAppendMax77705Summary I2C_INT B2LMaxHsI2cIntStatus
call :B2LUsbAppendMax77705Summary I2C_ERR B2LMaxHsI2cErrorStatus
call :B2LUsbAppendMax77705Summary I2C_XFER B2LMaxHsI2cTransferStatus
call :B2LUsbAppendMax77705Summary I2C_FIFO B2LMaxHsI2cFifoStatus
call :B2LUsbAppendMax77705Summary I2C_AUTO B2LMaxHsI2cAutoConfiguration
call :B2LUsbAppendMax77705Summary I2C_REGADDR B2LMaxHsI2cAddress
call :B2LUsbAppendMax77705Summary I2C_TRYADDR B2LMaxHsI2cAttemptedAddress
call :B2LUsbAppendMax77705Summary I2C_RETRY B2LMaxHsI2cRetryCount
call :B2LUsbAppendMax77705Summary I2C_PHASE B2LMaxHsI2cFailurePhase
call :B2LUsbAppendMax77705Summary PLATFORM_OK B2LMaxPlatformPrepared
call :B2LUsbAppendMax77705Summary CMGP_MUX3 B2LMaxCmgpMuxUsi3
call :B2LUsbAppendMax77705Summary CMGP_DIV3 B2LMaxCmgpDivUsi3
call :B2LUsbAppendMax77705Summary CMGP_QCH3 B2LMaxCmgpQchUsi3
call :B2LUsbAppendMax77705Summary GPM12_CON B2LMaxGpm12Con
call :B2LUsbAppendMax77705Summary GPM13_CON B2LMaxGpm13Con
call :B2LUsbAppendMax77705Summary PMIC_ID B2LMaxPmicId
call :B2LUsbAppendMax77705Summary PMIC_REV B2LMaxPmicRev
call :B2LUsbAppendMax77705Summary USBC_HW B2LMaxUsbcHwRev
call :B2LUsbAppendMax77705Summary USBC_FW B2LMaxUsbcFwRev
call :B2LUsbAppendMax77705Summary USBC_MINOR B2LMaxUsbcFwMinor
call :B2LUsbAppendMax77705Summary CC0 B2LMaxCcStatus0
call :B2LUsbAppendMax77705Summary CC1 B2LMaxCcStatus1
call :B2LUsbAppendMax77705Summary TOP_INT B2LMaxTopInterruptSource
call :B2LUsbAppendMax77705Summary TOP_SEEN B2LMaxTopInterruptSourceSeen
call :B2LUsbAppendMax77705Summary TOP_MASK B2LMaxTopInterruptSourceMask
call :B2LUsbAppendMax77705Summary UIC_SEEN B2LMaxUicInterruptSeen
call :B2LUsbAppendMax77705Summary CC_SEEN B2LMaxCcInterruptSeen
call :B2LUsbAppendMax77705Summary PD_SEEN B2LMaxPdInterruptSeen
call :B2LUsbAppendMax77705Summary VDM_SEEN B2LMaxVdmInterruptSeen
call :B2LUsbAppendMax77705Summary CC_STATE B2LMaxCcState
call :B2LUsbAppendMax77705Summary CC_PREV B2LMaxPreviousCcState
call :B2LUsbAppendMax77705Summary CC_SAMPLES B2LMaxCcSampleCount
call :B2LUsbAppendMax77705Summary CC_TRANS B2LMaxCcTransitionCount
call :B2LUsbAppendMax77705Summary CC_IRQS B2LMaxCcInterruptCount
call :B2LUsbAppendMax77705Summary CC_SRC_EV B2LMaxCcSourceEventCount
call :B2LUsbAppendMax77705Summary UIC_M B2LMaxUicInterruptMask
call :B2LUsbAppendMax77705Summary CC_M B2LMaxCcInterruptMask
call :B2LUsbAppendMax77705Summary PD_M B2LMaxPdInterruptMask
call :B2LUsbAppendMax77705Summary VDM_M B2LMaxVdmInterruptMask
call :B2LUsbAppendMax77705Summary MASKS_OK B2LMaxUsbcRuntimeMasksConfigured
call :B2LUsbAppendMax77705Summary CCCTRL1 B2LMaxCcControlReadBack
call :B2LUsbAppendMax77705Summary CCOPEN_OK B2LMaxCcOpenNormalized
call :B2LUsbAppendMax77705Summary CTRL3_BEFORE B2LMaxControl3Before
call :B2LUsbAppendMax77705Summary CTRL3_AFTER B2LMaxControl3After
call :B2LUsbAppendMax77705Summary MUIC_REQ B2LMaxMuicPathRequested
call :B2LUsbAppendMax77705Summary MUIC_CTL1 B2LMaxMuicControl1
call :B2LUsbAppendMax77705Summary MUIC_OK B2LMaxMuicPathVerified
call :B2LUsbAppendMax77705Summary MUIC_NT B2LMaxMuicStatus
call :B2LUsbAppendMax77705Summary MUIC_Q B2LMaxMuicOutstanding
call :B2LUsbAppendMax77705Summary MUIC_DET_NT B2LMaxMuicDetectStatus
call :B2LUsbAppendMax77705Summary MUIC_CABLE B2LMaxMuicType
call :B2LUsbAppendMax77705Summary MUIC_CAUSE B2LMaxMuicCause
call :B2LUsbAppendMax77705Summary MUIC_VALID B2LMaxMuicSampleValid
call :B2LUsbAppendMax77705Summary MUIC_BC B2LMaxMuicEffectiveBc
call :B2LUsbAppendMax77705Summary MUIC_CHGTYP B2LMaxMuicChargerType
call :B2LUsbAppendMax77705Summary RID_RAW B2LMaxRidRaw
call :B2LUsbAppendMax77705Summary RID_CACHED B2LMaxMuicRid
call :B2LUsbAppendMax77705Summary MUIC_RPRD B2LMaxMuicRprd
call :B2LUsbAppendMax77705Summary UCM_STATUS B2LMaxConnectorStatus
call :B2LUsbAppendMax77705Summary UCM_SWAPSTATUS B2LMaxConnectorSwapStatus
call :B2LUsbAppendMax77705Summary UCM_RUNNING B2LMaxConnectorRunning
call :B2LUsbAppendMax77705Summary UCM_ATTACHED B2LMaxConnectorAttached
call :B2LUsbAppendMax77705Summary UCM_QUEUED B2LMaxConnectorQueued
call :B2LUsbAppendMax77705Summary UCM_ATTEMPTS B2LMaxConnectorAttempts
call :B2LUsbAppendMax77705Summary UCM_COUNT B2LMaxConnectorCount
call :B2LUsbAppendMax77705Summary UCM_OVERFLOW B2LMaxConnectorOverflow
call :B2LUsbAppendMax77705Summary PD_RID B2LMaxPdRid
call :B2LUsbAppendMax77705Summary RID_TRANS B2LMaxPdRidChanges
call :B2LUsbAppendMax77705Summary RID_DETACH B2LMaxRidUsbDetaches
call :B2LUsbAppendMax77705Summary RID_BLOCK B2LMaxRidUsbAttachBlocks
call :B2LUsbAppendMax77705Summary USB_NOTIFY B2LMaxUsbRoleAttached
call :B2LUsbAppendMax77705Summary PATH_WAIT B2LMaxConnectorPathWaiting
call :B2LUsbAppendMax77705Summary PATH_NT B2LMaxConnectorPathStatus
call :B2LUsbAppendMax77705Summary PATH_WAITS B2LMaxConnectorPathWaits
call :B2LUsbAppendMax77705Summary UCM_STALE B2LMaxConnectorStaleEvents
call :B2LUsbAppendMax77705Summary SYSMSG B2LMaxSysmsgValue
call :B2LUsbAppendMax77705Summary SYS_NT B2LMaxSysmsgStatus
call :B2LUsbAppendMax77705Summary SYS_VALID B2LMaxSysmsgValid
call :B2LUsbAppendMax77705Summary READ0 B2LMaxSysmsgSample0
call :B2LUsbAppendMax77705Summary READ1 B2LMaxSysmsgSample1
call :B2LUsbAppendMax77705Summary READ2 B2LMaxSysmsgSample2
call :B2LUsbAppendMax77705Summary READ_N B2LMaxSysmsgSampleCount
call :B2LUsbAppendMax77705Summary SYS_EVENTS B2LMaxSysmsgEvents
call :B2LUsbAppendMax77705Summary MISMATCHES B2LMaxSysmsgMismatches
call :B2LUsbAppendMax77705Summary UNHANDLED B2LMaxSysmsgUnhandled
call :B2LUsbAppendMax77705Summary RETRY_OP B2LMaxSysmsgRetryOpcode
call :B2LUsbAppendMax77705Summary RETRY_NT B2LMaxSysmsgRetryStatus
call :B2LUsbAppendMax77705Summary RETRY_TRIES B2LMaxSysmsgRetries
call :B2LUsbAppendMax77705Summary AFC_TRIES B2LMaxAfcClockAttempts
call :B2LUsbAppendMax77705Summary AFC_NT B2LMaxAfcClockStatus
call :B2LUsbAppendMax77705Summary RELOCK_NT B2LMaxAfcRelockStatus
call :B2LUsbAppendMax77705Summary INIT_STARTED B2LMaxProbeStarted
call :B2LUsbAppendMax77705Summary INIT_NT B2LMaxControlStatus
call :B2LUsbAppendMax77705Summary INIT_Q B2LMaxControlPending
call :B2LUsbAppendMax77705Summary INIT_DONE B2LMaxUsbcInitialized
call :B2LUsbAppendMax77705Summary RESET_PHASE B2LMaxResetStage
call :B2LUsbAppendMax77705Summary RESET_NT B2LMaxResetStatus
call :B2LUsbAppendMax77705Summary PRIOR_FAULT B2LMaxResetPriorFault
call :B2LUsbAppendMax77705Summary WDT_COUNT B2LMaxWatchdogResets
call :B2LUsbAppendMax77705Summary POR_COUNT B2LMaxPorResets
call :B2LUsbAppendMax77705Summary RESET_UIC B2LMaxResetUicRaw
call :B2LUsbAppendMax77705Summary UIC_PENDING B2LMaxSavedUic
call :B2LUsbAppendMax77705Summary UIC_REPLAY B2LMaxLastUicReplay
call :B2LUsbAppendMax77705Summary REPLAY_COUNT B2LMaxUicReplayCount
call :B2LUsbAppendMax77705Summary QUEUE_CLEARS B2LMaxQueueClears
call :B2LUsbAppendMax77705Summary DETACH_CLEARS B2LMaxDetachQueueClears
call :B2LUsbAppendMax77705Summary RX_MISMATCH B2LMaxResponseMismatches
call :B2LUsbAppendMax77705Summary RX_UNHANDLED B2LMaxResponseUnhandled
call :B2LUsbAppendMax77705Summary RX_SHORT B2LMaxResponseShort
call :B2LUsbAppendMax77705Summary EXPECTED B2LMaxResponseExpected
call :B2LUsbAppendMax77705Summary RECEIVED B2LMaxResponseOpcode
call :B2LUsbAppendMax77705Summary RX_LEN B2LMaxResponseLength
call :B2LUsbAppendMax77705Summary DISPATCH_NT B2LMaxResponseStatus
call :B2LUsbAppendMax77705Summary REQ_NT B2LMaxResponseRequestStatus
call :B2LUsbAppendMax77705Summary RAW0 B2LMaxResponseRaw0
call :B2LUsbAppendMax77705Summary RAW1 B2LMaxResponseRaw1
call :B2LUsbAppendMax77705Summary CTRL3_VALID B2LMaxControl3RawValid
call :B2LUsbAppendMax77705Summary CTRL3_RAW B2LMaxControl3Raw
call :B2LUsbAppendMax77705Summary ALT_ACK B2LMaxAltModeAcknowledged
call :B2LUsbAppendMax77705Summary PDO_RETRY B2LMaxPdRequestRetry
call :B2LUsbAppendMax77705Summary SBU_VALID B2LMaxSbuValid
call :B2LUsbAppendMax77705Summary SBU1_RAW B2LMaxSbuRaw0
call :B2LUsbAppendMax77705Summary SBU2_RAW B2LMaxSbuRaw1
call :B2LUsbAppendMax77705Summary SELF_VALID B2LMaxSelftestValid
call :B2LUsbAppendMax77705Summary SELF_CC B2LMaxSelftestCc
call :B2LUsbAppendMax77705Summary SELF_SBU1 B2LMaxSelftestSbu0
call :B2LUsbAppendMax77705Summary SELF_SBU2 B2LMaxSelftestSbu1
call :B2LUsbAppendMax77705Summary QUEUE_DROPS B2LMaxQueueDiscarded
call :B2LUsbAppendMax77705Summary RECOVER_MASK B2LMaxRecoverMask
call :B2LUsbAppendMax77705Summary GRL_COUNT B2LMaxGrlRequests
call :B2LUsbAppendMax77705Summary GRL_NT B2LMaxGrlStatus
call :B2LUsbAppendMax77705Summary ALT_START_REQ B2LMaxAltModeStartRequested
call :B2LUsbAppendMax77705Summary ALT_NT B2LMaxAltModeStatus
call :B2LUsbAppendMax77705Summary PD_MSG B2LMaxPdMessage
call :B2LUsbAppendMax77705Summary PD_NT B2LMaxPdMessageStatus
call :B2LUsbAppendMax77705Summary PD_UNHANDLED B2LMaxPdUnhandledEvents
call :B2LUsbAppendMax77705Summary PSRDY_RAW B2LMaxPsRdyRaw
call :B2LUsbAppendMax77705Summary PSRDY_NT B2LMaxPsRdyStatus
call :B2LUsbAppendMax77705Summary CAPS_VALID B2LMaxPdCapsValid
call :B2LUsbAppendMax77705Summary CAPS_NT B2LMaxPdCapsStatus
call :B2LUsbAppendMax77705Summary CAPS_HEADER B2LMaxPdCapsHeader
call :B2LUsbAppendMax77705Summary PDO_COUNT B2LMaxPdPdoCount
call :B2LUsbAppendMax77705Summary CURRENT_PDO B2LMaxPdCurrentPdo
call :B2LUsbAppendMax77705Summary PD_READ_PENDING B2LMaxPdReadsPending
call :B2LUsbAppendMax77705Summary PD_REC_PENDING B2LMaxPdRecoveryPending
call :B2LUsbAppendMax77705Summary PD_REC_NT B2LMaxPdRecoveryStatus
call :B2LUsbAppendMax77705Summary CHG_UNLOCK B2LMaxChargerUnlocked
call :B2LUsbAppendMax77705Summary CHG_CFG06 B2LMaxChgConfig06
call :B2LUsbAppendMax77705Summary CHG_MODE B2LMaxChgMode
call :B2LUsbAppendMax77705Summary OTG_SW B2LMaxOtgEnabled
call :B2LUsbAppendMax77705Summary OTG_HW B2LMaxOtgHardwareActive
call :B2LUsbAppendMax77705Summary RX_FAIL B2LMaxHsI2cReadFailures
call :B2LUsbAppendMax77705Summary RX_NT B2LMaxHsI2cLastReadFailureStatus
call :B2LUsbAppendMax77705Summary RX_WANTED B2LMaxHsI2cLastReadFailureLength
call :B2LUsbAppendMax77705Summary RX_READ B2LMaxHsI2cLastReadFailureTransferred
call :B2LUsbAppendMax77705Summary I2C_GSIV B2LMaxHsI2cRawIrq
call :B2LUsbAppendMax77705Summary I2C_IRQ B2LMaxHsI2cInterruptCount
call :B2LUsbAppendMax77705Summary MAX_GSIV B2LMaxMaxRawIrq
call :B2LUsbAppendMax77705Summary MAX_IRQ B2LMaxMaxInterruptCount
call :B2LUsbAppendMax77705Summary MAX_NT B2LMaxMaxLastInterruptStatus
call :B2LUsbAppendMax77705Summary OP_DONE B2LMaxOpcodeCompletions
call :B2LUsbAppendMax77705Summary MFD_FAULT B2LMaxMfdFault
call :B2LUsbAppendMax77705Summary PWR_ROLE B2LMaxPowerRole
call :B2LUsbAppendMax77705Summary DATA_ROLE B2LMaxDataRole
call :B2LUsbAppendMax77705Summary DATA_NT B2LMaxDataRoleStatus
call :B2LUsbAppendMax77705Summary WATER B2LMaxWaterDetected
call :B2LUsbAppendMax77705Summary BYP_OCP B2LMaxOvercurrentEvents
call :B2LUsbAppendMax77705Summary DET_RECHECK B2LMaxDetachRechecks
call :B2LUsbAppendMax77705Summary I2C_DONE B2LMaxHsI2cCompletionCount
rem Keep both automatic captures; print only the compact event summary.
rem maxevents reads one registry value and works even if BYN0002 is absent.
if exist "%B2LUSBDIR%\b2lusbctl.exe" (
  "%B2LUSBDIR%\b2lusbctl.exe" maxevents > "%SystemDrive%\B2LUSB-MAX77705-EVENTS-LAST.TXT" 2>&1
  >> "%SystemDrive%\B2LUSB-MAX77705-EVENTS.TXT" echo CAPTURE=%DATE% %TIME%
  type "%SystemDrive%\B2LUSB-MAX77705-EVENTS-LAST.TXT" >> "%SystemDrive%\B2LUSB-MAX77705-EVENTS.TXT"
  "%B2LUSBDIR%\b2lusbctl.exe" filter "%SystemDrive%\B2LUSB-MAX77705-EVENTS-LAST.TXT" "MAX_" >> "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT"
)
echo MAX77705 toplama tamamlandi; son yayimlanan durum:
"%B2LUSBDIR%\b2lusbctl.exe" filter "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT" "STAGE=" "STATUS=" "MAX_"
exit /b 0

:B2LUsbAppendMax77705Summary
for /f "tokens=3" %%V in ('reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lmax77705" /v "%~2" 2^>nul') do >> "%SystemDrive%\B2LUSB-MAX77705-SUMMARY.TXT" echo %~1=%%V
exit /b 0

:B2LUsbCollectInputState
rem Root hub/receiver/HID/mouse zincirini birbirinden ayir. Tam PnP raporlari
rem CACHE'e aktarilir; final ekranda fare sinifi ayrica gorunur.
> "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" echo USBHUB3/HIDUSB/MOUHID/MOUCLASS servis kayitlari:
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\USBHUB3" /v Start >> "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" 2>&1
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\HidUsb" /v Start >> "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" 2>&1
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\mouhid" /v Start >> "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" 2>&1
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\mouclass" /v Start >> "%SystemDrive%\B2LUSB-INPUT-STACK.TXT" 2>&1
pnputil /enum-devices /class USB /services /stack /drivers > "%SystemDrive%\B2LUSB-USB.TXT" 2>&1
"%B2LUSBDIR%\b2lusbctl.exe" filter "%SystemDrive%\B2LUSB-USB.TXT" "Instance ID:" "Device Description:" "Status:" "Problem Code:" "Driver Name:" > "%SystemDrive%\B2LUSB-USB-SUMMARY.TXT" 2>nul
pnputil /enum-devices /class HIDClass /services /stack /drivers > "%SystemDrive%\B2LUSB-HID.TXT" 2>&1
pnputil /enum-devices /class Mouse /services /stack /drivers > "%SystemDrive%\B2LUSB-MOUSE.TXT" 2>&1
exit /b 0

:B2LTouchInstall
if not exist "%B2LTOUCHDIR%\b2ltouch.inf" (
  echo B2LTOUCH paketi bu WIM'de yok; atlandi.
  exit /b 0
)
if not exist "%B2LTOUCHDIR%\manifest.txt" (
  echo HATA: B2LTOUCH manifest yok; yari paket reddedildi.
  exit /b 1
)
if not exist "%B2LTOUCHDIR%\offline-driverstore.txt" (
  echo HATA: B2LTOUCH offline DriverStore isareti yok; runtime INF kurulumu reddedildi.
  exit /b 1
)
echo Paket kimligi:
type "%B2LTOUCHDIR%\manifest.txt"
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\vhf" > "%SystemDrive%\B2LTOUCH-PNP.TXT" 2>&1
if errorlevel 1 (
  echo HATA: Microsoft VHF offline servis kaydi yok.
  type "%SystemDrive%\B2LTOUCH-PNP.TXT"
  exit /b 1
)
>> "%SystemDrive%\B2LTOUCH-PNP.TXT" echo B2LTOUCH_OFFLINE_DRIVERSTORE=1
pnputil /scan-devices >> "%SystemDrive%\B2LTOUCH-PNP.TXT" 2>&1
timeout /t 3 /nobreak >nul 2>&1
echo Offline PnP baslangic sonucu:
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
set "B2LTOUCHREADY=1"
exit /b 0

:B2LTouchLaunchKeyboard
if not "%B2LTOUCHREADY%"=="1" exit /b 0
if exist "%ProgramFiles%\Common Files\microsoft shared\ink\TabTip.exe" (
  echo Windows dokunmatik klavyesi aciliyor...
  start "" "%ProgramFiles%\Common Files\microsoft shared\ink\TabTip.exe"
  exit /b 0
)
if exist "%SystemRoot%\System32\osk.exe" (
  echo Windows ekran klavyesi aciliyor...
  if exist "%SystemRoot%\B2LOSK\manifest.txt" type "%SystemRoot%\B2LOSK\manifest.txt"
  call :B2LTouchPrepareNativeOsk
  if errorlevel 1 exit /b 1
  start "" "%SystemRoot%\System32\osk.exe"
  exit /b 0
)
echo Windows yerel ekran klavyesi bu WinPE imajinda yok.
exit /b 1

:B2LTouchPrepareNativeOsk
set "B2LOSKLOG=%SystemDrive%\B2LOSK-RUNTIME.TXT"
set "B2LTSFCLSID=HKCR\CLSID\{529A9E6B-6587-4F23-AB9E-9C7D683E3C50}\InprocServer32"
> "%B2LOSKLOG%" echo Microsoft native OSK runtime hazirligi

rem Ciplak ADK WinPE, msctf.dll dosyasini icermeyebilir. Native OSK import
rem kapanimi dosyayi ekler; ancak TSF Thread Manager bir COM sinifi oldugu
rem icin yalniz dosyanin bulunmasi yetmez. Tam Windows'ta Component Based
rem Servicing'in olusturdugu kaydi bu gecici WinPE oturumunda Microsoft'un
rem kendi DllRegisterServer giris noktasi olusturur.
reg.exe query "%B2LTSFCLSID%" >> "%B2LOSKLOG%" 2>&1
if errorlevel 1 (
  if not exist "%SystemRoot%\System32\msctf.dll" (
    echo HATA: native OSK icin msctf.dll yok.
    >> "%B2LOSKLOG%" echo MSCTF_DLL=YOK
    type "%B2LOSKLOG%"
    exit /b 1
  )
  if not exist "%SystemRoot%\System32\regsvr32.exe" (
    echo HATA: native OSK icin regsvr32.exe yok.
    >> "%B2LOSKLOG%" echo REGSVR32=YOK
    type "%B2LOSKLOG%"
    exit /b 1
  )
  "%SystemRoot%\System32\regsvr32.exe" /s "%SystemRoot%\System32\msctf.dll" >> "%B2LOSKLOG%" 2>&1
  if errorlevel 1 (
    echo HATA: msctf.dll COM kaydi basarisiz.
    >> "%B2LOSKLOG%" echo MSCTF_REGSVR32=HATA
    type "%B2LOSKLOG%"
    exit /b 1
  )
  >> "%B2LOSKLOG%" echo MSCTF_REGSVR32=0
)

reg.exe query "%B2LTSFCLSID%" >> "%B2LOSKLOG%" 2>&1
if errorlevel 1 (
  echo HATA: Microsoft Text Services Framework COM kaydi olusturulamadi.
  >> "%B2LOSKLOG%" echo TSF_THREADMGR=YOK
  type "%B2LOSKLOG%"
  exit /b 1
)

rem WinPE'nin Default kullanici hive'inda kalmis veya uyumsuz bir OSK ayari
rem olmamali. Bu yalniz RAM'deki gecici WinPE oturumunun HKCU anahtaridir.
reg.exe delete "HKCU\Software\Microsoft\Osk" /f >> "%B2LOSKLOG%" 2>&1
>> "%B2LOSKLOG%" echo TSF_THREADMGR=HAZIR
type "%B2LOSKLOG%"
exit /b 0

:B2LUfsVerifyBootStart
if not exist "%B2LUFSDIR%\b2lufs.inf" (
  echo HATA: B2LUFS paketi WIM'de yok.
  exit /b 1
)
if not exist "%B2LUFSDIR%\manifest.txt" (
  echo HATA: B2LUFS manifest yok; eski veya yari paketlenmis WIM reddedildi.
  exit /b 1
)
if not exist "%B2LUFSDIR%\offline-driverstore.txt" (
  echo HATA: B2LUFS offline DriverStore isareti yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\drivers\b2lufs.sys" (
  echo HATA: boot-start b2lufs.sys System32\drivers altinda yok.
  exit /b 1
)
if not exist "%SystemRoot%\System32\pnputil.exe" (
  echo HATA: PnPUtil yok.
  exit /b 1
)
echo Paket kimligi:
type "%B2LUFSDIR%\manifest.txt"
echo.
echo Offline boot kaydi:
type "%B2LUFSDIR%\offline-driverstore.txt"

set "B2LUFSSTART="
for /f "tokens=3" %%V in ('reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lufs" /v Start 2^>nul ^| "%SystemRoot%\System32\find.exe" "REG_DWORD"') do set "B2LUFSSTART=%%V"
if /i not "%B2LUFSSTART%"=="0x0" (
  echo HATA: b2lufs servis Start=0 degil ^(bulunan: %B2LUFSSTART%^).
  exit /b 1
)

set "B2LUFSGROUP="
for /f "tokens=2,*" %%V in ('reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lufs" /v Group 2^>nul ^| "%SystemRoot%\System32\find.exe" "REG_SZ"') do set "B2LUFSGROUP=%%W"
if /i not "%B2LUFSGROUP%"=="SCSI Miniport" (
  echo HATA: b2lufs servis grubu SCSI Miniport degil ^(bulunan: %B2LUFSGROUP%^).
  exit /b 1
)

> "%SystemDrive%\B2LUFS-PNP.TXT" echo B2LUFS_OFFLINE_BOOT_START=1
reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lufs" >> "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
pnputil /enum-devices /deviceid "ACPI\BYND9820" /services /stack /drivers /resources >> "%SystemDrive%\B2LUFS-PNP.TXT" 2>&1
echo.
echo Boot-start servis ve PnP sonucu:
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
