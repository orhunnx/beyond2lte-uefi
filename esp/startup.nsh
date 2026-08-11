@echo -off
#
# TEK IMAJ: once ONCEKI acilisin sifirlama sebebini goster, sonra Windows'u ac.
#
# ------------------------------------------------------------------------
# UC ADIMLI PLAN NEDEN CALISMIYORDU
# ------------------------------------------------------------------------
# Plan soyleydi: okuyucu imajini flasla-oku, Windows imajini flasla-coksun,
# okuyucuyu tekrar flasla-oku. Ama RST_STAT SON sifirlamayi tutuyor ve
# Odin'in flas sonrasi yeniden baslatmasi da bir sifirlama. Yani Windows'un
# biraktigi izi, tam da onu okumaya giderken kendi elimizle siliyorduk.
#
# Bu surum donguyu kapatiyor. Yeniden flaslama yok:
#
#   acilis -> RST_STAT'i goster (bu ONCEKI cokusun sebebi) -> tusa bas
#          -> Windows acilir -> coker -> cihaz sifirlanir
#          -> acilis -> RST_STAT artik O COKUSUN sebebini gosteriyor
#
# Yani ILK acilis Odin'in yeniden baslatmasini gosterir (0x20000000),
# IKINCI acilis Windows'un cokusunu gosterir. Fotograflanacak olan ikincisi.
#
# ------------------------------------------------------------------------
# REFERANS DEGER (olculdu)
# ------------------------------------------------------------------------
#   0x20000000  = kurtarma menusunden yeniden baslatma (yazilim sifirlamasi)
#                 bit 29. DIKKAT: Mu-Silicium'un 9830'dan aldigi
#                 WARM_RESET = bit 28 sabiti bu SoC icin yanlis gorunuyor.
#   0x00000600  = SEQUENCER, ayni acilista
#
# Windows coktukten sonra ARADIGIMIZ SEY: deger 0x20000000'den FARKLI mi?
#   ayni (0x20...)    -> Windows sifirlamayi kendi istedi: kontrollu bugcheck
#                        -> ResetSystem kancasi dogrudan ise yarar
#   0x01000000 (bit24)-> watchdog atti: yazilim TAKILDI, kimse istemedi
#                        -> kesme yonlendirmesi / GIC tarafina bakariz
#   0x00000001 (bit 0)-> soguk sifirlama: guc kesildi, PMIC/regulator
#   baska bir sey     -> yeni bir kaynak, DTB'deki PMU node'una geri doneriz
#
# PMU adresleri (system-controller@15860000, stock DTB'den):
#   +0x404  RST_STAT     son sifirlamanin kaynagi
#   +0x500  SEQUENCER
#
# NOT: komutun adi "dmem", ve bu Shell surumu ";MMIO" ekini kabul etmiyor
# ("dmem: Too many arguments" veriyordu). Eksiz kullaniyoruz.
#
mode 80 25

#
# fs0 HANGI LUN'DA? Tek satir, bedava, ve bir soruyu kapatiyor.
#
# Ekranda "B2L UFS: WriteBlocks LUN=2 LBA=0 ... OCS=0x07 -> Device Error"
# goruldu. OCS_FATAL_ERROR + response=0x01: istek denetleyiciye ulasiyor,
# AYGIT reddediyor. Basarisiz olanlarin hepsi LBA=0; daha once basarili
# gordugumuz yazmalar (dosya verisi, dizin girdisi) sifirdan buyuk
# adreslerdeydi. Yani LU'nun ilk blogu aygit tarafinda yazma korumali
# gorunuyor (Samsung UFS'te bLUWriteProtect ile olagan).
#
# Eksik parca: LUN 2 bizim ESP'mizin LU'su mu, baska bir LU mu? map -r
# fs0'in aygit yolunu basiyor; icinde LUN numarasi ve bolum (HD(...)) olup
# olmadigi goruluyor. Bolum yoksa LBA 0 = FAT onyukleme sektoru, varsa
# LBA 0 = LU'nun basi, yani bizim dosya sistemimiz bile degil.
#
echo ================ fs0 HANGI LUN
map -r
echo .

#
# ------------------------------------------------------------------------
# 1) RESETSYSTEM KANCASININ KAYDI  (B2LResetHookDxe)
# ------------------------------------------------------------------------
# RST_STAT olcumu sifirlamanin YAZILIM kaynakli oldugunu gosterdi (temiz
# yeniden baslatma da, Windows cokusu de 0x20000000; watchdog yok, soguk
# sifirlama yok). ARM64 UEFI'de yazilimin sifirlama istemesinin tek yolu
# ResetSystem - ve o bizim runtime servisimiz. Kancaladik.
#
# Kanca ExitBootServices'ten sonra calisiyor, ekrana yazamaz. Onun yerine
# 0xCF000000'a yaziyor; DRAM sicak sifirlamayi atlatiyor (ramoops mantigi).
#
# YERLESIM:
#   +0x00  bu acilisin kaydi (surucu her acilista temizler)
#   +0x40  42 32 4C 49 = "B2LI" -> surucu yuklendi
#   +0x80  ONCEKI acilisin kaydi  <-- BAKILACAK YER
#
# +0x80'de ne aranacak:
#   42 32 4C 52 = "B2LR"  -> Windows ResetSystem cagirdi. Devaminda:
#        +0x84 Seq, +0x88 ResetType (0=Cold 1=Warm 2=Shutdown 3=Platform),
#        +0x90 ResetStatus (8 bayt, little-endian) -- ASIL CEVAP BU
#        +0xA0 ResetData (genelde UCS-2 metin)
#   imza yoksa            -> kanca hic cagrilmadi; sifirlama ResetSystem'den
#        gecmiyor, daha alt seviyede (istisna / dogrudan SMC / guc devresi)
#
echo ================ KANCA KAYDI  (0xCF000000)
dmem 0xCF000000 192
echo .

echo .
echo ================ ONCEKI ACILISIN SIFIRLAMA SEBEBI
echo .
echo RST_STAT  (0x15860404):
dmem 0x15860404 4
echo .
echo SEQUENCER (0x15860500):
dmem 0x15860500 4
echo .
echo Referans: 0x20000000 = normal yazilim yeniden baslatmasi
echo .
echo ================ FOTOGRAFLA, sonra tusa bas: Windows acilacak.
pause

#
# Windows. Coktugunde cihaz sifirlanacak ve yukaridaki dokum bu kez
# COKUSUN sebebini gosterecek. Yeniden flaslamaya gerek yok.
#
if exist fs0:\EFI\Microsoft\Boot\bootmgfw.efi then
    echo Windows Boot Manager...
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W: Boot Manager geri dondu.
    pause
endif

echo Windows bulunamadi: cache_esp.sh --win ile imaji uretmeyi unuttun mu?
pause
