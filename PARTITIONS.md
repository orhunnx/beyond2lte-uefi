# beyond2lte bölüm tablosu

Kaynak: `BEYOND2LTE_EUR_OPEN.pit`, stock firmware CSC tar'ından çıkarıldı.
Telefonun açılmasına gerek yok — PIT tüm bölümleri tanımlıyor.

> **Blok boyutu 4096, 512 değil.** İlk çıkarımda 512 varsaymıştım; CACHE'i
> cihazdan geri okuyunca 629.145.600 bayt geldi — PIT'teki 153600 bloğun tam
> 4096 katı. Bu UFS'in mantıksal blok boyutu (`UfsDxe`: `BlockSize : 4096`).
> Aşağıdaki bütün boyutlar buna göre düzeltildi; önceki sürümde hepsi 8 kat
> küçüktü.

| Bölüm | Blok | Boyut | Odin dosyası |
|---|---:|---:|---|
| `BOOTLOADER` | 1024 | 4.0 MB | sboot.bin |
| `ECT` | 20 | 0.1 MB | ect.bin |
| `ACPM` | 35 | 0.1 MB | acpm.bin |
| `UL_KEYS` | 0 | 0.0 MB | ul_key.bin |
| `BOOTLOADER2` | 768 | 3.0 MB | - |
| `DDI` | 256 | 1.0 MB | - |
| `CM` | 1792 | 7.0 MB | cm.bin |
| `CPEFS` | 1536 | 6.0 MB | — |
| `PIT` | 2 | 0.0 MB | - |
| `MD5HDR` | 256 | 1.0 MB | md5.img |
| `BOTA0` | 1152 | 4.5 MB | - |
| `BOTA1` | 1920 | 7.5 MB | - |
| `EFS` | 5120 | 20.0 MB | efs.img |
| `PARAM` | 2048 | 8.0 MB | param.bin |
| `UP_PARAM` | 2048 | 8.0 MB | up_param.bin |
| `KEYSTORAGE` | 128 | 0.5 MB | keystorage.bin |
| `SEC_EFS` | 5120 | 20.0 MB | sec_efs.img |
| `BOTA2` | 2048 | 8.0 MB | - |
| `STEADY` | 1024 | 4.0 MB | steady.bin |
| `UH` | 512 | 2.0 MB | uh.bin |
| `UHCFG` | 1024 | 4.0 MB | uhuh.bin |
| `DTB` | 2048 | 8.0 MB | dt.img |
| `DTBO` | 2048 | 8.0 MB | dtbo.img |
| `BOOT` | 14080 | 55.0 MB | boot.img |
| `RECOVERY` | 16512 | 64.5 MB | recovery.img |
| `RADIO` | 12800 | 50.0 MB | modem.bin |
| `NAD_REFER` | 256 | 1.0 MB | nad_refer.bin |
| `PERSISTENT` | 128 | 0.5 MB | — |
| `MISC` | 256 | 1.0 MB | — |
| `KEYREFUGE` | 4096 | 16.0 MB | — |
| `KEYDATA` | 4096 | 16.0 MB | — |
| `DQMDBG` | 4096 | 16.0 MB | dqmdbg.img |
| `VBMETA` | 16 | 0.1 MB | vbmeta.img |
| `BOTA3` | 368 | 1.4 MB | — |
| `SYSTEM` | 1459200 | 5.57 GB | system.img |
| `VENDOR` | 281600 | 1.07 GB | vendor.img |
| `PRODUCT` | 158720 | 620.0 MB | product.img |
| `CACHE` | 153600 | 600.0 MB | cache.img |
| `OMR` | 12800 | 50.0 MB | omr.img |
| `CP_DEBUG` | 1280 | 5.0 MB | modem_debug.bin |
| `USERDATA` | 0 | 0.0 MB | userdata.img |
