@echo -off
#
# Once ONCEKI acilisin izlerini goster, sonra Windows'u ac.
#
# ------------------------------------------------------------------------
# BU DOSYA NEDEN BOYLE: dongu kapali, yeniden flaslama yok
# ------------------------------------------------------------------------
# RST_STAT son sifirlamayi tutuyor, ve Odin'in flas sonrasi yeniden
# baslatmasi da bir sifirlama. Ayri bir "okuyucu imaji" flaslasaydik,
# Windows'un biraktigi izi tam da onu okumaya giderken silerdik. Bu yuzden
# tek imaj: ayni script hem dokumu basiyor hem Windows'u aciyor.
#
#   acilis -> ONCEKI cokusun izleri -> tusa bas -> Windows -> coker
#          -> acilis -> O COKUSUN izleri
#
# ------------------------------------------------------------------------
# dmem BOYUTU ONALTILIK
# ------------------------------------------------------------------------
# Bir tur "dmem 0xCF000000 192" yazdim; 192 ondalik sanmistim, Shell 0x192
# (402 bayt) okudu ve bakmak istedigim +0x40 / +0x80 satirlari ekrandan
# tasti. Asagida boyutlar onaltilik ve kisa: her sey tek ekranda kaliyor.
# Bu yuzden "map -r" da kaldirildi - isini gormustu (fs0 LUN 0'da, GPT
# bolumleri orada; LBA 0 hatalari LUN 2'de, yani bizim FS'imiz degil).
#
mode 80 25

#
# ------------------------------------------------------------------------
# 1) PMU: SIFIRLAMANIN KAYNAGI
# ------------------------------------------------------------------------
# OLCULEN DEGERLER:
#   0x40010000  Odin flasindan sonraki ilk acilis
#   0x20000000  kurtarma menusunden kasitli yeniden baslatma
#   0x20000000  WINDOWS COKTUKTEN SONRA        <- ayni
#
# Register canli, acilis yoluna gore degisiyor. Ve Windows'un cokusu,
# kasitli bir yeniden baslatmayla ayni degeri uretiyor: sifirlamayi
# yazilim ISTIYOR, donanim zorlamiyor. Watchdog biti yok.
#
echo ================ PMU SIFIRLAMA KAYNAGI
echo RST_STAT  (0x15860404):
dmem 0x15860404 4
echo SEQUENCER (0x15860500):
dmem 0x15860500 4
echo .
echo 0x20000000 = yazilim istedi  /  0x40010000 = Odin yolu
echo .

#
# ------------------------------------------------------------------------
# 2) RESETSYSTEM KANCASININ KAYDI  (B2LResetHookDxe, 0xCF000000)
# ------------------------------------------------------------------------
# ARM64 UEFI'de yazilimin sifirlama isteyebilecegi tek arayuz ResetSystem,
# ve o bizim runtime servisimiz - ExitBootServices'ten sonra da bizde
# kaliyor. Kancaladik. Kanca ekrana yazamaz (ConOut yok), onun yerine
# 0xCF000000'a yaziyor; DRAM sicak sifirlamayi atlatiyor.
#
#   +0x40  "B2LI" (42 32 4C 49) -> surucu bu acilista yuklendi
#   +0x80  onceki cokusun kaydi:
#            +0x80 "B2LR" (42 32 4C 52) imza
#            +0x88 ResetType  0=Cold 1=Warm 2=Shutdown 3=Platform
#            +0x90 ResetStatus, 8 bayt little-endian  <- ASIL CEVAP
#            +0xA0 ResetData (UCS-2 metin olabilir)
#
# +0x40'ta imza yoksa surucu hic yuklenmemis (derleme/FDF sorunu).
# +0x40 var ama +0x80 yoksa kanca cagrilmamis: sifirlama ResetSystem'den
# GECMIYOR, daha alt seviyede - istisna, dogrudan SMC, ya da guc devresi.
#
echo ================ KANCA
echo +0x40 surucu imzasi:
dmem 0xCF000040 10
echo +0x80 onceki cokus:
dmem 0xCF000080 40
echo .

echo ================ FOTOGRAFLA, sonra tusa bas.
pause

if exist fs0:\EFI\Microsoft\Boot\bootmgfw.efi then
    echo Windows Boot Manager...
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W: Boot Manager geri dondu.
    pause
endif

echo Windows bulunamadi: cache_esp.sh --win calistirdin mi?
pause
