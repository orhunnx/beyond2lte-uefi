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
