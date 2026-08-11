@echo off
::
:: ===========================================================================
:: EKRANA YAZMIYORUZ - GORUNMUYOR
:: ===========================================================================
:: Onceki surumler ekrana echo'luyor ve sonunda "pause" ile bekliyordu.
:: Sonuc: siyah ekran, hicbir yazi, ve animasyonlu kapanma da YOK.
::
:: Sebep: WinPE'de konsol ciktisini ekrana cizen sey win32k ve GORUNTU
:: SURUCUSU. Bizde calisan bir goruntu surucusu yok - bootvid'in daireyi
:: cizdigi framebuffer'i Windows'un konsolu kullanamiyor. Yani echo satirlari
:: bosluga gidiyor, sonra pause'a gelip SONSUZA KADAR bekliyor. Kabuk
:: cikmadigi icin cihaz kapanmiyor ve animasyon gorunmuyor.
::
:: O siyah ekran aslinda AYAKTA DURAN BIR WinPE. Sadece konusamiyor.
::
:: KANIT: stock startnet.cmd (tek satir "wpeinit") ile animasyonlu kapanma
:: geliyordu - cunku o ekrana hic yazmiyor ve beklemiyor, cmd cikiyor.
::
:: ===========================================================================
:: BU SURUM
:: ===========================================================================
:: Ekrana hic yazmiyor, hic beklemiyor. Yaptigi tek sey her surucu harfine
:: bir iz dosyasi birakmak ve cikmak.
::
::   cikista animasyonlu sonme  -> script sonuna kadar CALISTI
::   WINLOG.TXT ESP'de bulunur  -> storufs bagli, DEPOLAMA CALISIYOR
::
:: Ikinci madde projenin otuz turdur bekledigi cevap. UEFI linki PWM Gear
:: 1'e cekildikten sonra ilk kez gercek bir sansi var: UniPro spec'ine gore
:: LINKSTARTUP sonrasi link zaten PWM G1'de olur, yani storufs'un jenerik
:: pazarligi vendor kalibrasyonu gerektirmiyor.
::
:: "if exist" KULLANMIYORUZ: olmayan surucu harfinde bekletebiliyor ve zaten
:: yazma denemesinin kendisi testin ta kendisi. Hatalar nul'a gidiyor.
::
wpeinit

for %%d in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
  echo B2L WinPE ayakta. storufs bagli, bu birim yazilabilir.> %%d:\WINLOG.TXT 2>nul
)

::
:: Burada bitiyor. cmd cikacak, WinPE tasarim geregi kapanacak ve ekran
:: animasyonla sonecek - bu bizim "script sonuna kadar kostu" isaretimiz.
::
