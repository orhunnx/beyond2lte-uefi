@echo -off
#
# B2LUFS disk-boot bugcheck ucus paneli turu.
#
# B2LGoster once 0xCF050000'daki onceki Windows kaydini yorumlar ve tus
# bekler. Tusa basildiginda tampon ARMED yapilir. Debug BCD, winload'dan once
# Windows Boot Manager menusunu gosterir; menu SYSTEM/NTFS taramasinin
# bittigini kanitlar. 0x7B olursa miniport kaydi ayni ekrana cizer.
#
mode 80 25
echo B2LUFS bugcheck ucus paneli etkin.

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

echo B2LUFS ARMED. Sonraki checkpoint: Windows Boot Manager menusu.

if exist fs0:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs0
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo BOOTMGFW GERI DONDU: fs0
endif
if exist fs1:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs1
    fs1:\EFI\Microsoft\Boot\bootmgfw.efi
    echo BOOTMGFW GERI DONDU: fs1
endif
if exist fs2:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs2
    fs2:\EFI\Microsoft\Boot\bootmgfw.efi
    echo BOOTMGFW GERI DONDU: fs2
endif
if exist fs3:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs3
    fs3:\EFI\Microsoft\Boot\bootmgfw.efi
    echo BOOTMGFW GERI DONDU: fs3
endif

echo Acilacak Windows BCD bulunamadi.
pause
