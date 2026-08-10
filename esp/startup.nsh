@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok; komutlar burada.
#
# SIRALAMA ONEMLI - ONCE EKRAN, SONRA DOSYA.
#
# Onceki surum once dosyaya yazmayi deniyordu. Yonlendirme basarisiz olunca
# Shell "Script Error Status: Invalid Parameter" verip SCRIPT'I KOMPLE
# DURDURUYOR. Yani yazma calismazsa yedek plan da calisamiyor ve tur bosa
# gidiyor. Onceki turda tam bu oldu.
#
# Simdi once her sey ekrana dokuluyor (bu kesin calisiyor), dosyaya yazma
# denemesi en sona birakildi. Orada olurse bir sey kaybetmiyoruz.
#
# EKRANDA DIKKAT: "echo" metnindeki yalniz "-" isareti bayrak saniliyor
# ("Unknown flag - '-'"). Metinlerde tire kullanma.
#
# Duzenle -> cache_esp.sh -> cache'i yaz. UEFI'yi yeniden derlemeye gerek yok.
#
# AMAC: ACPI tablolarimizi dogrulamak. Nihai hedef Windows on ARM ve Windows
# ACPI olmadan hic acilmaz.
#

mode 80 25

echo .
echo beyond2lte ACPI dogrulama
echo .

echo ==== 1/4  acpiview spec dogrulama (EN ONEMLISI)
acpiview -r 2
echo .
echo (devam icin ses tusu)
pause

echo ==== 2/4  MADT: CPU listesi
acpiview -s APIC
echo .
echo (devam icin ses tusu)
pause

echo ==== 3/4  GTDT ve FACP
acpiview -s GTDT
acpiview -s FACP
echo .
echo (devam icin ses tusu)
pause

echo ==== 4/4  kurulu tablolar ve dosya sistemi
acpiview -l
map -r
echo .
echo (devam icin ses tusu)
pause

#
# Buradan sonrasi dosyaya yazma denemesi. Basarisiz olursa script burada
# durur ama yukaridaki her sey zaten ekranda gorulmus olur.
#
# Onceki tur "Unable to redirect file" verdi. Sebebi henuz belli degil:
# UfsDxe'nin WriteBlocks yolu (kaynakta var, Media.ReadOnly FALSE) ya da
# FatPkg'in 4096 baytlik sektorle yazma davranisi. Android tarafindan ayni
# bolume dd ile yazilabiliyor, yani donanim yaziliabilir durumda.
#
echo .
echo ==== dosyaya yazma denemesi
echo YAZMA TESTI >a fs0:\LOG_00_test.txt
echo   yazma calisti. Kalan ciktilar dosyaya gidiyor.
acpiview -r 2 >a fs0:\LOG_03_acpi_verify.txt
acpiview -s APIC >a fs0:\LOG_04_madt.txt
acpiview -s GTDT >a fs0:\LOG_05_gtdt.txt
acpiview -s FACP >a fs0:\LOG_06_facp.txt
map -r >a fs0:\LOG_01_map.txt
memmap >a fs0:\LOG_09_memmap.txt
ls fs0:\LOG_*

echo .
echo Bitti.
pause
