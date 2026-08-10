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

:: --- ramdisk tanimi -------------------------------------------------------
:: boot.sdi'yi burasi isaret ediyor. "boot" = bootmgr'in yuklendigi birim.
bcdedit /store "%BCD%" /create {ramdiskoptions} /d "Ramdisk" >nul || goto :hata
bcdedit /store "%BCD%" /set {ramdiskoptions} ramdisksdidevice boot   >nul || goto :hata
bcdedit /store "%BCD%" /set {ramdiskoptions} ramdisksdipath \boot\boot.sdi >nul || goto :hata

:: --- isletim sistemi girdisi ---------------------------------------------
for /f "tokens=2 delims={}" %%g in ('bcdedit /store "%BCD%" /create /d "beyond2lte WinPE" /application osloader') do set "G={%%g}"
if not defined G goto :hata
echo   osloader GUID: %G%

bcdedit /store "%BCD%" /set %G% device   ramdisk=[boot]\sources\boot.wim,{ramdiskoptions} >nul || goto :hata
bcdedit /store "%BCD%" /set %G% osdevice ramdisk=[boot]\sources\boot.wim,{ramdiskoptions} >nul || goto :hata
bcdedit /store "%BCD%" /set %G% path       \windows\system32\boot\winload.efi >nul || goto :hata
bcdedit /store "%BCD%" /set %G% systemroot \windows >nul || goto :hata
bcdedit /store "%BCD%" /set %G% detecthal  Yes >nul
bcdedit /store "%BCD%" /set %G% winpe      Yes >nul

:: --- TANI: sessiz cokmeyi gorunur kil ------------------------------------
bcdedit /store "%BCD%" /set %G% recoveryenabled    No  >nul
bcdedit /store "%BCD%" /set %G% bootstatuspolicy   IgnoreAllFailures >nul
bcdedit /store "%BCD%" /set %G% quietboot          No  >nul
bcdedit /store "%BCD%" /set %G% sos                On  >nul
bcdedit /store "%BCD%" /set %G% nx                 OptIn >nul

:: --- varyanta ozel ayar ---------------------------------------------------
if /i "%VAR%"=="tekcpu" (
  bcdedit /store "%BCD%" /set %G% numproc 1 >nul
  echo   AYAR: numproc 1  ^(tek cekirdek^)
)
if /i "%VAR%"=="azram" (
  bcdedit /store "%BCD%" /set %G% truncatememory 0x100000000 >nul
  echo   AYAR: truncatememory 4 GB
)

:: --- boot manager ---------------------------------------------------------
bcdedit /store "%BCD%" /create {bootmgr} /d "beyond2lte" >nul
bcdedit /store "%BCD%" /set {bootmgr} device boot >nul
bcdedit /store "%BCD%" /set {bootmgr} default %G%  >nul
bcdedit /store "%BCD%" /set {bootmgr} timeout 0    >nul
bcdedit /store "%BCD%" /displayorder %G% /addlast  >nul

echo.
echo === Sonuc
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
