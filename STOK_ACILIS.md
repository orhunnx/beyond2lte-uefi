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
