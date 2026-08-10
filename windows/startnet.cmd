@echo off
::
:: WinPE acilisinda OTOMATIK calisir (\Windows\System32\startnet.cmd).
::
:: BU DOSYADA UC TUZAK VAR, UCU DE YASANDI:
::
:: 1) CRLF SATIR SONU ZORUNLU. Linux'ta LF ile yazilmisti; batch yorumlayicisi
::    etiketleri ve "goto"yu bozabiliyor.
::
:: 2) "ping" ILE BEKLEME YAPILMAZ. ping TCP/IP yiginina ihtiyac duyuyor ve
::    wpeinit calismadan once WinPE'de ag YOK - beklemeler aninda donuyor,
::    zamana dayali tani islemiyor. "timeout" ag gerektirmiyor.
::
:: 3) SONSUZ DONGU HICBIR DIS KOMUTA BAGLI OLMAMALI. Kabuk cikarsa WinPE
::    MAKINEYI YENIDEN BASLATIYOR; dongu bir sekilde kirilirsa her sey biter.
::    Sondaki dongu saf batch, hicbir exe cagirmiyor.
::
:: ZAMANLA TANI - cihazin donen daireden KAC SANIYE sonra yeniden basladigi
:: hangi asamada oldugunu soyler:
::    ~20 sn -> asama 1 gecti, wpeinit'te oldu
::    ~40 sn -> wpeinit gecti, diskpart'ta oldu
::    ~60 sn -> diskpart gecti, kopyalamada oldu
::    hic    -> hepsi gecti, bekliyor   <- HEDEF
::
set L=X:\b2l.txt

cls
echo ================ beyond2lte WinPE
echo [1] startnet basladi
echo ================ beyond2lte WinPE > %L%
echo [1] startnet basladi >> %L%
timeout /t 20 /nobreak >nul

echo [2] wpeinit
echo [2] wpeinit >> %L%
wpeinit
echo [2] wpeinit bitti
echo [2] wpeinit bitti >> %L%
timeout /t 20 /nobreak >nul

echo [3] diskpart
echo [3] diskpart >> %L%
echo list disk   | diskpart >> %L% 2>&1
echo list volume | diskpart >> %L% 2>&1
echo [3] diskpart bitti
echo [3] diskpart bitti >> %L%
timeout /t 20 /nobreak >nul

echo [4] ESP araniyor
echo [4] ESP araniyor >> %L%
for %%d in (C D E F G H I J) do if exist %%d:\EFI\Microsoft\Boot\bootmgfw.efi copy /y %L% %%d:\WINLOG.TXT >nul 2>&1

cls
echo ================ beyond2lte WinPE  [5] TAMAM
echo.
type %L%
echo.
echo Bu ekran kalici. Cihaz artik yeniden baslamamali.

::
:: Saf batch sonsuz dongu. Hicbir dis komut cagirmiyor, dolayisiyla
:: hicbir sekilde kirilamaz.
::
:bekle
goto bekle
