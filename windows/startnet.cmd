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
echo [B2LUFS kurulumu]
call :B2LUfsInstall
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
if exist "%B2LUSBDIR%\b2lusbctl.exe" echo USB host kontrolu: %B2LUSBDIR%\b2lusbctl.exe status^|observe^|host
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
) else if exist "%SystemDrive%\B2LUSB-AUTOHOST.TXT" (
  echo.
  echo === B2LUSB otomatik HOST hata gunlugu ===
  type "%SystemDrive%\B2LUSB-AUTOHOST.TXT"
)
if exist "%B2LUSBDIR%\auto-host.test" echo Native OSK, USB tani ekranini kapatmamasi icin otomatik acilmadi.
echo Multi-touch testi gerekirse: %B2LTOUCHDIR%\b2lmt.exe
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
echo Paket kimligi:
type "%B2LUSBDIR%\manifest.txt"
> "%SystemDrive%\B2LUSB-PNP.TXT" echo B2LUSB_OFFLINE_DRIVERSTORE=1
pnputil /scan-devices >> "%SystemDrive%\B2LUSB-PNP.TXT" 2>&1
timeout /t 3 /nobreak >nul 2>&1
echo Offline PnP baslangic sonucu:
type "%SystemDrive%\B2LUSB-PNP.TXT"
pnputil /enum-devices /deviceid "ACPI\BYN0002" 2>nul
pnputil /enum-devices /deviceid "ACPI\BYN0003" /services /stack /drivers /resources 2>nul
if exist "%B2LUSBDIR%\b2lusbctl.exe" (
  echo.
  echo B2LUSB ilk durum:
  "%B2LUSBDIR%\b2lusbctl.exe" status
  if exist "%B2LUSBDIR%\auto-host.test" (
    echo.
    echo B2LUSB --usb-test statik ACPI HOST ilk-boot kontrolu:
    > "%SystemDrive%\B2LUSB-AUTOHOST.TXT" echo ACPI HOST child yerinde birakildi; hedefli restart yapilmadi.
    call :B2LUsbTraceStart
    "%B2LUSBDIR%\b2lusbctl.exe" observe >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    pnputil /scan-devices >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    timeout /t 5 /nobreak >nul 2>&1
    call :B2LUsbTraceStop
    pnputil /enum-devices /deviceid "ACPI\BYN0003" /services /stack /drivers /resources >> "%SystemDrive%\B2LUSB-AUTOHOST.TXT" 2>&1
    pnputil /enum-devices /deviceid "ACPI\BYN0003" /resources > "%SystemDrive%\B2LUSB-HOST-RESOURCES.TXT" 2>&1
    reg.exe query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI" /s /f B2LMon > "%SystemDrive%\B2LUSB-HOST-MONITOR.TXT" 2>&1
    call :B2LUsbCollectSetupApiSummary
    call :B2LUsbCollectInputState
    "%B2LUSBDIR%\b2lusbctl.exe" status > "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT" 2>&1
    type "%SystemDrive%\B2LUSB-AUTOHOST.TXT"
    type "%SystemDrive%\B2LUSB-MOUSE.TXT"
    type "%SystemDrive%\B2LUSB-AUTOHOST-STATUS.TXT"
  ) else (
    echo HOST yeniden hazirlama: %B2LUSBDIR%\b2lusbctl.exe host
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
  if exist "%SystemDrive%\B2LUSB-USBXHCI.csv" findstr /i /c:"error" /c:"fail" /c:"status" /c:"0xC000009E" "%SystemDrive%\B2LUSB-USBXHCI.csv" > "%SystemDrive%\B2LUSB-ETW-SUMMARY.TXT" 2>nul
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
rem ekrana sigacak tek dosyada tut. Kaynak log salt okunur; hata yoksa bos kalir.
if not exist "%SystemRoot%\INF\setupapi.dev.log" exit /b 0
findstr /i /c:"BYN0002" /c:"BYN0003" /c:"USBXHCI" /c:"Device not started" /c:"CM_PROB_FAILED_START" /c:"0xC000009E" "%SystemRoot%\INF\setupapi.dev.log" > "%SystemDrive%\B2LUSB-SETUPAPI-SLICE.TXT" 2>nul
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
findstr /i /c:"Instance ID:" /c:"Device Description:" /c:"Status:" /c:"Problem Code:" /c:"Driver Name:" "%SystemDrive%\B2LUSB-USB.TXT" > "%SystemDrive%\B2LUSB-USB-SUMMARY.TXT" 2>nul
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
