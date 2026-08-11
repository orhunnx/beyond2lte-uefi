@echo -off
#
# Once onceki acilisin izlerini goster, sonra ne varsa onu ac.
#
# ------------------------------------------------------------------------
# BIRIME BAGIMLI DEGIL: fs0..fs3 taraniyor
# ------------------------------------------------------------------------
# Windows 11 23H2'nin boot.wim'i 742 MB ve 600 MiB'lik CACHE'e sigmiyor.
# LZX ile yeniden sikistirmak hic kazanc vermedi (742 -> 742): ISO'nunki
# zaten LZX, acilmis icerik 1650 MiB. Bu yuzden ESP artik SYSTEM bolumune
# (5.57 GB) gidiyor.
#
# Sonuc: cihazda IKI FAT birimi olabiliyor - eski cache ve yeni system.
# Shell startup.nsh'i hangisinde bulursa onu calistirir ve buyuk ihtimalle
# eskisini bulur. O yuzden burada sabit "fs0" YAZMIYORUZ; dort birimi de
# tariyoruz. Hangi kopya calisirsa calissin dogru yeri buluyor.
#
# "for" dongusu KULLANILMADI bilerek: bu Shell surumunun script icinde
# nasil davrandigini test etmedik ve her deneme bir Odin flasina mal
# oluyor. Acik yazmak cirkin ama risksiz.
#
# ------------------------------------------------------------------------
# ECHO'DA "-" VE "/" YOK
# ------------------------------------------------------------------------
# Shell'in echo'su kelime basindaki tireyi ve egik cizgiyi bayrak saniyor;
# ikisi de birer tur kaybettirdi ("Unknown flag").
#
# dmem BOYUTU ONALTILIK: "dmem <adr> 192" 402 bayt okur.
#
mode 80 25

#
# 1) PMU: sifirlamanin kaynagi
#    0x40010000 guc cevrimi  /  0x20000000 yazilim istedi
#    Windows cokusu her seferinde 0x20000000 veriyor.
#
echo ================ PMU
dmem 0x15860404 4

#
# 2) ResetSystem kancasi (B2LResetHookDxe, 0xCF000000)
#    +0x40 "B2LI" = surucu yuklendi   +0x80 "B2LR" = kanca atesledi
#    Simdiye kadar +0x80 hep bos: Windows ResetSystem'i cagirmiyor.
#
echo ================ KANCA
dmem 0xCF000040 10
dmem 0xCF000080 20

echo ================ BIRIMLER
map

echo .
echo ================ Fotografla, sonra tusa bas.
pause

#
# 0) EbsTest - varsa HER SEYDEN ONCE.
#
# ExitBootServices cagirip HICBIR SEY kurmayan bir kalp atisi. Boot
# servisleri kapandiktan sonra geriye yalnizca framebuffer'a kare cizen bir
# dongu kaliyor: ne zamanlayici, ne kesme, ne surucu. Her tik bir kare,
# yani kendi kendini zamanliyor - cihaz sifirlanirsa kareleri saymak
# yetiyor, kronometreye gerek yok.
#
# NEDEN: Linux ExitBootServices cagirip 120 saniye yasiyor ama her seyi
# kuruyor. Windows cagirip 15 saniyede oluyor. Aradaki ucuncu durum hic
# denenmedi. ~15 saniyede sifirlarsa sorun Windows'ta DEGIL demektir ve
# ACPI'de aramayi tamamen birakiriz.
#
# Dosya esp/ icinde yoksa bu blok atlanir ve normal acilis surer.
#
if exist fs0:\EBSTEST.EFI then
    echo EbsTest calisiyor. Kareleri say.
    fs0:\EBSTEST.EFI
    echo EbsTest geri dondu.
    pause
endif

#
# 3) Linux once: bayraksiz cache_esp.sh acikca Linux modu.
#
if exist fs0:\initramfs-test.gz then
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs1:\initramfs-test.gz then
    fs1:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs2:\initramfs-test.gz then
    fs2:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs3:\initramfs-test.gz then
    fs3:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif

#
# 4) Windows. sources\boot.wim ile birlikte arıyoruz: eski cache biriminde
#    bootmgfw kalmis ama boot.wim silinmis olabilir; o birimi secersek
#    "device not accessible" ile geri doneriz.
#
if exist fs0:\sources\boot.wim then
    echo Windows: fs0
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W0: geri dondu.
endif
if exist fs1:\sources\boot.wim then
    echo Windows: fs1
    fs1:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W1: geri dondu.
endif
if exist fs2:\sources\boot.wim then
    echo Windows: fs2
    fs2:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W2: geri dondu.
endif
if exist fs3:\sources\boot.wim then
    echo Windows: fs3
    fs3:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W3: geri dondu.
endif

echo Acilacak bir sey bulunamadi.
pause
