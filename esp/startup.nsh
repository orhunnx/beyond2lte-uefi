@echo -off
#
# Windows'u dogrudan baslatir. Bekleme yok, ACPI dokumu yok.
#
# YONLENDIRME KULLANMA. Shell'in ">" ve ">a" operatorlerinin ikisi de bu
# platformda "Unable to redirect file / Invalid Parameter" veriyor ve
# SCRIPT'I OLDURUYOR. Dosya aslinda olusuyor ve yaziliyor (UfsDxe tani
# satirlari bunu gosterdi); sorun EDK2'nin yonlendirme kodunun hedefi
# acip-silip-yeniden-olusturma adiminda. Detay: STATUS.md
#
mode 80 25

if exist fs0:\EFI\Microsoft\Boot\bootmgfw.efi then
    echo Windows Boot Manager...
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W: Boot Manager geri dondu.
endif

if exist fs0:\initramfs-test.gz then
    echo Linux (ACPI, DTB yok)...
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif

echo Acilacak bir sey yok.
pause
