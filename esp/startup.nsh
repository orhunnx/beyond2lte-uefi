@echo -off
#
# YONLENDIRME BICIMI TESTI.
#
# ONCEKI TESHIS YANLISTI. "UEFI'den UFS'e yazma calismiyor" demistim; enstrumante
# edilmis UfsDxe bunun tersini gosterdi:
#
#   B2L UFS: WriteBlocks LUN=0 LBA=1983528 boyut=4096 blok=4096 ro=0
#   B2L UFS: WriteBlocks sonuc = Success
#   FSOpen: Created new directory entry 'YAZTEST.TXT'
#
# Yazma, FAT, dizin girdisi hepsi calisiyor. Basarisiz olan SHELL'IN
# YONLENDIRME mekanizmasi:
#
#   Unable to redirect file.
#   Script Error Status: Invalid Parameter (line number 20)   <- ">a" satiri
#
# Bu script iki bicimi ayri ayri deniyor. Yonlendirme hatasi script'i
# OLDURDUGU icin siralama onemli: once ">" (supheli olmayan), sonra ls,
# en sonda ">a".
#

mode 80 25

echo .
echo ================ YONLENDIRME TESTI
echo .

echo 1) Duz ">" (UCS-2) deneniyor
echo duz yonlendirme > fs0:\T1.TXT

echo 2) Buraya geldiysek ">" CALISTI
if exist fs0:\T1.TXT then
    echo    T1.TXT olustu
else
    echo    T1.TXT YOK
endif

echo .
echo 3) Kokteki dosyalar
ls fs0:\

echo .
echo 4) Simdi ">a" (ASCII) deneniyor. Script burada olebilir.
echo ascii yonlendirme >a fs0:\T2.TXT

echo 5) Buraya geldiysek ">a" da CALISTI
ls fs0:\

echo .
echo ================ Bitti. Fotografla.
pause
