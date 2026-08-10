@echo off
::
:: WinPE acilisinda OTOMATIK calisir (\Windows\System32\startnet.cmd).
::
:: BU DOSYA CRLF SATIR SONLARIYLA YAZILIR. Linux'ta LF ile yazilmisti;
:: batch yorumlayicisi etiketleri ve "goto"yu LF ile bozabiliyor, script
:: yarida kirilinca cmd.exe cikiyor ve WinPE MAKINEYI YENIDEN BASLATIYOR.
:: tools_winboot.sh enjekte ederken unix2dos uyguluyor.
::
:: ZAMANLA TANI: ekrani goremiyor olabiliriz (WinPE masaustune gecince
:: ekran surucusu calismiyorsa kor kaliyoruz). O yuzden her asama arasinda
:: 25 saniye bekliyoruz. Cihazin donen daireden KAC SANIYE sonra yeniden
:: basladigi hangi asamada oldugunu soyler:
::
::    ~25 sn  -> asama 1 gecildi, wpeinit'te oldu
::    ~50 sn  -> wpeinit gecti, diskpart'ta oldu
::    ~75 sn  -> diskpart gecti, kopyalamada oldu
::    hic     -> her sey gecti, sonsuz dongude bekliyor  <- HEDEF
::
set L=X:\b2l.txt

cls
echo ================ beyond2lte WinPE
echo [1] startnet basladi
echo ================ beyond2lte WinPE > %L%
echo [1] startnet basladi >> %L%
ping -n 26 127.0.0.1 >nul

echo [2] wpeinit
echo [2] wpeinit >> %L%
wpeinit
echo [2] wpeinit bitti
echo [2] wpeinit bitti >> %L%
ping -n 26 127.0.0.1 >nul

echo [3] diskpart
echo [3] diskpart >> %L%
echo list disk   | diskpart >> %L% 2>&1
echo list volume | diskpart >> %L% 2>&1
echo [3] diskpart bitti
echo [3] diskpart bitti >> %L%
ping -n 26 127.0.0.1 >nul

echo [4] ESP araniyor
echo [4] ESP araniyor >> %L%
for %%d in (C D E F G H I J) do if exist %%d:\EFI\Microsoft\Boot\bootmgfw.efi copy /y %L% %%d:\WINLOG.TXT >nul 2>&1

cls
echo ================ beyond2lte WinPE  [5] TAMAM
echo.
type %L%
echo.
echo Bu ekran kalici. Cihaz artik yeniden baslamamali.

:bekle
ping -n 60 127.0.0.1 >nul
goto bekle
