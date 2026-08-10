@echo off
::
:: EN SADE HALI - wpeinit YOK.
::
:: NEDEN: _CLS kapatilinca (storufs.sys artik UFS0'a baglanmiyor) cihaz
:: 5-6 saniye yerine 12 SANIYE dayandi. Sure ikiye katlandi, yani storufs
:: supheli dogrulandi.
::
:: 12 saniye ayrica sunu soyluyor: bu script muhtemelen CALISIYOR. Ilk
:: beklememiz 20 saniyeydi; ona ulassaydik daha uzun dayanirdik. Demek ki
:: "timeout" aninda donuyor (WinPE'de ag yokken oluyor) ve hemen wpeinit'e
:: geciyoruz - wpeinit'in aygit taramasi tam olarak bu kadar suruyor.
::
:: Bu surum wpeinit'i HIC CAGIRMIYOR. Sonuc ikiye ayiriyor:
::   cihaz ARTIK YENIDEN BASLAMIYORSA  -> sucllu wpeinit, WinPE ayakta demektir
::   yine ~12 saniyede sifirliyorsa    -> script hic calismiyor, baska yerde
::
:: Hicbir dis komut cagirmiyoruz; timeout, ping, diskpart hepsi disarida.
::
cls
echo ================================================
echo   beyond2lte WinPE  -  startnet calisiyor
echo   wpeinit CAGIRILMADI
echo ================================================
echo.
echo Bu ekran kaliciysa WinPE bu telefonda AYAKTA.
echo.

:: Depolama gorunuyorsa ESP'ye iz birak (storufs bagli degil, muhtemelen yok)
for %%d in (C D E F G H I J) do if exist %%d:\EFI\Microsoft\Boot\bootmgfw.efi echo WinPE ayakta, wpeinit yok > %%d:\WINLOG.TXT

echo Bekleme dongusune giriliyor.

:bekle
goto bekle
