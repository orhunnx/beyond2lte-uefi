# Stok çekirdeğin açılış sırası — ve Windows'ta olmayanlar

## Nasıl çıkarıldı (tekrarlanabilir)

```
tar -xf stockfirmware/AP_*.tar.md5 boot.img.lz4
python3 -c "import lz4.frame,sys; open('boot.img','wb').write(lz4.frame.decompress(open('boot.img.lz4','rb').read()))"
# ANDROID! basligindan kernel'i kes (offset = page_size)
pip install vmlinux-to-elf
vmlinux-to-elf kernel.bin vmlinux.elf     # kallsyms'ten sembol tablosu kuruyor
```

Çekirdek **sıkıştırılmamış** ARM64 Image, 45 MB, `kallsyms` gömülü. `vmlinux.elf`
içinde 113.175 sembol var. `__initcall_*` sembolleri bağlantı sırasında durduğu
için **açılış sırasının kendisi** okunabiliyor: 1162 initcall.

- Tam liste: `dtb-analiz/initcall_sirasi.txt`
- Yalnızca Exynos/Samsung olanlar: `dtb-analiz/initcall_exynos.txt`

## Seviyeler

| Seviye | Adet |
|---|---|
| early | 27 |
| core (1) | 50 |
| postcore (2) | 25 |
| arch (3) | 29 |
| subsys (4) | 117 |
| fs (5) | 54 |
| device (6) | 738 |
| late (7) | 109 |

## Windows'ta karşılığı olmayan, açılışa dokunan Exynos initcall'ları

```
early  exynos_cpupm_early_init      dbg_snapshot_init
1      exynos_protect_kernel_text   exynos_chipid_init
3      exynos_set_seh_address       exynos_acpm_ipc_init
3      exynos_powermode_init        exynos_cpupm_init
3s     exynos_acpm_init             exynos_adv_tracer_init
4      exynos_pmu_init              exynos_reboot_init
4      exynos_handler_init          s2mps19_pmic_init
5      exynos_acpm_dvfs_init        exynos_acpm_tmu_init
5s     exynos_acpm_binary_update
6      exynos_itmon_driver_init     exynos_ppmpu_init
6      exynos_seclog_init           init_tzdev
```

## EbsTest bu listenin çoğunu eliyor

`ExitBootServices` çağırıp **hiçbir şey kurmayan** bir döngü 30+ saniye yaşıyor.
Dolayısıyla periyodik servis isteyen ne varsa — watchdog, ACPM canlılık kontrolü,
EL3 kilitlenme dedektörü, termal denetim — sıfırlamanın sebebi olamaz. Olsaydı
`EbsTest`'i de öldürürdü.

Geriye yalnızca **AP bir şey yapınca ısıran** mekanizmalar kalıyor:

| Aday | Neden uyuyor |
|---|---|
| `exynos_ppmpu_init` | Korumalı DRAM bölgeleri. Windows ayırıcısı ilerledikçe birine dokunur. |
| `exynos_itmon_driver_init` | Yasadışı veri yolu erişimi. Saatsiz/güçsüz bir IP bloğuna dokunmak yeter. |

İkisi de gözlemlerin tamamına oturuyor: iş temelli ölüm, bugcheck yok,
`ResetSystem` çağrılmıyor, EL3 istisnası yok, temiz yazılım sıfırlaması,
`EbsTest` hayatta.

## Sıradaki ölçüm

Banka testi belleğin **dört adresine** baktı. Beyan edilen belleğin tamamı hiç
yoklanmadı. UEFI içinden bütün `EfiConventionalMemory` sayfalarını gezip
yazma/okuma yapmak, korumalı bir bölge varsa onu **Windows'tan önce ve tam
adresiyle** bulur.

Tarama kendi ilerlemesini `0xCF041200`'e yazıyor; cihaz tarama sırasında
sıfırlanırsa bir sonraki açılışta tarama atlanıp son adres gösteriliyor.
Yoksa aynı yerde sonsuza kadar sıfırlanırdık.

---

# KONTROL DENEYI: LINUX AYNI FIRMWARE VE ACPI ILE

Tarih itibariyle projenin en belirleyici olcumu.

`cache_esp.sh --linux --system` ile Alpine 6.18 (aarch64) **bizim UEFI'mizden**,
**bizim ACPI tablolarimizla** (`acpi=force`) acildi:

```
ACPI: RSDP/XSDT/FACP/DSDT/APIC/SSDT/GTDT/PPTT/BGRT  hepsi okundu
CPU: All CPU(s) started at EL1
SMP: Total of 1 processors activated
Memory: 7847276K/8261888K available
kvm [1]: HYP mode not available
Run /init as init process
```

Ardindan `linux_test/init` CPU'yu surekli yakti:

```
B2L: YUK N=1  uptime=14.91
...
B2L: YUK N=84 uptime=169.83
```

**170 saniye kesintisiz CPU yuku, sifirlama yok.**

Windows ayni donanimda kendi CPU'sunda ~10 saniye calisip sifirlaniyor
(9.78 / 10.04 / ~12 saniye, uc bagimsiz olcum).

## Sonuc

Platform aklandi. Sunlarin hicbiri sifirlamanin sebebi degil:

- firmware ve UEFI kurulumu
- ACPI tablolari (FACP, DSDT, APIC, GTDT, PPTT)
- bellek haritasi ve DRAM bankalari
- S-Boot'un biraktigi donanim durumu (calisan DECON/DSIM/DPP dahil)
- GIC ve bekleyen sekiz SPI
- EL3 / TrustZone

Gercek bir isletim sistemi ayni tablolarla, ayni yuk altinda, on yedi kat
uzun sure calisiyor. Ariza Windows tarafindaki entegrasyonda.

## Yan bulgu: PSCI

```
psci: PSCIv65535.65535 detected in firmware
psci: Using standard PSCI v0.2 function IDs
```

`PSCI_VERSION` cagrisi `0xFFFFFFFF` donuyor - EL3 bu komutu TANIMIYOR. Linux
tek cekirdekte PSCI'ye ihtiyac duymadigi icin devam ediyor. Windows ise
FACP'de PSCI biti kapatilinca hic acilmiyordu (winload cekirdegi
baslatmadan duruyor).

Yani Windows PSCI'ye guveniyor, EL3 ise en temel PSCI cagrisina bile cevap
vermiyor. Bu celiski henuz aciklanmadi ve siradaki adaylardan biri.

---

# SUCLU BULUNDU: storufs.sys

## Olcum

| Durum | Sonuc |
|---|---|
| Butun adaylar acik | ~10 saniyede SESSIZ SIFIRLAMA, BUG=H |
| Depolama kumesi kapali (11 surucu) | `0xc000021a`, kullanici moduna varildi |
| **YALNIZCA storufs kapali** | **`0xc000021a`, TIK cok yuksek, cubuklar coK ilerledi** |

Tek degisken, iki farkli sonuc.

## Iki hata kodunun anlami ayri

- `0xc000021a` (SYSTEM_PROCESS_TERMINATED) - `smss.exe` calismis, yani
  **olum noktasi gecilmis**. Sessiz sifirlama cekirdek baslatmasinin
  sonunda oluyordu; kullanici modu ondan sonrasi.
- `NDIS.SYS 0xD1` - surucu yuklenirken, yani noktadan ONCE. TIK cok kucuk
  cikiyor. Bunu "suclu bulundu" diye okumak butun aramayi cope atardi.

## Neden her seye uyuyor

| Gozlem | Aciklama |
|---|---|
| Windows'a ozgu | Linux kendi `exynos-ufs` surucusunu kullaniyor |
| Ise orantili, ~10 sn CPU | `storufs`'un denetleyiciyi tanima/kurma ani |
| Bugcheck yok | Coken Windows degil, veri yolu / denetleyici |
| `EbsTest` 30+ sn yasiyor | UFS'e hic dokunmuyor |
| Butun surucular kapaliyken kayboluyor | `storufs` o kumedeydi |
| Linux 170 sn CPU yakiyor | Kendi surucusu dogru kuruyor |

## Onceki yanlis kayit

`storufs` daha once "yapisal olarak elendi" diye kaydedilmisti. O sonuc,
sonradan karistigi anlasilan disk acilisi olcumune dayaniyordu ve bir daha
acilmamisti. Kapanmis sayilan bir dosyayi yeniden acmak bu turda karsiligini
verdi.

## Sonraki adim

WinPE RAM diskten calistigi icin `storufs` olmadan da aciliyor. Kalan
`0xc000021a`, kapattigimiz servislerin eksikliginden kaynaklanan NORMAL bir
WinPE sorunu - Windows'un belgelenmis alani, aylardir ugrastigimiz kapali
kutu degil.
