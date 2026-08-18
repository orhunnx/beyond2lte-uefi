@echo -off
#
# B2LUFS disk-boot ucus kaydi turu.
#
# B2LGoster once 0xCF050000'daki onceki Windows kaydini yorumlar ve tus
# bekler. Ilk turda tusa basildiginda tampon ARMED yapilir; Windows 0x7B ile
# durursa cihazi KAPATMADAN sicak yeniden baslat. Ikinci ekrani fotografladik-
# tan sonra tusa basmak bir sonraki denemeyi yeniden arm eder.
#
mode 80 25
echo B2LUFS sicak reset ucus kaydi etkin.

if exist fs0:\B2LGOSTR.EFI then
    fs0:\B2LGOSTR.EFI
endif
if exist fs1:\B2LGOSTR.EFI then
    fs1:\B2LGOSTR.EFI
endif
if exist fs2:\B2LGOSTR.EFI then
    fs2:\B2LGOSTR.EFI
endif
if exist fs3:\B2LGOSTR.EFI then
    fs3:\B2LGOSTR.EFI
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
