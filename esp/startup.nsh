@echo -off
#
# YAZMA TESTI MODU.
#
# UEFI'den UFS'e yazma calismiyor; tek tani kanalimiz buydu ve kapali.
# Bu script Windows'u BASLATMIYOR - sadece yazmayi deniyor ve UfsDxe'nin
# tani satirlarini ekranda birakiyor (tools_ufs_patch.py "B2L UFS:" onekiyle
# basiyor).
#
# Windows'a donmek icin: bu dosyayi eski haline al (git checkout).
#
# DIKKAT: DEBUG derlemesi gerekiyor. RELEASE'de DEBUG() makrolari derlemeden
# cikariliyor ve "B2L UFS:" satirlari hic basilmaz.
#     bash yap.sh -r DEBUG
#

mode 80 25

echo .
echo ================ UFS YAZMA TESTI
echo .

echo 1) Kucuk dosya yazmayi dene
echo beyond2lte yazma testi >a fs0:\YAZTEST.TXT

echo .
echo 2) Dosya olustu mu
if exist fs0:\YAZTEST.TXT then
    echo    EVET, yazma CALISIYOR
    echo    icerik:
    type fs0:\YAZTEST.TXT
else
    echo    HAYIR, yazma calismiyor
    echo    Yukaridaki "B2L UFS:" satirlari sebebi soyluyor:
    echo      WriteBlocks hic gorunmuyorsa  : FatPkg oraya kadar gelmiyor
    echo      ro=1 ise                      : Media.ReadOnly, LUN korumali
    echo      WRITE_10 basarisiz ise        : SCSI seviyesinde reddediliyor
endif

echo .
echo 3) Dizin olusturmayi dene
if not exist fs0:\YAZDIR then
    mkdir fs0:\YAZDIR
endif
if exist fs0:\YAZDIR then
    echo    dizin: TAMAM
else
    echo    dizin: BASARISIZ
endif

echo .
echo ================ Fotografla. Ustteki B2L UFS satirlari onemli.
pause
