@echo off
setlocal enabledelayedexpansion
::
:: WinPE icin SIFIRDAN BCD uretir - TANI SECENEKLERI ACIK.
::
:: Windows'ta, YONETICI olarak calistir:
::     windows\bcd_olustur.cmd [varyant]
::
:: VARYANTLAR - her biri TEK bir hipotezi test eder, ayri ayri dene:
::
::   tani     (varsayilan) sadece tani secenekleri
::   tekcpu   numproc 1  -> Windows'u TEK cekirdege zorlar
::                          Cokme ikincil cekirdek acilirken oluyorsa bu gecer.
::                          Bizim MADT/PSCI ile en cok iliskili varyant.
::   azram    truncatememory 0x100000000 -> RAM'i 4 GB'a kirpar
::                          Cokme yuksek bellek adreslerinden geliyorsa bu gecer.
::                          8 GB'lik haritamizin ust kismi supheli olabilir.
::   zamanlayici  numproc 1 + disabledynamictick + useplatformclock
::                          CEKIRDEK 5-6 SANIYE CALISIP SONRA SESSIZCE
::                          SIFIRLIYOR. Anlik cokme degil; bu ARM portlarinda
::                          klasik bir zamanlayici belirtisi. Windows dinamik
::                          tick'i timer'i Linux'tan farkli kullaniyor ve
::                          GTDT'miz 9830'dan uyarlanmisti.
::                            disabledynamictick -> sabit periyodik tick
::                            useplatformclock   -> platform saatini kullan
::
::   hatagoster  Windows'un KENDI hata raporlamasini ACAR.
::                          numproc 1 + recoveryenabled Yes + DisplayAllFailures.
::                          Windows her acilisin durumunu ESP'deki bootstat.dat'a
::                          yazar; SONRAKI acilista bootmgr onu okuyup
::                          "Windows failed to start" ekranini HATA KODUYLA
::                          gosterir. Yani hatayi N. acilista goremiyoruz ama
::                          N+1. acilista okuyoruz.
::                          Iki kez ustuste ac: ilki coksun, ikincisi raporlasin.
::
::
::   disk     WinPE'yi RAM diskten degil GERCEK BIR NTFS BOLUMDEN acar.
::            ramdisk yerine locate= kullaniyor. storufs baglanmazsa winload
::            ekrana hata basar - RAM diskteki sessizligin panzehiri.
::            Once: bash tools_windisk.sh  (SYSTEM bolumu icin NTFS imaji)
::
::   menu     "disk" + ACILIS MENUSU + 30 saniye bekleme.
::            TEK ACILISTA IKI SORUYU BIRDEN AYIRIR.
::
::            Disk denemesinde cihaz cemberi gosterip 1-2 saniyede
::            sifirladi (RAM diskte 16 saniyeydi). Cemberi hem bootmgfw
::            hem winload ciziyor, dolayisiyla ikisini ayirt edemiyoruz:
::              (a) bootmgfw birimleri tararken mi oluyor
::              (b) winload'u bulup calistirdi da winload mi oluyor
::
::            Menu tam bu noktada, winload cagrilmadan HEMEN ONCE ciziliyor
::            ve kullanici girdisi bekliyor. Yani:
::              Menu cikip BEKLIYOR  -> bootmgfw saglam, NTFS/UFS taramasi
::                                      sorunsuz. Sucu winload'da. Enter'a
::                                      basip ayni acilista ikinci yariyi
::                                      da olcuyoruz.
::              Menu YOK, yine 1-2 s -> bootmgfw'in kendisi oluyor; sorun
::                                      birim taramasinda, Windows'un
::                                      cekirdek tarafina hic gelmemisiz.
::
::            bootmenupolicy zaten Legacy: menu metin modunda ciziliyor,
::            grafik yigina bagimli degil.
::
:: Ikisini AYNI ANDA deneme; hangisinin ise yaradigini bilemezsin.
::
:: Cikti: Masaustunde "BCD" dosyasi. Onu WSL'deki depoya kopyala:
::     cp /mnt/c/Users/<sen>/Desktop/BCD ~/beyond2lte-uefi/windows/BCD
:: tools_winboot.sh varsa bunu ISO'nunki yerine kullanir.
::
:: NEDEN SIFIRDAN:
:: ISO'nun BCD'si Setup icin kurulmus ve tani secenekleri kapali. Cihazda
:: winload.efi calisiyor (ekran temizleniyor) ama sonra sessizce yeniden
:: baslatiyoruz - ne cokme ekrani ne kod goruyoruz. Asagidaki dort ayar tam
:: olarak bunu degistirmek icin:
::
::   recoveryenabled No          -> kurtarma ortamina atlamasin, hata ekranda kalsin
::   bootstatuspolicy IgnoreAllFailures -> ardarda hatada "onarim" moduna girmesin
::   quietboot No                -> acilis logosu yerine metin ciksin
::   sos On                      -> yuklenen surucileri tek tek ekrana yazsin
::
:: Sonuncusu en degerlisi: cekirdegin TAM OLARAK nerede takildigini gosterir.
::
echo.
echo === WinPE BCD uretiliyor
echo.

net session >nul 2>&1
if errorlevel 1 (
  echo HATA: Yonetici olarak calistir.
  pause & exit /b 1
)

set "VAR=%~1"
if "%VAR%"=="" set "VAR=tani"
echo   varyant: %VAR%

set "BCD=%USERPROFILE%\Desktop\BCD"
if exist "%BCD%" del /f /q "%BCD%"

bcdedit /createstore "%BCD%" || goto :hata

::
:: MIRAS ZINCIRI. BUNU ATLAMISTIM VE BIR TUR KAYBETTIRDI.
::
:: Gercek bir BCD'de girdiler {bootloadersettings} -> {globalsettings}
:: zinciriyle baglidir. Elle kurdugumuz depoda {globalsettings}'i olusturup
:: HICBIR YERE BAGLAMAMISTIM; dolayisiyla oraya yazdigim "bootux Disabled"
:: tamamen etkisiz kaldi. "sos On" acikken tek satir metin gormememizin
:: sebebi bu olabilir.
::
bcdedit /store "%BCD%" /create {globalsettings}     /d "Global" >nul 2>&1
bcdedit /store "%BCD%" /create {bootloadersettings} /d "Loader" >nul 2>&1
bcdedit /store "%BCD%" /set {bootloadersettings} inherit {globalsettings} >nul 2>&1
bcdedit /store "%BCD%" /set {globalsettings} bootux Disabled >nul 2>&1

:: --- ramdisk tanimi -------------------------------------------------------
:: boot.sdi'yi burasi isaret ediyor. "boot" = bootmgr'in yuklendigi birim.
bcdedit /store "%BCD%" /create {ramdiskoptions} /d "Ramdisk" >nul || goto :hata
bcdedit /store "%BCD%" /set {ramdiskoptions} ramdisksdidevice boot   >nul || goto :hata
bcdedit /store "%BCD%" /set {ramdiskoptions} ramdisksdipath \boot\boot.sdi >nul || goto :hata

:: --- isletim sistemi girdisi ---------------------------------------------
for /f "tokens=2 delims={}" %%g in ('bcdedit /store "%BCD%" /create /d "beyond2lte WinPE" /application osloader') do set "G={%%g}"
if not defined G goto :hata
echo   osloader GUID: %G%

::
:: RAM DISK MI, GERCEK BOLUM MU
::
:: "disk" varyanti WinPE'yi RAM diskten degil NTFS bir bolumden aciyor.
:: Fikir kullanicidan geldi ve haklıydı: RAM diskte storufs'un baglanip
:: baglanmamasi hicbir seyi degistirmiyor, o yuzden sessiz. Diskten acarken
:: baglanmazsa winload ACILIS BIRIMINI BULAMAZ ve EKRANA HATA BASAR.
::
:: winload'un hata yolunun calistigini biliyoruz: ws2ifsl.sys denemesinde
:: "0xc0000428 / digital signature couldn't be verified" ekranini gorduk.
::
:: "locate=" KULLANIYORUZ, "partition=" DEGIL: bootmgr birimleri kendisi
:: tarayip verilen yolu buluyor. Boylece GPT bolum GUID'ini elle yazmak
:: zorunda kalmiyoruz ve hangi boluma yazarsak yazalim calisiyor.
::
set "DISKMOD="
if /i "%VAR%"=="disk" set "DISKMOD=1"
if /i "%VAR%"=="menu" set "DISKMOD=1"

if defined DISKMOD (
  bcdedit /store "%BCD%" /set %G% device   locate=\Windows\System32\winload.efi >nul || goto :hata
  bcdedit /store "%BCD%" /set %G% osdevice locate=\Windows\System32\winload.efi >nul || goto :hata
  echo   AYAR: gercek bolumden acilis ^(locate^), RAM disk YOK
) else (
  bcdedit /store "%BCD%" /set %G% device   ramdisk=[boot]\sources\boot.wim,{ramdiskoptions} >nul || goto :hata
  bcdedit /store "%BCD%" /set %G% osdevice ramdisk=[boot]\sources\boot.wim,{ramdiskoptions} >nul || goto :hata
)
bcdedit /store "%BCD%" /set %G% path       \windows\system32\boot\winload.efi >nul || goto :hata
bcdedit /store "%BCD%" /set %G% systemroot \windows >nul || goto :hata
bcdedit /store "%BCD%" /set %G% detecthal  Yes
if errorlevel 1 echo   ! BASARISIZ: detecthal  Yes
bcdedit /store "%BCD%" /set %G% winpe      Yes
if errorlevel 1 echo   ! BASARISIZ: winpe      Yes
bcdedit /store "%BCD%" /set %G% inherit {bootloadersettings}
if errorlevel 1 echo   ! BASARISIZ: inherit {bootloadersettings}

:: --- TANI: sessiz cokmeyi gorunur kil ------------------------------------
::
:: DIKKAT: bu ikisi TANIYI ENGELLIYORDU. "Kurtarma ekranina atlamasin" diye
:: koymustum ama o ekran tam olarak bize lazim olan seyi gosteriyor: bir
:: onceki acilisin neden basarisiz oldugunu. "hatagoster" varyanti bunlari
:: tersine cevirir.
::
if /i "%VAR%"=="hatagoster" (
  bcdedit /store "%BCD%" /set %G% recoveryenabled  Yes
  if errorlevel 1 echo   ! BASARISIZ: recoveryenabled  Yes
  bcdedit /store "%BCD%" /set %G% bootstatuspolicy DisplayAllFailures
  if errorlevel 1 echo   ! BASARISIZ: bootstatuspolicy DisplayAllFailures
  bcdedit /store "%BCD%" /set %G% numproc 1
  if errorlevel 1 echo   ! BASARISIZ: numproc 1
  echo   AYAR: hata raporlamasi ACIK + numproc 1
) else (
  bcdedit /store "%BCD%" /set %G% recoveryenabled    No
  if errorlevel 1 echo   ! BASARISIZ: recoveryenabled    No
  bcdedit /store "%BCD%" /set %G% bootstatuspolicy   IgnoreAllFailures
  if errorlevel 1 echo   ! BASARISIZ: bootstatuspolicy   IgnoreAllFailures
)
bcdedit /store "%BCD%" /set %G% quietboot          No
if errorlevel 1 echo   ! BASARISIZ: quietboot          No
bcdedit /store "%BCD%" /set %G% sos                On
if errorlevel 1 echo   ! BASARISIZ: sos                On
:: Legacy boot deneyimi: grafik yerine METIN. Donen daire yerine surucu
:: isimleri akmasi icin gerekli - "sos On" tek basina Win11'de yetmiyor.
bcdedit /store "%BCD%" /set %G% bootmenupolicy      Legacy
if errorlevel 1 echo   ! BASARISIZ: bootmenupolicy      Legacy
bcdedit /store "%BCD%" /set %G% nx                 OptIn
if errorlevel 1 echo   ! BASARISIZ: nx                 OptIn

:: --- varyanta ozel ayar ---------------------------------------------------
if /i "%VAR%"=="tekcpu" (
  bcdedit /store "%BCD%" /set %G% numproc 1
  if errorlevel 1 echo   ! BASARISIZ: numproc 1
  echo   AYAR: numproc 1  ^(tek cekirdek^)
)
if /i "%VAR%"=="zamanlayici" (
  bcdedit /store "%BCD%" /set %G% numproc 1
  if errorlevel 1 echo   ! BASARISIZ: numproc 1
  bcdedit /store "%BCD%" /set %G% disabledynamictick Yes
  if errorlevel 1 echo   ! BASARISIZ: disabledynamictick Yes
  bcdedit /store "%BCD%" /set %G% useplatformclock Yes
  if errorlevel 1 echo   ! BASARISIZ: useplatformclock Yes
  echo   AYAR: numproc 1 + disabledynamictick + useplatformclock
)
if defined DISKMOD (
  bcdedit /store "%BCD%" /set %G% numproc 1
  if errorlevel 1 echo   ! BASARISIZ: numproc 1
  echo   AYAR: numproc 1
)
if /i "%VAR%"=="azram" (
  bcdedit /store "%BCD%" /set %G% truncatememory 0x100000000
  if errorlevel 1 echo   ! BASARISIZ: truncatememory 0x100000000
  echo   AYAR: truncatememory 4 GB
)

:: --- boot manager ---------------------------------------------------------
bcdedit /store "%BCD%" /create {bootmgr} /d "beyond2lte" >nul
bcdedit /store "%BCD%" /set {bootmgr} device boot
if errorlevel 1 echo   ! BASARISIZ: device boot
bcdedit /store "%BCD%" /set {bootmgr} default %G%
if errorlevel 1 echo   ! BASARISIZ: default %G%
::
:: MENU VARYANTI: bootmgfw'i winload'u cagirmadan ONCE durdurup bekletiyoruz.
::
:: Menunun ciziliyor olmasi tek basina bir olcum: bootmgfw o noktaya kadar
:: birimleri taramis, BCD'yi okumus ve ekrani kurmus demektir. Cizilmiyorsa
:: sorun daha geride.
::
:: timeout 30 - kullanici fotografi cekip Enter'a basacak kadar zaman.
::
if /i "%VAR%"=="menu" (
  bcdedit /store "%BCD%" /set {bootmgr} displaybootmenu Yes
  if errorlevel 1 echo   ! BASARISIZ: displaybootmenu Yes
  bcdedit /store "%BCD%" /set {bootmgr} timeout 30
  if errorlevel 1 echo   ! BASARISIZ: timeout 30
  echo   AYAR: acilis menusu ACIK, 30 saniye bekliyor
) else (
  bcdedit /store "%BCD%" /set {bootmgr} timeout 0
  if errorlevel 1 echo   ! BASARISIZ: timeout 0
)
bcdedit /store "%BCD%" /displayorder %G% /addlast  >nul

echo.
echo === Sonuc  (BU CIKTIYI PAYLAS - hangi ayarin gercekten yazildigini gosterir)
bcdedit /store "%BCD%" /enum all
echo.
echo Dosya: %BCD%
echo.
echo Simdi WSL'de:
echo   cp "%BCD%" ~/beyond2lte-uefi/windows/BCD
echo   cd ~/beyond2lte-uefi ^&^& bash tools_winboot.sh ^&^& bash cache_esp.sh --win
echo.
pause
exit /b 0

:hata
echo.
echo BCD uretimi basarisiz. Yukaridaki hataya bak.
pause
exit /b 1
