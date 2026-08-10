@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok; komutlar burada.
# "pause" satirlarini SES TUSUYLA gecebilirsin (KeypadDxe -> SimpleTextIn).
#
# Duzenle -> cache_esp.sh -> cache'i yaz. UEFI'yi yeniden derlemeye gerek yok.
#
# AMAC: ACPI tablolarimizi dogrulamak. Nihai hedef Windows on ARM ve Windows
# ACPI olmadan hic acilmaz. Once tablolari burada, sonra Linux'la test ediyoruz.
#

echo .
echo ================ 1/4  ACPI tablolari kurulu mu
echo .
acpiview -l
echo .
echo (devam icin ses tusu)
pause

echo .
echo ================ 2/4  ACPI tablolarini spec'e karsi dogrula
echo .
# -r 2 : ACPI 6.x kurallarina gore denetle. Hata/uyari sayisini sonda yazar.
acpiview -r 2
echo .
echo Yukarida "0 Errors, 0 Warnings" gormek istiyoruz.
pause

echo .
echo ================ 3/4  MADT (CPU'lar) ve GTDT (timer) ayrintisi
echo .
acpiview -s APIC
pause
acpiview -s GTDT
pause

echo .
echo ================ 4/4  Linux ACPI testi
echo .
if exist fs0:\vmlinuz-lts then
    echo Kernel bulundu, ACPI ile aciliyor (DTB YOK).
    echo Beklenen: ACPI ayristirilir, CPU'lar kurulur, root bulunamayip panic.
    echo Panic BASARIDIR - oraya kadar gelmek ACPI'nin calistigini gosterir.
    echo .
    pause
    fs0:\vmlinuz-lts initrd=\initramfs-lts console=tty0 earlycon acpi=force loglevel=7 keep_bootcon
else
    echo Kernel yok. Indirmek icin (WSL, proje icinde):
    echo   cd esp
    echo   curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/vmlinuz-lts
    echo   curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/initramfs-lts
    echo   bash cache_esp.sh
    echo .
    echo Shell'e dusuluyor.
endif

pause
