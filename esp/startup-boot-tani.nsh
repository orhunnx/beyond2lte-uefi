@echo -off
#
# Windows bootmgfw kalici-bekleme tanisi.
#
# Bu yol B2LGOSTR, dmem ve sabit fiziksel adres kullanan eski tani kodunu
# calistirmaz. B2LBOOT.EFI yalnizca kendi FAT birimindeki BCD/bootmgfw'yi,
# GOP framebuffer'ini ve UEFI olaylarini kullanir.
#
mode 80 25
echo B2L Boot Tani basliyor.

if exist fs0:\B2LBOOT.EFI then
    fs0:\B2LBOOT.EFI
endif
if exist fs1:\B2LBOOT.EFI then
    fs1:\B2LBOOT.EFI
endif
if exist fs2:\B2LBOOT.EFI then
    fs2:\B2LBOOT.EFI
endif
if exist fs3:\B2LBOOT.EFI then
    fs3:\B2LBOOT.EFI
endif

echo B2LBOOT.EFI ile ayni birimde BCD veya bootmgfw bulunamadi.
pause
