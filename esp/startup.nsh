@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok; komutlar burada.
#
# ACPI tablolari dogrulandi: acpiview -r 2 -> 3 hata, ucu de kasitli
# (MCFG/SPCR/DBG2). Artik ekran dokumune gerek yok, sira Linux'u ACPI ile
# baslatmakta.
#
# EKRANDA DIKKAT: "echo" metnindeki yalniz "-" isareti bayrak saniliyor
# ("Unknown flag - '-'"). Metinlerde tire kullanma.
#
# Duzenle -> cache_esp.sh -> cache'i yaz. UEFI'yi yeniden derlemeye gerek yok.
#

mode 80 25

echo .
echo beyond2lte
echo .

echo ==== ACPI ozeti
acpiview -r 2
echo .
echo (3 hata bekleniyor: MCFG, SPCR, DBG2)
echo (devam icin ses tusu)
pause

#
# Linux, ACPI ile. DTB VERILMIYOR - amac tam olarak tablolarimizin tek
# basina yeterli olup olmadigini gormek. Windows da ayni tablolari kullanacak.
#
# initramfs-test.gz kendi /init'ini calistirip dmesg'i ESP'ye KLOG.TXT olarak
# yaziyor, sonra cihazi kapatiyor. Boylece kernel ciktisini fotograflamadan,
# tam metin olarak alabiliyoruz (bkz. linux_test/init).
#
if exist fs0:\initramfs-test.gz then
    echo ==== Linux baslatiliyor (ACPI, DTB yok)
    echo Kernel log ESP'ye KLOG.TXT olarak yazilacak, sonra cihaz kapanacak.
    echo Kapandiktan sonra recovery'ye gecip cache bolumunu cek.
    echo .
    echo (baslatmak icin ses tusu)
    pause
    #
    # Bu satiri gorursen "pause" gecildi demektir. Sonrasinda ekran donarsa
    # kernel devraldi ama ciktisini basamiyor; hic gorunmezse tus algilanmadi.
    #
    echo ISARET A: kernel yukleniyor...
    #
    # earlycon PARAMETRESIZ KULLANILMAZ. Oyle yazilinca Linux seri portu
    # SPCR ya da DBG2 tablosundan okumaya calisiyor; bizde ikisi de yok,
    # dolayisiyla erken konsol hic kurulmuyor ve kernel acilirken tek satir
    # basamiyor. Ekranin donmus gorunmesinin sebebi buydu.
    # "efifb" diyerek framebuffer'i dogrudan gosteriyoruz.
    #
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
    #
    # Buraya donduysek kernel HIC BASLAMADI (yuklenemedi). Yukaridaki komutun
    # bastigi hata satiri sebebi soyler.
    #
    echo ISARET B: kernel geri dondu, yani baslatilamadi.
else
    echo initramfs-test.gz yok. Uretmek icin (WSL):
    echo   bash linux_test/kur.sh
    echo   bash cache_esp.sh
endif

echo .
echo Bitti.
pause
