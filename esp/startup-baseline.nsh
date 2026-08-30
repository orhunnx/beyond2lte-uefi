@echo -off
#
# Windows temiz taban acilisi.
#
# Bilerek calistirmaz: B2LGOSTR.EFI, EBSTEST.EFI, dmem, bellek taramasi ve
# runtime/EL3/GIC kayit okumasi. Tek istisna, yap.sh --usb-test secildiginde
# ESP'ye acikca konan salt-okuma B2LUSB.EFI uygulamasidir. Windows acilis
# yolunu normal startup.nsh ile ayni sekilde BCD uzerinden bulur.
#
# Bu dosya yalnizca:
#   bash cache_esp.sh --win --system --baseline
# ile, imaj icindeki startup.nsh adi olarak kullanilir.
#
mode 80 25
echo B2L temiz taban. USB testi varsa acikca secilmistir.

# --usb-test acik oldugunda salt-okuma USB uygulamasi temiz Windows yolundan
# once calisir. Dosya yoksa bu blok tamamen etkisizdir.
if exist fs0:\B2LUSB.EFI then
    fs0:\B2LUSB.EFI
endif
if exist fs1:\B2LUSB.EFI then
    fs1:\B2LUSB.EFI
endif
if exist fs2:\B2LUSB.EFI then
    fs2:\B2LUSB.EFI
endif
if exist fs3:\B2LUSB.EFI then
    fs3:\B2LUSB.EFI
endif

if exist fs0:\B2LTOUCH.EFI then
    fs0:\B2LTOUCH.EFI
endif
if exist fs1:\B2LTOUCH.EFI then
    fs1:\B2LTOUCH.EFI
endif
if exist fs2:\B2LTOUCH.EFI then
    fs2:\B2LTOUCH.EFI
endif
if exist fs3:\B2LTOUCH.EFI then
    fs3:\B2LTOUCH.EFI
endif

if exist fs0:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs0
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
endif
if exist fs1:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs1
    fs1:\EFI\Microsoft\Boot\bootmgfw.efi
endif
if exist fs2:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs2
    fs2:\EFI\Microsoft\Boot\bootmgfw.efi
endif
if exist fs3:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs3
    fs3:\EFI\Microsoft\Boot\bootmgfw.efi
endif

echo Acilacak Windows BCD bulunamadi.
pause
