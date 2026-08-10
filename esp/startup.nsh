@echo -off
#
# SIFIRLAMA SEBEBI - Shell'in "mem" komutuyla dogrudan okunuyor.
#
# ONCEKI DENEME NEDEN GORUNMEDI: sebebi BoardInit.c'den DEBUG ile
# yazdiriyorduk, ama FrameBufferDelay'i 0'a cektigimiz icin DXE logu
# ekrandan uctu gitti ve Shell acildiginda o satirlar coktan kaymisti.
#
# Burada firmware'e hic dokunmuyoruz: PMU registerini Shell okuyor ve
# ekranda EN SONDA kaliyor.
#
# PMU (system-controller@15860000, DTB'den):
#   +0x404  RST_STAT   son sifirlamanin KAYNAGI, sifirlamayi asar
#   +0x500  SEQUENCER
#
# RST_STAT'ta bakacagimiz bitler:
#   bit 28 (0x10000000)  WARM_RESET        yazilim sicak sifirlama istedi
#                                          -> Windows'un ResetSystem cagrisi
#   bit 24 (0x01000000)  LITTLE_WDT_RESET  watchdog atti, yazilim takilmis
#
# KULLANIM - uc adim:
#   1) Bu imajla ac, degeri not et            (temiz acilis referansi)
#   2) Windows imajini flasla, ac, sifirlansin
#   3) Bu imaji tekrar flasla, ac, degeri karsilastir
#
mode 80 25

echo .
echo ================ PMU SIFIRLAMA DURUMU
echo .

echo RST_STAT  (0x15860404):
mem 0x15860404 4 ;MMIO

echo .
echo SEQUENCER (0x15860500):
mem 0x15860500 4 ;MMIO

echo .
echo Aranan bitler:  0x10000000 = WARM (yazilim istedi)
echo                 0x01000000 = WDT  (watchdog atti)
echo .
echo ================ Fotografla.
pause
