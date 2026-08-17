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
       Windows/System32/DriverStore/FileRepository/b2lufs.inf_*/
       offline SYSTEM hive: boot-start b2lufs
  -> StorPort -> B2LUFS -> Exynos 9820 UFS
```

UEFI'nin GPT'yi Windows'a ayrıca bildirmesi gerekmez. UEFI, CACHE ESP'den
boot manager'ı yükler; Windows tarafında GPT ve blok I/O'yu boot-start StorPort
miniportu sağlar.

Telefonun kalıcı GPT tablosundaki `SYSTEM` ve `CACHE` tür GUID'leri Samsung'un
Android türleridir. Paket bunları medyada değiştirmez. B2LUFS'un CRC-tutarlı,
salt Windows GPT görünümü her iki girdiyi Microsoft Basic Data olarak
gösterir: `SYSTEM` içeriği NTFS, `CACHE` içeriği FAT16'dır. Aksi halde winload
NTFS'ten çekirdeği okuyabilse bile kernel `SYSTEM` volume'unu tanımayıp
`INACCESSIBLE_BOOT_DEVICE` ile durabilir.

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

BCD üretiminde Windows batch dosyası `\\wsl.localhost` veya WSL içinden UAC
zinciriyle çalıştırılmaz. Araç, gerçek Windows Masaüstü konumunda
`B2L-UFS-BCD` klasörünü hazırlar ve pahalı WIM işlemlerinden önce bekler.
Yönetici Komut İstemi'nde ekranda gösterilen klasöre geçip
`B2L_BCD_DISK_OLUSTUR.cmd` çalıştırılır. Batch'in başarılı işareti, `disk`
varyantı, dosya boyutu ve BCD'nin `regf` başlığı doğrulandıktan sonra akış
devam eder. Böylece `bcdedit` hatasının tamamı açık CMD penceresinde kalır.
PowerShell'in OEM konsol kodlaması `Masaüstü` gibi yolları bozmasın diye bilinen
Masaüstü yolu UTF-8 Base64 olarak WSL'ye taşınır ve orada geri açılır.
Test-imzalı B2LUFS için `testsigning=Yes` zorunludur. İmzasız sürücü
kullanılmadığından `nointegritychecks` yazılmaz; bu ayar Secure Boot açık
derleme bilgisayarlarında BCDEdit tarafından reddedilebilir.

Sürücü paketi üretildikten sonra araç ikinci kez bekler. Bu kez ekranda
gösterilen `%LocalAppData%\B2L-UFS-DISM` klasörüne Yönetici Komut İstemi'nde
geçip `B2L_UFS_WIM_SURUCU_EKLE.cmd` çalıştırılır. Yardımcı betik `boot.wim`i
Windows DISM ile bağlar ve test-imzalı `b2lufs.inf` paketini `/Add-Driver
/ForceUnsigned` ile çevrimdışı imajın DriverStore ve DriverDatabase yapısına
kurar. Başarı işareti yalnız DISM commit'i tamamlandıktan sonra yazılır; WSL
tarafı istek kimliğini, WIM boyutunu, WIM bütünlüğünü ve DriverStore'daki
INF/SYS/CAT dosyalarını yeniden doğrular. Microsoft'un çevrimdışı sürücü
kurulum modeli de paketin yalnız `System32\drivers` altına kopyalanmasını değil,
[DISM ile DriverStore'a eklenmesini](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/add-and-remove-drivers-to-an-offline-windows-image?view=windows-11)
öngörür.

1. ISO'dan WinPE/boot dosyalarını çıkarır.
2. Mevcut SYS'i WSL'de yeniden derlemeden imzalar ve kataloglar.
3. INF/SYS/CAT paketini yönetici DISM ile offline DriverStore'a kurar.
4. SYS'i offline SYSTEM hive'ında boot-critical yapıp iki katmanı doğrular.
5. Masaüstünde yönetici CMD ile üretilmiş disk BCD'sini doğrular.
6. NTFS `system.img` ve FAT16 `cache.img` üretir.
7. Yalnız bu iki imajı `out/ufs-diskboot.tar` içine koyar.

Üretim sırasında `system.img` için NTFS BPB imzası, 4096 bayt sektör ve
4096 bayt küme geri okunarak doğrulanır. `cache.img` ise FAT16 olarak
oluşturulur. Araç, SYS yoksa veya kaynaklardan eskiyse pahalı ISO/BCD/DISM
adımlarından önce durur ve gerekli PowerShell komutunu gösterir. WSL tarafında
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
  sırasında B2LUFS depolama zinciri kurulmamıştır. Eski pakette INF yalnız
  `Windows\B2LUFS` altında bulunuyor, resmi offline DriverStore kurulumu
  yapılmıyordu; yeni DISM kapısı bu bilinen eksikliği giderir. Aynı kod yeni
  paketle tekrarlanırsa artık sürücünün çok daha erken yükleme/PnP davranışı
  ayrı bir tanı girdisidir.
- Boot manager veya winload hatası, ekrandaki kodla BCD/dosya katmanına ayrılır.

Geri dönüşte Download Mode üzerinden stock SYSTEM ve CACHE imajlarını ya da tam
stock AP paketini flaşlayın. Bölüm tablosunu değiştirmediğimiz için PIT ile
yeniden bölümleme normal geri dönüşün parçası değildir.

## Alt araçlar

| Araç | Görevi |
|---|---|
| `tools_ufsboot.sh` | Tüm WSL paketleme akışını doğru sırayla çalıştırır. |
| `windows/B2L_UFS_WIM_SURUCU_EKLE.cmd` | Yönetici DISM ile INF/SYS/CAT paketini offline DriverStore'a kurar. |
| `tools_ufsboot_prepare.sh` | WIM'e SYS ve offline boot-critical kayıtları ekler/doğrular. |
| `windows/b2lufs/b2lufs-boot.reg` | Hive'da bulunan control set'ler için servis ve CriticalDeviceDatabase şablonu. |
| `windows/bcd_olustur.cmd` | Diskteki `winload.efi` için BCD üretir. |
| `windows/B2L_BCD_DISK_OLUSTUR.cmd` | Masaüstündeki yönetici CMD akışını ve başarı işaretini yönetir. |
| `tools_windisk.sh` | WIM'i NTFS SYSTEM imajına uygular. |
| `tools_winboot.sh --disk` | CACHE ESP için EFI dosyalarını ve disk BCD'sini sahneler. |
| `cache_esp.sh --win --disk --baseline` | 600 MiB CACHE ESP imajını ve tar paketini üretir. |

Sürücü kaynaklarının bağımsız ikinci kopyası `drivers/windows/storage/b2lufs`
altındadır. Canlı kaynak değişikliğinden sonra yedekler
`bash drivers/sync-driver-backups.sh update` ile eşitlenir.
