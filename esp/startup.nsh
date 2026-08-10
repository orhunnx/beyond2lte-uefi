@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok; komutlar burada.
#
# CIKTI DOSYAYA YAZILIYOR. Recovery calistigi icin ekrani fotograflamiyoruz:
#   1) Bu script fs0:\LOG_*.txt dosyalarini uretir
#   2) Recovery'ye gir, cache bolumunu cek, dosyalari oku (bkz. LOG_CEKME.md)
#
# ">a" ASCII yazar. Duz ">" UCS-2 yazar ve PC'de okumak zorlasir.
#
# NEDEN ALT DIZIN YOK: onceki surum "mkdir fs0:\log" yapip icine yaziyordu.
# mkdir basarisiz olursa dokuz yonlendirmenin hepsi birden sessizce duser ve
# geriye hicbir iz kalmaz. Koke yazinca o hata modu yok.
#
# Duzenle -> cache_esp.sh -> cache'i yaz. UEFI'yi yeniden derlemeye gerek yok.
#
# AMAC: ACPI tablolarimizi dogrulamak. Nihai hedef Windows on ARM ve Windows
# ACPI olmadan hic acilmaz.
#

mode 80 25

echo .
echo beyond2lte - ACPI dogrulama
echo .

#
# Once yazma testi. UfsDxe'nin BlockIo yazma yolu hic denenmedi; calismiyorsa
# bunu simdi, ekranda ogrenelim - 20 dakika sonra bos bir imajla degil.
#
echo YAZMA TESTI >a fs0:\LOG_00_test.txt
if exist fs0:\LOG_00_test.txt then
    echo   yazma: TAMAM
else
    echo   yazma: BASARISIZ - UFS yazma calismiyor, log dosyaya yazilamaz.
    echo   Bu durumda komutlari ekrana dokup fotograflamaya donecegiz.
endif
echo .

echo [1/6] map -r
map -r >a fs0:\LOG_01_map.txt

echo [2/6] acpiview -l  (kurulu tablolar)
acpiview -l >a fs0:\LOG_02_acpi_list.txt

echo [3/6] acpiview -r 2  (spec dogrulama)
acpiview -r 2 >a fs0:\LOG_03_acpi_verify.txt

echo [4/6] acpiview -s APIC  (MADT: CPU'lar)
acpiview -s APIC >a fs0:\LOG_04_madt.txt

echo [5/6] acpiview -s GTDT + FACP
acpiview -s GTDT >a fs0:\LOG_05_gtdt.txt
acpiview -s FACP >a fs0:\LOG_06_facp.txt

echo [6/6] handle, surum, bellek haritasi
dh -p SimpleFileSystem >a fs0:\LOG_07_fs.txt
ver >a fs0:\LOG_08_ver.txt
memmap >a fs0:\LOG_09_memmap.txt

echo .
echo Yazilan dosyalar:
ls fs0:\LOG_*
echo .

#
# Linux ACPI testi - kernel varsa
#
if exist fs0:\vmlinuz-lts then
    echo Kernel bulundu. ACPI ile aciliyor (DTB YOK).
    echo Beklenen: ACPI ayristirilir, CPU'lar kurulur, root bulunamayip panic.
    echo Panic BASARIDIR - oraya kadar gelmek ACPI'nin calistigini gosterir.
    echo Bu asamada cikti ekranda kalir, dosyaya yazilamaz.
    echo .
    echo (baslatmak icin ses tusu, atlamak icin bekleme yok)
    pause
    fs0:\vmlinuz-lts initrd=\initramfs-lts console=tty0 earlycon acpi=force loglevel=7 keep_bootcon
else
    echo Kernel yok. Indirmek icin (WSL, proje icinde):
    echo   cd esp
    echo   curl -LO .../aarch64/netboot/vmlinuz-lts
    echo   curl -LO .../aarch64/netboot/initramfs-lts
endif

echo .
echo Bitti. Simdi recovery'ye gecip log'lari cekebilirsin.
pause
