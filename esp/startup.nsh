@echo -off
#
# EFI Shell acilisinda otomatik calisir. Klavye yok, komutlari buraya yaziyoruz.
# "pause" satirlarini SES TUSUYLA gecebilirsin (KeypadDxe -> SimpleTextIn).
#
# Duzenle -> cache_esp.sh calistir -> flash. Yeniden derlemeye gerek yok.
#

echo .
echo ================ 1/5  map -r : blok cihazlar ve dosya sistemleri
echo .
map -r
echo .
echo (devam icin ses tusu)
pause

echo .
echo ================ 2/5  fs0: kok dizin
echo .
ls fs0:\
echo .
pause

echo .
echo ================ 3/5  BlockIo tasiyan handle sayisi
echo .
dh -p BlockIo
echo .
pause

echo .
echo ================ 4/5  SimpleFileSystem handle'lari
echo .
dh -p SimpleFileSystem
echo .
pause

echo .
echo ================ 5/5  surum
echo .
ver
echo .
echo Bitti. Shell'e dusuyorsun.
pause
