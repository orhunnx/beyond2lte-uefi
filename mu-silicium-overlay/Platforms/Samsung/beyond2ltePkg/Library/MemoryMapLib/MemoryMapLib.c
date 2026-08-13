#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  //
  // NOT: beyond1lte'de "DXE Heap" (0xC9240000 + 60 MB = 0xCCE40000) "Display
  // Reserved" bolgesini (0xCA000000 - 0xCB400000) tamamen iceriyordu. DXE heap
  // framebuffer'in uzerine tahsis yapar. Heap'i Display'in ardina tasidik.
  // Sonraki DTB rezervi modem_spare 0xD0000000 -> 16 MB bosluk kaliyor.
  //
  //
  // Samsung U-H / EL2 - kalici S-Boot hipervizoru.
  //
  // Stok G975FXXSGHWC1 BL, imzali UH bolumunu burada yukleyip
  // x0=0xC2000400, x1=0x87001000, x2=0x2000, x3=1,
  // x4=0x87000000, x5=0x100000 ile EL2'ye giriyor. UH ELF'i 0x87000000'a
  // bagli; 0x8701b800'e VBAR_EL2 kuruyor, veri alanini ve 0x87046000'e
  // kadar olan EL2 yigini kullaniyor. BOOT bolumune UEFI yazmak UH
  // bolumunu degistirmez, dolayisiyla bu kod UEFI/Windows devrinde yasiyor.
  //
  // BsCode yanlisti: ExitBootServices sonrasinda Windows bu sayfalari
  // normal tahsis havuzuna geri katabilir. U-H'nin kodunu/verisini ezmek
  // sessiz EL2 reseti icin sinanabilir bir adaydi. Koruma uygulaninca RAM
  // 0x1F29'dan 0x1F27'ye indi ama Windows reseti surdu; yani bu ezilme ana
  // neden degil. Buna ragmen platformun calisan EL2 sayfalarini Windows'a
  // vermek yanlis oldugundan rezerv korunur. Stok yukleyici x5'te 1 MiB
  // ister; mevcut 2 MiB rezervi eksiksiz koruyoruz.
  //
  {"Samsung U-H EL2",    0x87000000, 0x00200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"UEFI FD",            0xC9000000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI Stack",         0xC9200000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"Display Reserved",   0xCA000000, 0x01400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"DXE Heap",           0xCB400000, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

  //
  // B2L EL2 - EL2 yuku, yigin ve SMC kaydi. 256 KB.
  //
  // NEREDE: DXE Heap 0xCF000000'da bitiyor, DTB'nin bir sonraki rezervi
  // modem_spare 0xD0000000'da basliyor. Aradaki 16 MB'lik boslugun basi;
  // ne UEFI ne Android burayi kullaniyor.
  //
  // Reserv SART, RtData DEGIL: EL2'de MMU kapali, adresler fiziksel. Windows'un
  // bu bolgeyi haritalamasina gerek yok - sadece DOKUNMAMASI gerekiyor.
  //
  // UNCACHED_UNBUFFERED SART: EL2 kodu onbellege ugramadan calisiyor ve
  // kaydi sifirlamadan hemen once yaziyor; veri onbellekte kalirsa kaybolur.
  //
  // Ic yerlesim (B2LEl2Payload.S):
  //   0xCF000000  istisna vektoru (VBAR_EL2, 2 KB hizali)
  //   0xCF000800  init rutini
  //   0xCF010000  yigin tepesi; cekirdek N -> +4096*N
  //   0xCF020000  bu acilisin SMC kaydi
  //   0xCF030000  onceki acilisin kaydi  <- Shell burayi okur
  //
  {"B2L EL2",            0xCF000000, 0x00040000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED},

  //
  // B2L RT LOG - CALISMA ZAMANI KANCASININ KAYDI
  //
  // "Reserv" DEGIL "RtData" OLMAK ZORUNDA, VE BU BIR BUGCHECK'E MAL OLDU.
  //
  // Ilk surumde kayit 0xCF040000'daydi ama bolge Reserv (EfiReservedMemoryType)
  // isaretliydi. SetVirtualAddressMap'ten sonra Reserv bolgeler SANAL HARITAYA
  // HIC GIRMIYOR; dolayisiyla EfiConvertPointer isaretciyi ceviremedi ve kanca
  // ilk cagrida fiziksel adrese yazmaya calisti. Sonuc:
  //
  //     SYSTEM_THREAD_EXCEPTION_NOT_HANDLED (0x7E)
  //
  // RtData (EfiRuntimeServicesData) bolgeler sanal haritaya giriyor ve
  // EfiConvertPointer onlari cevirebiliyor. EFI Shell tarafindan FIZIKSEL
  // adresten okumak yine calisiyor - bellegin kendisi ayni yerde.
  //
  // WRITE_BACK yeterli: bu bolgeye EL2 kodu degil, normal cekirdek modu
  // kodu yaziyor ve sicak sifirlamada icerik zaten kaliyor.
  //
  {"B2L RT Log",         0xCF040000, 0x00010000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK},

  //
  // ==========================================================================
  // STOCK DTB'DEKI reserved-memory BOLGELERI
  // ==========================================================================
  //
  // NEDEN EKLENDI - AYLARDIR ARADIGIMIZ IMZAYA BIREBIR UYUYOR
  //
  // Windows cekirdegi ~10 saniye SAGLIKLI calisiyor (kalp atisi son ana kadar
  // duzenli), sonra TEMIZ bir yazilim sifirlamasi geliyor. Bugcheck YOK
  // (dogrulanmis geri cagri), kullanici moduna hic cikilmiyor, ve sure
  // depolama hizindan, menu beklemesinden, startnet icerignden, watchdog'dan,
  // LPS0'dan ve ErrorControl'den BAGIMSIZ.
  //
  // Cekirdek init'inde bir sey ters giderse Windows BUGCHECK verir. Sessiz,
  // temiz bir sifirlama icin isletim sisteminin DISINDA birinin karar vermesi
  // gerekiyor.
  //
  // Samsung SoC'lerinde tam olarak boyle bir mekanizma var: normal dunyadan
  // KORUMALI bir bellek bolgesine erisim, guvenli monitorun cihazi
  // sifirlamasiyla sonuclaniyor. Isletim sistemine haber verilmiyor.
  //
  // Zamanlama da uyuyor: Windows'un tahsis sirasi deterministik, yani o
  // bolgeye ne zaman dokunacagi her acilista ayni - hep 9-10 saniye.
  //
  // Ve Linux'un neden calistigini acikliyor: DTB'deki reserved-memory
  // dugumlerini okuyup bu bolgelerden uzak duruyor. Bizim haritamizda ise
  // hicbiri yoktu, dolayisiyla Windows hepsini bos RAM saniyordu.
  //
  // BAS SUPHELI: "exynos,seclog" 0xC0000000. Adindan belli - GUVENLI GUNLUK,
  // TrustZone'un. Ustelik dusuk bir adres, yani Windows erken tahsis ediyor.
  //
  // Kaynak: stock DTB, /reserved-memory dugumu (dtb-analiz/exynos9820-base.dts).
  // Sabit "reg" adresi olan butun girdiler alindi; "size" ile tanimlanan
  // dinamik olanlar (ion, abox_rmem) atlandi cunku adresleri calisma aninda
  // belirleniyor ve UEFI'de karsiligi yok.
  //
  //
  // 3 GB USTU KESME KALDIRILDI - DELIGE DUSMUSTU.
  //
  // 0x140000000'a Reserved konmustu ve cihazda hicbir etkisi olmadi: harita
  // yine 7977 MB gosterdi. Sebebi stok DTB'de: RAM tek parca degil.
  //
  //   memory@80000000    0x80000000  .. 0x100000000
  //   memory@880000000   0x880000000 .. 0x900000000
  //   memory@900000000   0x900000000 .. 0x980000000
  //
  // 0x100000000 - 0x880000000 arasi DELIK. Kesme tam oraya dusmustu, yani
  // zaten RAM olmayan bir yeri "RAM degil" diye isaretledik.
  //
  // Kesmeyi dogru yere koymadan once UEFI'nin bu bankalari nasil beyan
  // ettigini olcmek gerekiyor: dogru mu kurmus, yoksa 0x80000000'dan duz
  // devam edip deligi de RAM sanmis mi? Toplam ikisinde de ~8 GB cikar,
  // ayirt eden sey EN YUKSEK ADRES. B2LGoster onu gosteriyor.
  //

  {"sec_debug_magic",    0x80000000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"mm_debug",           0x91200000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"seclog",             0xC0000000, 0x00080000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},

  // debug-snapshot ailesi - cekirdek/platform cokme gunlukleri
  {"dss_log_pstore",     0xEB308000, 0x00008000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_s2d",        0xEB310000, 0x01500000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_arraydump",  0xEC810000, 0x00A00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_header",         0xFD900000, 0x00010000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_kernel",     0xFD910000, 0x00200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_platform",   0xFDB10000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_sfr",        0xFDF10000, 0x00100000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_cachedump",  0xFE010000, 0x00A00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_etm",        0xFEA10000, 0x00100000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_bcm",        0xFEB10000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_llc",        0xFEF10000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_dbgc",       0xFF310000, 0x000F0000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"dss_log_kevents",    0xFF400000, 0x00C00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},

  // iRAM Regions
  {"IRAM",               0x02020000, 0x00018000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"NS_IRAM",            0x02038000, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  // Register Regions
  {"Chip Info",          0x10000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  //
  // DONANIM WATCHDOG'LARI - SADECE OKUMAK ICIN HARITALANIYOR
  //
  // Stock DTB'de iki tane var ve ikisini de hic incelemedik:
  //     watchdog_cl0@10050000  samsung,exynos9-v1-wdt  timeout-sec = 30
  //     watchdog_cl2@10060000  samsung,exynos9-v2-wdt  timeout-sec = 30
  //
  // Watchdog hipotezini daha once CIKARIMLA elemistik: EbsTest 60 saniye
  // yasadi, ve EDK2'nin YAZILIM watchdog'unu etkisizlestirmek bir sey
  // degistirmedi. Ama bunlar DONANIM watchdog'lari, kume basina birer tane,
  // ve haritada olmadiklari icin ne UEFI ne Windows onlara dokunuyor.
  //
  // Cikarim muhtemelen dogru - acik olsalar UEFI de sifirlanirdi. Ama bu
  // proje sunu ogretti: CIKARIMLA ELEME, OLCUMLE ELEME DEGILDIR. Ve okumak
  // tek flas.
  //
  // Exynos WDT yazmaclari:
  //     +0x00 WTCON   bit5 = watchdog enable, bit0 = reset enable
  //     +0x04 WTDAT   yeniden yukleme degeri
  //     +0x08 WTCNT   anlik sayac
  //
  // WTCON'da bit5 set ise watchdog CALISIYOR demektir ve eleme yanlisti.
  //
  {"WDT CL0",            0x10050000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"WDT CL2",            0x10060000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICD",               0x10101000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICC",               0x10102000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PMU",                0x15860000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Decon",              0x19030000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Clock Controller",   0x1A240000, 0x00008000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  // UFS bloklari. beyond1lte'de yoktu; eksikligi 0x13D6xxxx erisiminde
  // "Data abort: Translation fault" veriyor.
  //
  // GPIO controller'lari BURAYA EKLENMEZ: GpioDxe onlari kendi
  // MapMemoryRegion() ile map ediyor. Onceden eklersek cakisir ve
  // "Failed to Map GPIO Controller / start failed: Access Denied" alirsin.
  {"UFS UniPro",         0x13D50000, 0x00008000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS HCI",            0x13D60000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS IO Coherency",   0x13C10000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
