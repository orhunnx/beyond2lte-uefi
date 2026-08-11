@echo -off
#
# Once ONCEKI acilisin izlerini goster, sonra ne varsa onu ac.
#
# ------------------------------------------------------------------------
# ECHO'DA "-" VE "/" KULLANMA
# ------------------------------------------------------------------------
# EFI Shell'in echo'su bir kelimenin basindaki tireyi VE egik cizgiyi bayrak
# saniyor. Once "----" ile "echo: Unknown flag - '-'" almistik, simdi de
# "0x20000000 = ... / 0x40010000 = ..." satirinda "Unknown flag - '/'".
# Metinlerde ikisi de yok artik.
#
# ------------------------------------------------------------------------
# dmem BOYUTU ONALTILIK
# ------------------------------------------------------------------------
# "dmem 0xCF000000 192" 0x192 = 402 bayt okur ve ekrani tasirir. Boyutlar
# onaltilik ve kisa.
#
mode 80 25

#
# ------------------------------------------------------------------------
# 1) PMU: SIFIRLAMANIN KAYNAGI
# ------------------------------------------------------------------------
# OLCULEN:
#   0x40010000  Odin flasindan sonraki acilis (guc cevrimi yolu)
#   0x20000000  kurtarma menusunden kasitli yeniden baslatma
#   0x20000000  WINDOWS COKTUKTEN SONRA        ayni deger
#
# Register acilis yoluna gore degisiyor, yani takili degil. Windows'un
# cokusu kasitli bir yeniden baslatmayla ayni izi birakiyor: sifirlamayi
# YAZILIM istiyor, watchdog atmiyor.
#
echo ================ PMU SIFIRLAMA KAYNAGI
echo RST_STAT  (0x15860404):
dmem 0x15860404 4
echo SEQUENCER (0x15860500):
dmem 0x15860500 4
echo .
echo 0x20000000 yazilim istedi, 0x40010000 guc cevrimi
echo .

#
# ------------------------------------------------------------------------
# 2) RESETSYSTEM KANCASI  (B2LResetHookDxe, 0xCF000000)
# ------------------------------------------------------------------------
# ILK TUR SONUCU:
#   +0x40 = 42 32 4C 49 ("B2LI"), sayac 0x403 -> 0x404 diye tam bir artti
#           => surucu her acilista calisiyor, bolge sifirlamayi atlatiyor
#   +0x80 = bastan sona FF, iki acilista da
#           => KANCA HIC CAGRILMADI
#
# Yani Windows sifirlamayi ResetSystem'den gecirmiyor. RST_STAT hala
# yazilim diyor; demek ki dogrudan SMC ile PSCI SYSTEM_RESET cagiriliyor
# ve UEFI tamamen atlanıyor.
#
# SIMDI SURUCU KILITLENME MODUNDA (B2L_HOOK_HANG 1). Kanca cagrilirsa
# hicbir bellege dokunmadan sonsuz donguye giriyor:
#   cihaz 12. saniyede DONARSA -> kanca cagriliyormus, sorun bizim yazmada
#   yine yeniden baslarsa      -> Windows ResetSystem'i gercekten atliyor
#
echo ================ KANCA
echo +0x40 surucu imzasi:
dmem 0xCF000040 10
echo +0x80 onceki cokus:
dmem 0xCF000080 40
echo .

#
# ------------------------------------------------------------------------
# 3) fs0 HANGI LUN'DA - LUN 2 SORUSUNU KAPATMAK ICIN
# ------------------------------------------------------------------------
# Ekranda surekli "B2L UFS: WriteBlocks LUN=2 LBA=0 ... Device Error" gecip
# duruyor. OCS=0x07 + response=0x01: istek denetleyiciye ulasiyor, UFS
# AYGITI reddediyor - LU yazma korumasi (Samsung boot LU'larini kilitler,
# genelde kalici ve yazilimla acilmaz).
#
# "map -r" GPT bolumlerinin LUN 0'da (cp_debug, userdata) ve LUN 3'te
# (cpefs) oldugunu gosterdi; LUN 2'de bolum yok. Yani bizim ESP'miz orada
# degil ve bootmgfw o hatalarin ardindan yoluna devam ediyor. Ama tam
# cikti ekrandan tastigi icin fs0'in kendisini hic goremedik.
#
# "map fs0" tek satir basar ve soruyu kalici olarak kapatir: aygit yolundaki
# VenHw(...,XX) kismindaki XX, fs0'in LUN numarasi.
#
echo ================ fs0 AYGIT YOLU
map fs0
echo .

echo ================ FOTOGRAFLA, sonra tusa bas.
pause

#
# ------------------------------------------------------------------------
# 3) NE VARSA ONU AC
# ------------------------------------------------------------------------
# LINUX DALI GERI GELDI. Bir tur "cache_esp.sh" (bayraksiz, yani Linux
# modu) ile flaslandi ama startup.nsh'te Linux dali kalmamisti; cihaz
# Windows'u aradi, bulamadi. Iki mod da destekleniyor artik:
#
#   cache_esp.sh        -> vmlinuz + initramfs var, Windows dosyalari yok
#   cache_esp.sh --win  -> tersi
#
# Linux once bakiliyor cunku bayraksiz mod acikca Linux modu.
#
if exist fs0:\initramfs-test.gz then
    echo Linux (ACPI, DTB yok)...
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
    echo ISARET L: kernel geri dondu.
    pause
endif

if exist fs0:\EFI\Microsoft\Boot\bootmgfw.efi then
    echo Windows Boot Manager...
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W: Boot Manager geri dondu.
    pause
endif

echo Acilacak bir sey yok. cache_esp.sh calistirdin mi?
pause
