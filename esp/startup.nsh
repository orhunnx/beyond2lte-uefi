@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok; komutlar burada.
#
# SADE TUTULUYOR. Onceki surumde acilistan once acipview dokumu ve dort ayri
# "ses tusuna bas" beklemesi vardi; her Windows denemesinde bunlar tekrar
# tekrar bekleniyordu. ACPI tablolari zaten dogrulandi (acpiview -r 2 -> 3
# hata, ucu de kasitli), tekrar tekrar bakmaya gerek yok.
#
# Ayrica beyond2lte.dsc'deki PcdFrameBufferDelay 100000'den 0'a cekildi;
# o deger her debug satiri icin 100 ms bekletiyordu.
#
# Tanı gerekirse "acpi" satirlarini geri acmak yeterli.
#

mode 80 25

#
# Windows - varsa dogrudan gir, bekleme yok.
#
if exist fs0:\EFI\Microsoft\Boot\bootmgfw.efi then
    echo Windows Boot Manager baslatiliyor...
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W: Boot Manager geri dondu (calistirilamadi).
endif

#
# Linux - ACPI testi (Windows dosyalari yoksa)
#
if exist fs0:\initramfs-test.gz then
    echo Linux baslatiliyor (ACPI, DTB yok)...
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
    echo ISARET L: kernel geri dondu.
endif

#
# Ikisi de yoksa: ACPI ozeti (eski tani modu)
#
echo Ne Windows ne Linux dosyasi var. ACPI ozeti:
acpiview -r 2
pause
