@echo off
::
:: WinPE acilisinda OTOMATIK calisir (\Windows\System32\startnet.cmd).
::
:: IKI ISI VAR:
::
:: 1) CIKMAMAK. WinPE'nin kabugu cmd.exe'dir ve ciktigi anda makine yeniden
::    baslar - tasarim boyle. Cihazda gordugumuz "donen daire, sonra ekran
::    sonme animasyonu, sonra reset" tam olarak buydu: cokme degil, duzgun
::    kapanma. Sondaki sonsuz dongu bunu engelliyor.
::
:: 2) ICERIDEN BILGI ALMAK. Seri portumuz yok, ama WinPE calisiyorsa
::    depolamayi gorebilir. Gorurse bu log ESP'ye yazilir ve recovery'den
::    okunur - Linux tarafinda initramfs ile yaptigimiz seyin ayni.
::
:: wmic KULLANILMIYOR: yeni Windows surumlerinde kaldirildi. diskpart her
:: WinPE'de var.
::
set L=X:\b2l.txt

echo ================ beyond2lte WinPE > %L%
echo [1] startnet basladi >> %L%
call :durum

echo [2] wpeinit calisiyor >> %L%
wpeinit
echo [3] wpeinit bitti (RC=%errorlevel%) >> %L%
call :durum
call :kaydet

::
:: Ekrana da bas: depolama yoksa tek kanalimiz bu.
::
cls
echo ================ beyond2lte WinPE
echo.
type %L%
echo.
echo ================ Bu ekran kalici. Fotografla.
echo.

:bekle
ping -n 60 127.0.0.1 >nul
goto bekle

:durum
echo --- birimler >> %L%
echo list volume | diskpart >> %L% 2>&1
echo --- diskler >> %L%
echo list disk   | diskpart >> %L% 2>&1
goto :eof

::
:: ESP'yi bul ve logu oraya kopyala. Isaret olarak bootmgfw.efi'ye bakiyoruz,
:: surucu harfi WinPE'de degisebiliyor.
::
:kaydet
for %%d in (C D E F G H I J) do (
  if exist %%d:\EFI\Microsoft\Boot\bootmgfw.efi (
    echo [4] ESP bulundu: %%d: >> %L%
    copy /y %L% %%d:\WINLOG.TXT >nul 2>&1
  )
)
goto :eof
