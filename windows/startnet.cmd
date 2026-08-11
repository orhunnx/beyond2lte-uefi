@echo off
::
:: ===========================================================================
:: MESGUL BEKLEME KALDIRILDI - GERILEMEYI BU YAPMIS OLABILIR
:: ===========================================================================
::
:: Bu dosyanin onceki surumu soyle bitiyordu:
::
::     :bekle
::     goto bekle
::
:: Tek cekirdekte (numproc 1) bu, cmd.exe'yi %100 CPU ile sonsuza kadar
:: donduren bir mesgul bekleme. Telefonun tamami bos yere doner.
::
:: NEDEN ONEMLI: bu dosyayi WIM'e enjekte etmeden ONCE, cihazda donen daire
:: bir EKRAN SONME ANIMASYONUYLA kayboluyordu. STATUS.md'de o an dogru
:: yorumlanmis: animasyon cokme degil, KABUGUN DUZGUN CIKMASI. WinPE'nin
:: kabugu cmd.exe'dir ve ciktigi anda makine tasarim geregi yeniden baslar.
:: Yani o anda ntoskrnl ayakta, WinPE tamamen acilmis, oturum kurulup
:: kapanmisti. Cokme YOKTU.
::
:: Enjeksiyondan sonra animasyon kayboldu, yerine sert sifirlama geldi.
:: Yani "kabuk cikmasin" diye yazdigim sey muhtemelen onu olduren sey.
::
:: BU SURUM HIC DONGU KURMUYOR. Isini yapiyor, ekrana basiyor, tek bir
:: "pause" ile bekliyor. Girdi varsa orada durur, yoksa duser ve kabuk
:: cikar - o da animasyonlu sonme demektir, yani BASARI isareti.
::
:: Iki sonuc da bilgi:
::   animasyonlu sonme  -> WinPE ayakta, is bitti, kabuk cikti
::   sert sifirlama     -> hala gercek bir cokme var
::
:: wpeinit CAGRILMIYOR: aygit taramasi zaman aliyor ve daha once suphe
:: cekmisti. Surucu harfleri wpeinit olmadan da atanir.
::
cls
echo ================================================
echo   beyond2lte WinPE  -  startnet calisiyor
echo ================================================
echo.

::
:: Yazilabilir her birime iz birak. storufs baglandiysa ve SYSTEM birimi
:: gorunuyorsa bu dosya recovery'den okunabilir - Linux'ta initramfs ile
:: yaptigimizin aynisi.
::
:: "if exist" ONCE: olmayan surucu harfine yazmaya calismak cmd'yi
:: bekletebiliyor.
::
for %%d in (C D E F G H I J K L) do (
  if exist %%d:\ (
    echo Birim bulundu: %%d:
    echo WinPE ayakta. Kabuk calisti, dongu yok. > %%d:\WINLOG.TXT 2>nul
  )
)

echo.
echo Yukaridaki listede birim varsa depolama Windows tarafinda GORUNUYOR.
echo Hic birim yoksa storufs baglanmamis demektir.
echo.
echo Bir tusa bas. Girdi yoksa kabuk cikacak ve ekran animasyonla sonecek,
echo bu da WinPE'nin sorunsuz calistigi anlamina gelir.
echo.
pause
