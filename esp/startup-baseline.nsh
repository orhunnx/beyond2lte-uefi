@echo -off
#
# Windows temiz taban acilisi.
#
# Bilerek calistirmaz: B2LGOSTR.EFI, EBSTEST.EFI, dmem, bellek taramasi,
# runtime/EL3/GIC kayit okumasi veya baska bir B2L tani kodu. Ama Windows
# acilis yolunu normal startup.nsh ile ayni sekilde BCD uzerinden bulur.
#
# Bu dosya yalnizca:
#   bash cache_esp.sh --win --system --baseline
# ile, imaj icindeki startup.nsh adi olarak kullanilir.
#
mode 80 25
echo B2L temiz taban: B2L tani kodu yok.

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
