@echo off
setlocal EnableExtensions
title Beyond2LTE WinPE automatic diagnostics
rem Yatayda yazinin kuculmemesi icin genislik sabit; gecmis cikti icin tampon yuksektir.
mode con: cols=112 lines=176 >nul 2>&1

rem Bu portta henuz klavye, fare veya dokunmatik girisi yoktur. Dolayisiyla
rem bu betik her seyi kendiliginden calistirir; `pause` veya kullanici
rem girdisi bekleyen bir komut kullanmaz.
set "DPSCRIPT=%SystemDrive%\B2LDISK.TXT"
set "DPPOSTSCRIPT=%SystemDrive%\B2LDISK-POST.TXT"
set "UFSBOOTREPORT=%SystemRoot%\B2LUFS.TXT"
set "B2LUFSDIR=%SystemRoot%\B2LUFS"

echo.
echo === Beyond2LTE WinPE: otomatik tani ===
echo PnP/WMI baslatiliyor; bu asama biraz surebilir.
%SystemRoot%\System32\wpeinit.exe

rem b2lcmd istege bagli bir b2lfb damgasidir; UFS tanisinin parcasi degildir.
rem Bu WIM'e enjekte edilmediyse sessizce atlanir.
if exist "%SystemRoot%\System32\b2lcmd.exe" "%SystemRoot%\System32\b2lcmd.exe" >nul 2>&1

> "%DPSCRIPT%" (
  echo list disk
  echo list volume
)

rem Bu ikinci DiskPart betigi de salt-okunurdur. Disk 0 bu WinPE'de B2LUFS'in
rem tek kullanici-LUN hedefidir; select/detail/list komutlari media yazmaz.
> "%DPPOSTSCRIPT%" (
  echo list disk
  echo select disk 0
  echo detail disk
  echo list partition
  echo list volume
)

rem Cikti dogrudan konsola gider: RAM-disk gunlugu resetten sonra kalmaz;
rem aradaki iki saniye fotograf cekmek icindir. Tum komutlar salt-okunurdur.
echo.
echo === Beyond2LTE WinPE otomatik tani ===
echo.
echo [Windows]
ver
call :PhotoPause

echo.
echo [DiskPart: diskler ve birimler - salt okunur]
diskpart /s "%DPSCRIPT%"
echo Not: X: WinPE'nin boot.wim RAM diski; dahili UFS diski degil.
call :PhotoPause

echo.
echo [B2LUFS Faz 1: salt-okunur LUN 0 miniportunu PnP'ye ekle]
call :B2LUfsInstall

echo.
echo [DiskPart: B2LUFS Faz 1 sonrasi - salt okunur disk/GPT taramasi]
diskpart /s "%DPPOSTSCRIPT%"
echo Beklenen: Disk 0 ve GPT bolumleri gorunur; B2LUFS yazma CDB'lerini reddeder.
echo.
echo [B2LUFS: UFS'ten donen ilk salt-okunur baytlar]
call :B2LUfsTransportLog

echo.
echo [PnP: klavye/fare/USB/ag]
if exist "%SystemRoot%\System32\pnputil.exe" (
  pnputil /enum-devices /connected /class Keyboard /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /class Mouse /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /bus USB /drivers
  call :PhotoPause
  pnputil /enum-devices /connected /class Net /drivers
  call :PhotoPause
) else (
  echo PnPUtil yok; HID/USB agaci olculemedi.
  reg query "HKLM\SYSTEM\CurrentControlSet\Enum\HID" /s
  call :PhotoPause
  reg query "HKLM\SYSTEM\CurrentControlSet\Enum\USB" /s
  call :PhotoPause
)

echo.
echo [IP yapilandirmasi]
ipconfig /all
call :PhotoPause

echo.
echo [UFS: ACPI UFSHCI kimligi ve secilen miniport]
call :UfsReport

echo.
echo --- storufs: inbox dosya, DriverStore ve servis kaydi ---
if exist "%SystemRoot%\System32\drivers\storufs.sys" (
  echo storufs.sys bulundu:
  dir /-c "%SystemRoot%\System32\drivers\storufs.sys"
) else (
  echo storufs.sys YOK: bu WinPE imajinda UFS miniportu bulunmuyor olabilir.
)
call :PhotoPause
dir /b /s "%SystemRoot%\System32\DriverStore\FileRepository\storufs.inf_*" 2>nul
call :PhotoPause
echo --- storufs servis kaydi ---
rem /s yerine ilgili dort anahtari ayri goster: uzun/ikili alt agac yok.
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs"
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\Parameters" 2>nul
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\Enum" 2>nul
reg query "HKLM\SYSTEM\CurrentControlSet\Services\storufs\StartOverride" 2>nul
call :PhotoPause

echo.
echo === Tani sonu ===

echo.
echo [B2LUFS: erken, salt-okunur HCI kaydi]
if exist "%UFSBOOTREPORT%" (
  type "%UFSBOOTREPORT%"
) else (
  echo B2LUFS.TXT bulunamadi: b2lfb, b2lcmd olayini veya WinPE RAM diskini gormemis olabilir.
)
echo.
echo Yazilimsal yeniden baslatma bu platformda donuyor; burada bekleniyor.
cmd.exe /k
goto :eof

:B2LUfsInstall
rem Paket/manifest yoksa bu WIM onceki veya yari paketlenmis bir surumdur.
rem Manifest, tools_b2lufs.sh'nin imzalanmis INF+SYS+CAT paketinden sonra
rem uretilir. Bu nedenle ekranda gorulmesi calisan WIM'in gercekten son
rem b2lufs paketini tasidiginin dogrudan kanitidir.
if not exist "%B2LUFSDIR%\b2lufs.inf" (
  echo B2LUFS paketi yok; inbox storufs tanisi ile devam ediliyor.
  exit /b 0
)
if not exist "%B2LUFSDIR%\manifest.txt" (
  echo B2LUFS manifest yok; yari/eski WIM reddedildi. Inbox storufs yuklenmeyecek.
  exit /b 1
)
echo B2LUFS WIM paket kimligi:
type "%B2LUFSDIR%\manifest.txt"
if not exist "%SystemRoot%\System32\pnputil.exe" (
  echo PnPUtil yok; B2LUFS paketi WinPE'ye eklenemedi.
  exit /b 1
)
echo INF ekleniyor ve yalniz ACPI\BYND9820 eslemesi kuruluyor:
rem HwFindAdapter hatalari kaybolmasin diye Operational kanali, surucu
rem yuklenmeden once etkinlestirilir. Kanal WinPE'de yoksa sessizce atlanir.
if exist "%SystemRoot%\System32\wevtutil.exe" (
  wevtutil sl "Microsoft-Windows-Storage-Storport/Operational" /e:true >nul 2>&1
)
pnputil /add-driver "%B2LUFSDIR%\b2lufs.inf" /install
echo PnP yeniden taraniyor:
pnputil /scan-devices
rem Miniport legacy SRB + BTL8 sozlesmesini kurduktan sonra PASSIVE BusChange
rem bildirir; StorPort'un INQUIRY/LUN taramasina kisa bir zaman penceresi ver.
timeout /t 2 /nobreak >nul 2>&1
echo Beklenen: b2lufs Started; legacy SRB/BTL8 ile LUN 0 disk PDO'su taranir.
echo --- B2LUFS: StorPort kaynak-esleme tanisi ---
call :B2LUfsStorPortLog
exit /b 0

:B2LUfsStorPortLog
if not exist "%SystemRoot%\System32\wevtutil.exe" (
  echo wevtutil yok; StorPort ETW kanali okunamiyor.
  exit /b 0
)
echo Operational kayitlari (yalniz B2LUFS kaynak redleri gorunur):
rem StorPort'un ETW semasi WDK surumleri arasinda degisebilir. Bu nedenle
rem EventData alanlarina gore kirilgan XPath kurmak yerine, PnP kurulumundan
rem hemen sonra olusan son olaylari ham metin olarak gostermek daha guvenilir.
wevtutil qe "Microsoft-Windows-Storage-Storport/Operational" /rd:true /f:text /c:12 2>nul
echo Not: "B2LUFS HwFindAdapter resource contract failure" gorunurse altindaki ikili kayit sirasiyla Failure, RangeIndex, RangeCount, InterfaceType, BusNumber, RangeLength, RangeBase'dir.
exit /b 0

:B2LUfsTransportLog
if not exist "%SystemRoot%\System32\wevtutil.exe" (
  echo wevtutil yok; B2LUFS UFS okuma kaydi gosterilemiyor.
  exit /b 0
)
echo Son B2LUFS SRB ve PnP/LUN yasam-dongusu kayitlari StorPort Operational gunlugunden okunuyor:
echo Her "BLS1" ikili kaydinin son 64 bayti UFS'in dondugu ham veridir.
echo CDB=12 INQUIRY, 1B START_STOP(no-op), 25 READ_CAPACITY, 28/88 READ.
echo Result=0 ve SCSI=0 basaridir; CHECK CONDITION kaydinin ornegi UFS sense baytlaridir.
echo "BLP1" kaydi legacy BTL8 PnP istegidir; UFS medya verisi veya CDB icermez.
wevtutil qe "Microsoft-Windows-Storage-Storport/Operational" /rd:true /f:text /c:24 2>nul
exit /b 0

:UfsReport
rem UFS raporu ana betikteki parantezli PnP raporundan bilerek ayridir.
rem Cihazdaki onceki ekranda bu basliktan hemen sonra ana betik bitti.
rem Foto tek basina sebebi ayirt etmiyor; bu alt yordam o belirsiz ana
rem blok akisini ortadan kaldirir ve her iki yolu exit /b ile ana rapora dondurur.
if not exist "%SystemRoot%\System32\pnputil.exe" goto :UfsWithoutPnP

echo --- UFS0: CID, eslesen surucu ve stack ---
pnputil /enum-devices /instanceid "ACPI\SMSN9820\0" /deviceids /drivers /services
call :PhotoPause
call :LunInventory
echo --- UFS0: yalniz PnP kok degerleri; Properties agaci atlandi ---
rem /s, Properties altindaki ikili verileri yuzlerce satir yaziyordu.
rem Kimlik/servis zaten yukaridaki PnPUtil satirinda gorunuyor.
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0\Control" 2>nul
call :PhotoPause
echo --- SCSIAdapter sinifi ---
pnputil /enum-devices /connected /class SCSIAdapter /drivers
call :PhotoPause
echo --- Tum sorunlu PnP aygitlari ---
pnputil /enum-devices /connected /problem
call :PhotoPause
exit /b 0

:UfsWithoutPnP
rem Eski WinPE'de de Properties alt agacini dokme.
call :LunInventory
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820"
call :PhotoPause
exit /b 0

:LunInventory
echo --- B2LUFS: READ CAPACITY onceligi ve ham cevap ---
echo Surucu once 10-baytli READ CAPACITY kullanir; 16-baytli form yalniz 2 TiB ustu LUN icindir.
echo Windows'un kendi capacity SRB'leri de dogrudan UFS'e gider; cache yaniti yoktur.
echo Result=0 ve ScsiStatus=0 ile ReadCapacityValid=1 beklenir; BlockBytes bu cihazda 4096 olmalidir.
set "B2L_LUN_KEY=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\SMSN9820\0\Device Parameters\B2LUFS"
echo Exynos/UFSHCI devralma durumu (CAP=1303FF1F, HCS ready, 32 UTR + 4 UTMR beklenir):
reg query "%B2L_LUN_KEY%" /v HciCapabilities 2>nul
reg query "%B2L_LUN_KEY%" /v HciStatus 2>nul
reg query "%B2L_LUN_KEY%" /v AutoHibern8Before 2>nul
reg query "%B2L_LUN_KEY%" /v TransferSlotCount 2>nul
reg query "%B2L_LUN_KEY%" /v TaskSlotCount 2>nul
echo StorPort SRB/adresleme sozlesmesi (legacy SRB + BTL8 zorunludur):
reg query "%B2L_LUN_KEY%" /v SrbType 2>nul
reg query "%B2L_LUN_KEY%" /v AddressType 2>nul
echo StorPort fiziksel-miniport kuyruk sozlesmesi (Targets=2, Initiator=1, digerleri=1 beklenir):
reg query "%B2L_LUN_KEY%" /v NumberOfBuses 2>nul
reg query "%B2L_LUN_KEY%" /v MaximumTargets 2>nul
reg query "%B2L_LUN_KEY%" /v InitiatorBusId 2>nul
reg query "%B2L_LUN_KEY%" /v ScatterGather 2>nul
reg query "%B2L_LUN_KEY%" /v Master 2>nul
reg query "%B2L_LUN_KEY%" /v TaggedQueuing 2>nul
reg query "%B2L_LUN_KEY%" /v AutoRequestSense 2>nul
reg query "%B2L_LUN_KEY%" /v MultipleRequestPerLu 2>nul
reg query "%B2L_LUN_KEY%" /v MaxNumberOfIO 2>nul
reg query "%B2L_LUN_KEY%" /v MaxIOsPerLun 2>nul
reg query "%B2L_LUN_KEY%" /v InitialLunQueueDepth 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityResult 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityScsiStatus 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityOpcode 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityReturnedBytes 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityValid 2>nul
reg query "%B2L_LUN_KEY%" /v ReadCapacityBlockBytes 2>nul
echo Ham READ CAPACITY cevabi (buyuk-endian last-LBA + blok boyutu):
reg query "%B2L_LUN_KEY%" /v ReadCapacityRaw 2>nul
echo --- B2LUFS: REPORT LUNS envanteri (salt-okunur, LUN 0 uzerinden) ---
echo Bu bilgi diger LUN'lara blok okuma/yazma yapmadan alinir.
echo Header: ilk 4 bayt big-endian liste bayti; sonraki her 8 bayt bir LUN kimligidir.
reg query "%B2L_LUN_KEY%" /v SchemaVersion 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsResult 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsScsiStatus 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsListBytes 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsReturnedBytes 2>nul
reg query "%B2L_LUN_KEY%" /v ReportLunsEntryCount 2>nul
echo Ham REPORT LUNS: 8 bayt baslik + LUN basina 8 bayt (REG_BINARY)
reg query "%B2L_LUN_KEY%" /v ReportLunsRaw 2>nul
echo Tek tek LUN kimlikleri (Lun00Id, Lun01Id, ...):
reg query "%B2L_LUN_KEY%" 2>nul
call :PhotoPause
exit /b 0

:PhotoPause
rem Bu turda fotograf arasi bekleme kapali: tarama hizla son kayda gelir.
exit /b
