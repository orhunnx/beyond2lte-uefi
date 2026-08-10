@echo -off
#
# SIFIRLAMA SEBEBI OKUMA MODU.
#
# Windows'u BASLATMIYOR. Amac, bir onceki acilisin nasil bittigini ogrenmek.
#
# Kullanim:
#   1) Bu imajla ac, ekrandaki "B2L RESET" satirlarini oku  (temiz acilis)
#   2) Windows imajini flasla, ac, cihaz sifirlansin
#   3) BU imaji tekrar flasla ve ac, "B2L RESET" satirlarini yeniden oku
#
# Ikisi arasindaki fark, Windows'un sifirlamayi KENDI mi istedigini yoksa
# donanimin mi zorla attigini soyler:
#
#   WARM=1        -> yazilim sicak sifirlama istedi. Windows'un ResetSystem
#                    cagrisi bu; yani kontrollu bir cokme (bugcheck yolu).
#   LITTLE_WDT=1  -> watchdog atti. Yazilim takilmis, donanim mudahale etmis.
#   ikisi de 0    -> soguk acilis; cok daha sert bir sey olmus.
#
# DEBUG derlemesi gerekiyor: RELEASE'de DEBUG() makrolari derlemeden cikiyor.
#     bash yap.sh -r DEBUG
#
mode 80 25

echo .
echo ================ SIFIRLAMA SEBEBI
echo .
echo Yukaridaki "B2L RESET" satirlarini fotografla.
echo Windows denemesinden SONRA bu imajla tekrar acip karsilastir.
echo .
pause
