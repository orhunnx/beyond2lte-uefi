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
  {"EL2 Code",           0x87000000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK},
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
