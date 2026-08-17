# UFS üzerinden Windows disk önyükleme

Bu akış, cihaz üzerinde doğrulanmış `b2lufs` okuma/yazma desteğini kullanarak
WinPE'yi RAM disk yerine dahili UFS'teki SYSTEM bölümünden başlatır. Araç zinciri
hazırdır; gerçek cihazda diskten açılış henüz doğrulanmamıştır.

## Yerleşim

```text
Samsung sboot
  -> Mu-Silicium UEFI + UfsDxe
  -> CACHE (600 MiB FAT16 ESP)
       EFI/BOOT/BOOTAA64.EFI
       EFI/Microsoft/Boot/bootmgfw.efi
       EFI/Microsoft/Boot/BCD
  -> SYSTEM (NTFS, varsayılan 4 GiB)
       Windows/System32/Boot/winload.efi
       Windows/System32/drivers/b2lufs.sys
       offline SYSTEM hive: boot-start b2lufs
  -> StorPort -> B2LUFS -> Exynos 9820 UFS
```

UEFI'nin GPT'yi Windows'a ayrıca bildirmesi gerekmez. UEFI, CACHE ESP'den
boot manager'ı yükler; Windows tarafında GPT ve blok I/O'yu boot-start StorPort
miniportu sağlar.

## Güvenlik sınırı

Üretilen Odin paketi yalnız şu iki dosyayı içerir:

- `system.img`: WinPE'nin açıldığı NTFS bölüm.
- `cache.img`: EFI yükleyici ve disk BCD'sini taşıyan FAT16 ESP.

Paket PIT, sboot/boot, recovery veya başka bir stock bölümü içermez. Buna rağmen
flaş işlemi cihazdaki mevcut **SYSTEM ve CACHE içeriklerini tamamen değiştirir**.
Devam etmeden önce bu iki bölümün veya tam stock firmware'in yedeği bulunmalıdır.
Odin'de **Re-Partition işaretlenmemelidir**.

## 1. PowerShell — yalnız sürücü derleme

`b2lufs.sys` yalnız Windows PowerShell'de MSVC ARM64 ve WDK ile derlenir:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

Başarılı sonuç `out/b2lufs/b2lufs.sys` olur. Bundan sonraki komutlar
PowerShell'de değil WSL'de çalıştırılır.

## 2. WSL — tek komutla disk-boot paketi

Gerekli araçlar:

```bash
sudo apt install -y wimtools libhivex-bin libwin-hivex-perl ntfs-3g mtools android-sdk-libsparse-utils
```

Windows ARM64 ISO yolunu vererek paketi üretin:

```bash
cd ~/beyond2lte-uefi
bash tools_ufsboot.sh /mnt/c/Users/orhun/Downloads/Windows_ARM64.iso 4
```

Son argüman SYSTEM imajının GiB cinsinden boyutudur; `2..5` aralığında olmalı,
verilmezse `4` kullanılır. Orkestrasyon sırasıyla:

Araç, kullanılan ISO'nun kanonik yolu, boyutu ve değişiklik zamanını kaydeder.
ISO değiştiğinde hazır `boot.wim` otomatik yenilenir; önceki ISO'nun WIM
önbelleği yeni pakete karışmaz.

BCD üretiminde Windows batch dosyası `\\wsl.localhost` üzerinden doğrudan
çalıştırılmaz. Araç betiği ve taze BCD çıktısını Windows `%TEMP%` altında
sahneler; yalnız `regf` doğrulamasını geçen çıktı repoya geri alınır.

1. ISO'dan WinPE/boot dosyalarını çıkarır.
2. Mevcut SYS'i WSL'de yeniden derlemeden imzalar ve kataloglar.
3. SYS'i offline SYSTEM hive'ına boot-critical olarak kurup doğrular.
4. Disk BCD'sini Windows `bcdedit.exe` ile otomatik oluşturur.
5. NTFS `system.img` ve FAT16 `cache.img` üretir.
6. Yalnız bu iki imajı `out/ufs-diskboot.tar` içine koyar.

Araç, SYS yoksa durur ve gerekli PowerShell komutunu gösterir. WSL tarafında
MSVC derlemesi yapılmaz.

## 3. Paketi doğrulama ve flaşlama

Flaşlamadan önce içeriği ve karmayı kaydedin:

```bash
cd ~/beyond2lte-uefi
tar -tf out/ufs-diskboot.tar
sha256sum out/ufs-diskboot.tar
```

Beklenen içerik yalnız şudur:

```text
system.img
cache.img
```

Windows'ta Odin ile:

1. `out/ufs-diskboot.tar` dosyasını AP alanına seçin.
2. **Re-Partition kapalı** olsun.
3. Yalnız SYSTEM ve CACHE hedeflerinin yazılacağını Odin günlüğünden kontrol
   edin, sonra flaşlayın.

## Beklenen sonuç ve geri dönüş

- Windows açılırsa boot-critical UFS zinciri çalışmıştır.
- `INACCESSIBLE_BOOT_DEVICE` görülürse EFI/BCD aşaması geçilmiş, fakat boot
  sırasında B2LUFS depolama zinciri kurulmamıştır; hata kodu yeni tanı girdisidir.
- Boot manager veya winload hatası, ekrandaki kodla BCD/dosya katmanına ayrılır.

Geri dönüşte Download Mode üzerinden stock SYSTEM ve CACHE imajlarını ya da tam
stock AP paketini flaşlayın. Bölüm tablosunu değiştirmediğimiz için PIT ile
yeniden bölümleme normal geri dönüşün parçası değildir.

## Alt araçlar

| Araç | Görevi |
|---|---|
| `tools_ufsboot.sh` | Tüm WSL paketleme akışını doğru sırayla çalıştırır. |
| `tools_ufsboot_prepare.sh` | WIM'e SYS ve offline boot-critical kayıtları ekler/doğrular. |
| `windows/b2lufs/b2lufs-boot.reg` | Hive'da bulunan control set'ler için servis ve CriticalDeviceDatabase şablonu. |
| `windows/bcd_olustur.cmd` | Diskteki `winload.efi` için BCD üretir. |
| `tools_windisk.sh` | WIM'i NTFS SYSTEM imajına uygular. |
| `tools_winboot.sh --disk` | CACHE ESP için EFI dosyalarını ve disk BCD'sini sahneler. |
| `cache_esp.sh --win --disk --baseline` | 600 MiB CACHE ESP imajını ve tar paketini üretir. |

Sürücü kaynaklarının bağımsız ikinci kopyası `drivers/windows/storage/b2lufs`
altındadır. Canlı kaynak değişikliğinden sonra yedekler
`bash drivers/sync-driver-backups.sh update` ile eşitlenir.
