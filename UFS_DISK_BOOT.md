# UFS üzerinden Windows disk önyükleme

Bu akış, cihaz üzerinde doğrulanmış `b2lufs` okuma/yazma desteğini kullanarak
WinPE'yi RAM disk yerine dahili UFS'teki SYSTEM bölümünden başlatır. Araç zinciri
hazırdır; gerçek cihazda diskten açılış henüz doğrulanmamıştır.

Son cihaz denemesinde PIT-uyumlu NTFS/FAT BPB'leri ve düzeltilmiş BCD birlikte
olmasına rağmen sonuç yine `INACCESSIBLE_BOOT_DEVICE` oldu. Dolayısıyla
DriverStore, GPT türü, gecikmeli ilk keşif, BPB geometrisi ve BCD `osdevice`
düzeltmeleri korunur; fakat hiçbiri tek kök neden değildir. `device`,
`winload.efi` uygulamasını; `osdevice` ise `\Windows` sistem kökünü arar ve bu
sözleşme üretilen BCD yeniden açılarak doğrulanır. Microsoft'un
[UEFI BCD ayarları](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/bcd-system-store-settings-for-uefi?view=windows-11)
belgesi `device` ile `osdevice` görevlerini ayrı tanımlar; güncel
[0x7B giderme kılavuzu](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/stop-error-7b-or-inaccessible-boot-device-troubleshooting)
da ilk kontroller arasında bu iki öğenin doğru bölümü göstermesini ister.

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

B2LUFS boot-start miniportu ayrıca kapasiteyi ve bu Windows GPT görünümünü
`HwStorInitialize` dönmeden eşzamanlı olarak hazırlar. RAM-WinPE'de çalışan
eski yol aynı işi PASSIVE callback ve 500 ms gecikmeli bus taramasına
bırakıyordu; bu sırada ilk keşif istekleri `NO_DEVICE` dönüyordu. Normal
çalışma ortamı diski sonradan bulabilir, fakat kernel aynı diskten açılırken
kök volume'u bekleyemez. `0.0.7.0` ile PASSIVE/timer yolu yalnız tanıdır;
ancak bu sürümün cihazda aynı 0x7B'yi vermesi, gecikmeli keşfin tek kök neden
olmadığını kanıtladı.

`0.0.9.0`, tahmini başka bir boot davranışı değiştirmek yerine erken StorPort
yaşam döngüsünü 64 KiB'lik ikili uçuş kaydına yazar. Kayıt yalnız UEFI
uygulaması alanı açıkça `ARMED` yaptığında etkinleşir; normal paketlerde eşleme
hemen bırakılır ve kayıt yolu pasif kalır. Debug durumda aynı sürücü önceden
framebuffer'ı eşler ve bugcheck callback'i kaydeder; son kayıtları 0x7B anında
ekrana çizdiği için cihaz resetine ihtiyaç duymaz.

Disk imajlarının dosya sistemi önyükleme geometrisi de kalıcı PIT ile aynı
olmalıdır. Stok PIT'e göre SYSTEM başlangıcı 83968, CACHE başlangıcı 1983488
ve UFS mantıksal sektör boyutu 4096 bayttır. Önceki üretici her iki volume'u
ham dosyada LBA 0'dan biçimlendirip gerçek bölüm başlangıcını BPB'ye yazmıyordu;
NTFS için `Heads` ve `SectorsPerTrack` da sıfır kalıyordu. NTFS-3G'nin kendi
[`mkntfs` belgesi](https://manpages.debian.org/trixie/ntfs-3g/mkntfs.8.en.html)
`--partition-start`, `--heads` ve `--sectors-per-track` değerlerinin Windows
boot için doğru olmasını zorunlu tutar. Güncel akış bu değerleri stok PIT'ten
okur, NTFS ve FAT BPB'lerine yazar ve imaj içeriği tamamlandıktan sonra tekrar
okuyarak doğrular. [`mkfs.fat` belgesindeki](https://manpages.debian.org/trixie/dosfstools/mkfs.fat.8.en.html)
`-h` alanı da bölümün disk başlangıcına göre sektör ofsetidir; `-g` iki CHS
değerini FAT boot sektörüne yazar. CACHE UEFI yolunda DOS VBR kodu
çalıştırılmaz: önyüklenebilirlik FAT volume içindeki `BOOTAA64.EFI`,
`bootmgfw.efi`, BCD ve firmware'in bölüm Block I/O yolundan gelir. BPB yine
fiziksel bölümle tutarlı tutulur. Uyarılar filtrelenmez. Bu alanları içeren
paket cihazda aynı 0x7B'yi verdiği için geometri artık kök neden sayılmıyor;
doğru disk tanımı olarak korunuyor.

## 0x7B reset gerektirmeyen uçuş kaydı

Kök volume bağlanamadığı için bu hata anında registry, ETW, dosya veya crash
dump çıktısına güvenilemez. Debug paketi bu nedenle `0xCF050000..0xCF05FFFF`
aralığındaki firmware-reserved belleğe yalnız olay kimliği, durum ve beş adet
32-bit argüman yazar. Sabit ABI'deki IRQL baytı mevcut ARM64 yapıda `0xFF`
(`?`, bilinmiyor) değeridir; tanı uğruna çözümlenemeyen kernel importu eklenmez.
Medya payload'ı kaydedilmez. UEFI bellek haritasında bu aralık
`EfiReservedMemoryType` olan `Lower dynamic guard` içindedir ve mevcut EL2,
framebuffer ve runtime tanı alanlarıyla çakışmaz.

Windows alanı `MmNonCached` eşler. Microsoft non-cached bellek üzerinde
`Interlocked*` kullanımını yasakladığı için halka indeksini doğrudan atomik
artırmaz. Cached sürücü değişkenindeki best-effort busy kapısı özyinelemeli
yazarı beklemeden düşürür; iki CPU aynı anda kapıdan geçerse yalnız aynı tanı
slotu çakışabilir. Commit sözcüğü karışmış kaydı geçersiz kılar. Bu alan UFS
komutunu veya kaynağını korumaz ve SYS'e CRT/kernel atomik sembolü ithal etmez.
Her record'un commit sözcüğü en son, header write index'i ise tamamlanmış
record'dan sonra yazılır. Böylece sıcak reset ortasındaki yarım kayıt UEFI
okuyucusu tarafından geçerli sayılmaz.

Tanı paketi sırası:

```bash
# 1. Windows PowerShell'de güncel 0.0.9.0 SYS'i derle.
# 2. WSL'de güncel B2LGoster uygulamasını üret.
bash yap.sh

# 3. Normal disk-boot yerine uçuş kayıtlı paketi üret.
bash tools_ufsboot.sh --debug '/mnt/c/yol/Windows_ARM64.iso' 4
```

`--debug`, CACHE'e `B2LGOSTR.EFI` ve özel `startup.nsh` koyar. İlk UEFI
ekranını fotoğraflayıp bir tuşa basınca kayıt `ARMED` olur ve Windows başlar.
Debug BCD'si normal `disk` yerine onunla aynı `locate` aygıtlarını kullanan
`menu` varyantıdır: `bootmgfw` SYSTEM bölümünü ve `winload.efi`yi bulduktan
sonra 30 saniyelik metin menüsü gösterir. Menü görünürse Boot Manager,
BCD ve NTFS taraması tamamlanmıştır; Enter ile hemen devam edilir. Menü hiç
görünmezse bekleme B2LUFS/çekirdekten önce, `bootmgfw` içindedir.
Fotoğrafla doğrulanan platform sınırı nedeniyle Windows 0x7B sonrasında cihazı
restart edemiyor. Bu mod bu yüzden WinPE `CrashControl\AutoReboot=0` değerini
yazıp geri okur. B2LUFS DriverEntry'de `0xCA000000` framebuffer eşlemesini ve
`KeRegisterBugCheckCallback` kaydını yalnız ARMED tur için hazırlar. Callback
HIGH_LEVEL'da hiçbir bellek ayırma, haritalama veya kilit işlemi yapmadan son
altı geçerli kaydı mavi ekranın üstündeki `B2LUFS BUGCHECK FLIGHT` paneline
yazar. Panelin fotoğrafı doğrudan sonuçtur; ikinci UEFI açılışı gerekmez.

UEFI DEBUG satırlarındaki `ConvertPages: failed to find range` bu checkpoint
ölçümünün sonucu değildir. EDK2 aynı uyarıyı görüntü yüklendikten sonra üretir;
sonraki `Loading driver ... bootmgfw.efi` ve `ProtectUefiImage` satırları
Boot Manager görüntüsünün başarıyla hazırlandığını gösterir. Asıl ilerleme
kanıtı menünün çizilmesidir. `bootmgfw` geri dönerse özel startup betiği bunu
`BOOTMGFW GERI DONDU: fsN` satırıyla ayrıca görünür kılar.

Panel sütunları `SEQ`, `EVT` ve `A0..A4` alanlarıdır. `EVT` aşağıdaki olayın
16-bit sayısal kodudur (`020F=INITOK`, `0310=SUBMIT`, `0311=DONE` gibi).
Callback kaydı veya framebuffer eşlemesi başarısızsa panel görünmez; bu iki
durum header `Flags` alanında ayrı bitlerle tutulur. Normal, ARMED olmayan
paket framebuffer'ı eşlemez ve callback kaydetmez.

Ekrandaki son olay temel ayrımı verir:

| Son olay | Anlam |
|---|---|
| `ARMED AMA SURUCU YAZMADI` | B2LUFS `DriverEntry` çalışmadı veya sabit alan eşlenemedi |
| `0001 ENTRY` / `0002 STORRET` | DriverEntry başladı / `StorPortInitialize` döndü |
| `010F FINDRET` | ACPI/StorPort kaynak ya da common-DMA aşamasında kaldı; A0 dönüş kodudur |
| `0201 TRANSP` | HCE/HCS/UTRL devralma sonucu; A0 taşıma aşama kodudur |
| `0202 CAP`, `0203 LUNS`, `0204 MEDIA`, `0210 INITFAIL` | Boot öncesi eşzamanlı medya doğrulamasının hangi kapıda kaldığını gösterir |
| `020F INITOK` | Miniport boot aygıtını StorPort'a hazır ilan etti |
| `0300 STARTIO` | StorPort en az bir gerçek SRB gönderdi; A0 SRB function'dır |
| `0310 SUBMIT` ama `0311 DONE` yok | Son UFS komutu tamamlanmadan kaldı; A0 CDB opcode'dur |
| `0311 DONE` | Son donanım komutu tamamlandı; A1 sonuç, A3 OCS, A4 interrupt status'tur |
| `0301 CCAP` / `0302 CGPT` | Cached capacity/GPT cevabı Windows depolama yığınına verildi |

Crash paneli son altı geçerli kaydın bütün A0..A4 alanlarını gösterir. Daha
sonra bir yazılım reset yolu sağlanırsa UEFI ekranı aynı kayıtları adlarıyla
yorumlamaya devam eder. 64 KiB ham görüntü başka bir yolla alınırsa tüm halka
şu araçla çözülür:

```bash
python3 tools_b2lufs_flight_decode.py b2lufs-flight.bin --last 128
```

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
Disk-boot üretimi her çalıştırmada `boot.wim`i ISO'dan zorla yeniler. Bunun
amacı yalnız önceki B2LUFS paketini değil, tanı araçlarının offline SYSTEM
hive'ında bırakabildiği Start/ErrorControl değişikliklerini de kesin olarak
dışarıda tutmaktır.

BCD üretiminde Windows batch dosyası `\\wsl.localhost` veya WSL içinden UAC
zinciriyle çalıştırılmaz. Araç, gerçek Windows Masaüstü konumunda
`B2L-UFS-BCD` klasörünü hazırlar ve pahalı WIM işlemlerinden önce bekler.
Yönetici Komut İstemi'nde ekranda gösterilen klasöre geçip
`B2L_BCD_DISK_OLUSTUR.cmd` çalıştırılır. Batch'in başarılı işareti, `disk`
varyantı, dosya boyutu ve BCD'nin `regf` başlığı doğrulandıktan sonra akış
devam eder. Böylece `bcdedit` hatasının tamamı açık CMD penceresinde kalır.
PowerShell'in OEM konsol kodlaması `Masaüstü` gibi yolları bozmasın diye bilinen
Masaüstü yolu UTF-8 Base64 olarak WSL'ye taşınır ve orada geri açılır.
Masaüstündeki eski başarı dosyası tek başına yeterli değildir: sahnelenen BCD
üretici ve başlatıcı betiklerinin ortak istek kimliği yeni `ready` kaydında
aynı olmalıdır. Üretilen depo ayrıca geri açılır; `device` için
`locate=\Windows\System32\Boot\winload.efi`, `osdevice` için
`locate=\Windows` okunmadan akış ilerlemez. Test-imzalı B2LUFS için
`testsigning=Yes` zorunludur. İmzasız sürücü
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

Üretim sırasında `system.img` için NTFS BPB imzası; 4096 bayt sektör/küme;
SYSTEM hidden/start LBA `83968`; 255 heads ve 63 sectors-per-track geri
okunarak doğrulanır. `cache.img` FAT16 oluşturulur ve CACHE hidden/start LBA
`1983488` ile aynı CHS alanları doğrulanır. Her imajın 4096 baytlık blok sayısı
PIT bölüm kapasitesini aşarsa paketleme durur. Araç, SYS yoksa veya
kaynaklardan eskiyse pahalı ISO/BCD/DISM adımlarından önce durur ve gerekli
PowerShell komutunu gösterir. WSL tarafında MSVC derlemesi yapılmaz.

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
- `INACCESSIBLE_BOOT_DEVICE` görülürse EFI/BCD ve winload aşaması geçilmiş,
  fakat kernel boot volume'u yeniden açamamıştır. Bunun nedeni yalnız miniport
  değildir; disk/partition/volume/NTFS zinciri de olabilir. Güncel paket
  `0.0.9.0` eşzamanlı medya hazırlığını, PIT uyumlu BPB'yi, düzeltilmiş BCD
  OS-aygıt tanımını ve reset gerektirmeyen bugcheck uçuş panelini birlikte
  taşır.
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
| `cache_esp.sh --win --disk --ufs-debug` | Uçuş kaydını ARMED yapan CACHE ESP imajını üretir. |
| `tools_b2lufs_flight_decode.py` | Ham 64 KiB uçuş kaydını doğrular ve çözer. |

Sürücü kaynaklarının bağımsız ikinci kopyası `drivers/windows/storage/b2lufs`
altındadır. Canlı kaynak değişikliğinden sonra yedekler
`bash drivers/sync-driver-backups.sh update` ile eşitlenir.
