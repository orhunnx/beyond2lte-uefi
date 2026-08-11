@echo -off
#
# Once onceki acilisin izlerini goster, sonra ne varsa onu ac.
#
# ------------------------------------------------------------------------
# BIRIME BAGIMLI DEGIL: fs0..fs3 taraniyor
# ------------------------------------------------------------------------
# Windows 11 23H2'nin boot.wim'i 742 MB ve 600 MiB'lik CACHE'e sigmiyor.
# LZX ile yeniden sikistirmak hic kazanc vermedi (742 -> 742): ISO'nunki
# zaten LZX, acilmis icerik 1650 MiB. Bu yuzden ESP artik SYSTEM bolumune
# (5.57 GB) gidiyor.
#
# Sonuc: cihazda IKI FAT birimi olabiliyor - eski cache ve yeni system.
# Shell startup.nsh'i hangisinde bulursa onu calistirir ve buyuk ihtimalle
# eskisini bulur. O yuzden burada sabit "fs0" YAZMIYORUZ; dort birimi de
# tariyoruz. Hangi kopya calisirsa calissin dogru yeri buluyor.
#
# "for" dongusu KULLANILMADI bilerek: bu Shell surumunun script icinde
# nasil davrandigini test etmedik ve her deneme bir Odin flasina mal
# oluyor. Acik yazmak cirkin ama risksiz.
#
# ------------------------------------------------------------------------
# ECHO'DA "-" VE "/" YOK
# ------------------------------------------------------------------------
# Shell'in echo'su kelime basindaki tireyi ve egik cizgiyi bayrak saniyor;
# ikisi de birer tur kaybettirdi ("Unknown flag").
#
# dmem BOYUTU ONALTILIK: "dmem <adr> 192" 402 bayt okur.
#
mode 80 25

#
# 1) PMU: sifirlamanin kaynagi
#    0x40010000 guc cevrimi  /  0x20000000 yazilim istedi
#    Windows cokusu her seferinde 0x20000000 veriyor.
#
#
# PMU DOKUMU EN SONA TASINDI - VE BU BIR TURA MAL OLDU.
#
# En basta basiliyordu; altindaki "ls", "map" ve 0x60 baytlik dmem ciktisi
# 25 satirlik ekrani asip PMU'yu yukari kaydiriyordu. Aradigimiz tek deger
# ekranda hic gorunmuyordu.
#
# "ls fs0:\" ve "map fs0" da kaldirildi: ikisi de gorevini coktan yapti
# (EBSTEST.EFI'nin imajda olup olmadigi ve fs0'in hangi LUN oldugu
# sorulari kapandi). Her satir, gormek istedigimiz satiri asagi itiyor.
#
#
# B2LGOSTER - kayitlari BUYUK harflerle ve YORUMLANMIS olarak cizer.
#
# Konsolun yazi tipi 8x19'a sabit ve buyutulemez (EFI_GLYPH_WIDTH MdePkg'de
# sabit, glif bitmapleri de o boyutta uretiliyor). Bu uygulama glifleri HII
# font protokolunden alip her pikseli 4x4 blok olarak framebuffer'a basiyor.
#
# Ayrica onaltilik dokum yerine anlamini yaziyor: "SEH KAYIT=00000000 KABUL".
#
# Ekrani temizleyip cizdikten sonra tus bekliyor. Tusa basinca asagidaki
# dmem dokumleri her zamanki gibi devam ediyor - biri digerinin yerine
# gecmiyor, ham veri yedek olarak duruyor.
#
if exist fs0:\B2LGOSTR.EFI then
    fs0:\B2LGOSTR.EFI
endif
if exist fs1:\B2LGOSTR.EFI then
    fs1:\B2LGOSTR.EFI
endif
if exist fs2:\B2LGOSTR.EFI then
    fs2:\B2LGOSTR.EFI
endif
if exist fs3:\B2LGOSTR.EFI then
    fs3:\B2LGOSTR.EFI
endif

echo ================ B2LFB KAYDI

#
# 2) ResetSystem kancasi (B2LResetHookDxe, 0xCF000000)
#    +0x40 "B2LI" = surucu yuklendi   +0x80 "B2LR" = kanca atesledi
#    Simdiye kadar +0x80 hep bos: Windows ResetSystem'i cagirmiyor.
#
#
# KANCA DOKUMU KALDIRILDI: B2LResetHookDxe bilinen iyi noktaya donerken
# geri alindi, yani 0xCF000000 artik bellek haritasinda bile yok. O adresi
# okumanin bir anlami kalmadi.
#
# YERINE KOK LISTESI. Gerekce: "cp out/EbsTest.efi esp/EBSTEST.EFI" yapildi,
# cache_esp.sh dosyayi kopyaladigini bildirdi, ama cihazda
# "if exist fs0:\EBSTEST.EFI" YANLIS dondu ve uygulama hic calismadi.
# Ikisinden biri yaniliyor. Tahmin etmek yerine birimin kokunu okutuyoruz:
# dosya listede varsa sorun "if exist" ifadesinde, yoksa imaja hic girmemis.
#
echo .
#
# EL2 SMC KAYDI  (B2LEl2Dxe + B2LEl2Payload.S, 0xCF030000)
#
# TrustZone arka kapisiyla EL2'ye ciktik ve HCR_EL2.TSC ile EL1'den gelen
# her SMC'yi tuzakliyoruz. Onceki acilista Windows'un EL3'e ne sordugu
# burada duruyor.
#
#   +0x00  42 32 4C 32  = "B2L2" imzasi. YOKSA kanca hic calismadi.
#   +0x04  yakalanan SMC sayisi
#   +0x10  ilk kayit, 32 bayt: x0 (fonksiyon no), x1, x2, ELR_EL2
#
# ARADIGIMIZ: son kayitta x0 = 0x84000009 (PSCI SYSTEM_RESET). Cikarsa
# Windows cihazi kendi istegiyle sifirliyor demektir ve ELR_EL2 cagriyi
# yapan kodun adresini verir.
#
#
# B2LFB'nin olum sonrasi kaydi. Windows'un icinden yazildi ve sicak
# sifirlamayi asti.
#
#   +0x00  "B2LF"        surucu calisti
#   +0x0C  "BUGC"        bugcheck oldu (yoksa olmadi)
#   +0x10  "TIK=nnnn"    cekirdek kac saniye yasadi
#          "BUG=H/E"     H = hayir, E = evet
#
# 0x20 bayt yetiyor: geri kalani zaten FF. Kisa tutmak PMU'yu ekranda
# tutuyor.
#
dmem 0xCF030000 40
echo .

#
# CALISMA ZAMANI SERVISI KANCASI (B2LRtHookDxe, 0xCF040000)
#
#   +0x00  "B2LR"   surucu yuklendi
#   +0x04  GetTime GIRIS sayaci
#   +0x08  GetTime CIKIS sayaci
#   +0x0C  GetVariable GIRIS sayaci
#   +0x10  GetVariable CIKIS sayaci
#
# ARADIGIMIZ: giris > cikis. O zaman sifirlama tam o cagrinin ICINDE oldu.
#
echo ================ CALISMA ZAMANI KANCASI
dmem 0xCF040000 28
echo .

#
# PMU RST_STAT - ONCEKI KAPANISIN SEBEBI
#
# EN SONDA, cunku aradigimiz tek deger bu ve yukarida kalirsa kayiyor.
#
#   0x20000000  yazilim sifirlamasi - Windows PSCI ile kendi istemis
#   0x40010000  guc cevrimi - cihaz gercekten kapanmis
#   baskasi     Exynos RST_STAT'ta watchdog bitleri var; farkli bir bit
#               cikarsa cevap PMU yapilandirmasinda demektir
#
#
# DONANIM WATCHDOG'LARI - CIKARIMLA ELENMISTI, SIMDI OLCULUYOR
#
# WTCON (+0x00) bit 5 = watchdog enable, bit 0 = reset enable.
# Iki yazmac da 00 00 00 00 ise watchdog'lar kapali ve eleme dogruydu.
# bit5 set cikarsa eleme yanlisti ve suclu bulundu.
#
#
# EL3 SMART EXCEPTION HANDLER (B2LSehDxe)
#
#   0xCF041100  "SEH!" imzasi varsa (ONCEKI acilisin kopyasi; EL3 ISTISNASI YAKALANDI
#     +0x08 Id     0 = EL3 monitorunun kendisi, degilse LDFW smc id
#     +0x10 elr_el3   istisnanin olustugu adres
#     +0x18 esr_el3   ISTISNA SINIFI - aradigimiz asil deger
#     +0x20 sctlr  +0x28 ttbr  +0x30 tcr  +0x38 scr  +0x40 offset
#
#   0xCF041080  "SEHR" + SMC donusleri
#     +0x04 SET_DEBUG_MEM donusu (dogrusu 0x2000)
#     +0x08 SET_SEH_ADDRESS donusu, UEFI icinde (0 = basarili)
#     +0x0C SET_SEH_ADDRESS donusu, SetVirtualAddressMap sonrasi
#     +0x10/+0x14 isleyicinin sanal adresi
#
# esr_el3'un ust alti biti (EC) istisna sinifini veriyor:
#   0x17 -> gecersiz SMC     0x24/0x25 -> veri erisim hatasi
#   0x0E -> illegal state    0x16      -> HVC
#
echo ================ EL3 SEH
dmem 0xCF041100 48
echo .
echo ================ EL3 SEH KAYIT DURUMU
dmem 0xCF041080 80
echo .

echo ================ WATCHDOG WTCON
dmem 0x10050000 4
dmem 0x10060000 4
echo .

echo ================ PMU RST_STAT
dmem 0x15860404 4

#
# MEMMAP KALDIRILDI - GOREVINI YAPTI, SONRA ZARAR VERDI.
#
# Eklenen bolgelerin haritaya girdigini dogruladi:
#     RT_Data : 300 -> 316 Pages   (+16 sayfa = tam 64 KB = B2L RT Log)
#     Reserved: CF000000-CF03FFFF  (B2L EL2, 0x40000)
#
# Ama ciktisi uzun ve altindaki her seyi yukari kaydiriyor. Kanca sayaclari
# tam bu yuzden ekranda gorunmedi - PMU'yu kaybettigimiz hatanin aynisi.
#
# Her gereksiz satir, gormek istedigimiz satiri asagi itiyor.
#

echo ================ Fotografla, sonra tusa bas.
pause

#
# 0) EbsTest - varsa HER SEYDEN ONCE.
#
# ExitBootServices cagirip HICBIR SEY kurmayan bir kalp atisi. Boot
# servisleri kapandiktan sonra geriye yalnizca framebuffer'a kare cizen bir
# dongu kaliyor: ne zamanlayici, ne kesme, ne surucu. Her tik bir kare,
# yani kendi kendini zamanliyor - cihaz sifirlanirsa kareleri saymak
# yetiyor, kronometreye gerek yok.
#
# NEDEN: Linux ExitBootServices cagirip 120 saniye yasiyor ama her seyi
# kuruyor. Windows cagirip 15 saniyede oluyor. Aradaki ucuncu durum hic
# denenmedi. ~15 saniyede sifirlarsa sorun Windows'ta DEGIL demektir ve
# ACPI'de aramayi tamamen birakiriz.
#
# Dosya esp/ icinde yoksa bu blok atlanir ve normal acilis surer.
#
if exist fs0:\EBSTEST.EFI then
    echo EbsTest calisiyor. Kareleri say.
    fs0:\EBSTEST.EFI
    echo EbsTest geri dondu.
    pause
endif

#
# 3) Linux once: bayraksiz cache_esp.sh acikca Linux modu.
#
if exist fs0:\initramfs-test.gz then
    fs0:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs1:\initramfs-test.gz then
    fs1:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs2:\initramfs-test.gz then
    fs2:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif
if exist fs3:\initramfs-test.gz then
    fs3:\vmlinuz-lts initrd=\initramfs-test.gz earlycon=efifb console=tty0 acpi=force loglevel=8 keep_bootcon
endif

#
# 4) Windows.
#
# ------------------------------------------------------------------------
# TETIKLEYICI DEGISTI: boot.wim DEGIL, BCD
# ------------------------------------------------------------------------
# Onceki surum "sources\boot.wim varsa ac" diyordu. Sebebi vardi: iki FAT
# birimi oldugunda eski cache'te bootmgfw kalmis ama boot.wim silinmis
# olabiliyordu ve o birimi secince "device not accessible" ile geri
# donuyorduk.
#
# Ama DISKTEN ACMA'ya gecince boot.wim ESP'den bilerek cikarildi - WinPE
# artik NTFS'teki SYSTEM bolumunden aciliyor, RAM diske gerek yok. Sonuc:
# dort birim de elendi ve Shell "Acilacak bir sey bulunamadi" dedi.
#
# Dogru tetikleyici BCD: bootmgfw'in TEK zorunlu yoldasi. Hem RAM disk hem
# disk varyantinda var, ve yaninda bootmgfw olmayan bir BCD anlamsiz.
#
if exist fs0:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs0
    fs0:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W0: geri dondu.
endif
if exist fs1:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs1
    fs1:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W1: geri dondu.
endif
if exist fs2:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs2
    fs2:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W2: geri dondu.
endif
if exist fs3:\EFI\Microsoft\Boot\BCD then
    echo Windows: fs3
    fs3:\EFI\Microsoft\Boot\bootmgfw.efi
    echo ISARET W3: geri dondu.
endif

echo Acilacak bir sey bulunamadi.
pause
