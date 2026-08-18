# Windows tarafına kendi sürücümüzü yazmak

Soru kullanıcıdan geldi: *"Linux'taki veriyle kendi UFS sürücümüzü baştan yazıp
ekleyemez miyiz?"*

Cevap: evet, ve `storufs.sys` kapısı yapısal olarak kapandığına göre (bkz.
STATUS.md, "_CCA=0 sonuç vermedi") **tek gerçek çözüm bu.** Ama sıra önemli.

## Güncel durum — taşıma kanıtlandı, disk-boot uçuş kaydıyla ölçülüyor

Bu belgedeki sürücü planı artık yalnız bir tasarım değildir. `b2lufs` aynı
WinPE oturumunda dahili UFS'i 119 GB Disk 0 olarak yayımladı, 31 GPT bölümünü
okudu ve CACHE üzerinde dosya yazıp geri okudu. Son disk-boot denemesinde
görülen `INACCESSIBLE_BOOT_DEVICE` bu nedenle yeni bir Exynos taşıma sürücüsü
yazmayı gerektiren kanıt değildir: EFI/BCD aşaması ve UFS veri yolu geçmiş,
çekirdek açılış volume'unu devralma aşamasında kalmıştır.

DriverStore ve SYSTEM Basic Data düzeltmeleri cihazda uygulandıktan sonra stop
code değişmedi. Paketleme çıktısı aynı SYS'in üç katmanda da bulunduğunu
kanıtladı. Bir sonraki mimari fark, miniportun boot aygıtını başlangıçta gizlemesiydi:
normal RAM-WinPE'de kapasite/GPT probları PASSIVE callback'te bitiyor ve 500 ms
timer'ın bus taraması diski sonradan oluşturuyordu. Diskten boot ederken kernel
kök volume'u bu callback/timer zincirinden önce istediği için ilk keşif
SRB'leri `NO_DEVICE` ile dönüyordu.

Microsoft'un boot-drive StorPort kısıtı boot aygıtının `HwStorInitialize`
dönmeden erişilebilir olmasını zorunlu tutar. Güncel `0.0.7.0` bu nedenle
kapasite, LUN ve GPT snapshot problarını eşzamanlı `HwInitialize` yoluna taşır;
SYSTEM eşleşmesi dahil bütün boot-medya koşulları hazır olmadan TRUE dönmez.
PASSIVE/timer yalnız telemetri ve yedek yeniden taramadır, boot için ön koşul
değildir.

`0.0.7.0` cihazda yine aynı `INACCESSIBLE_BOOT_DEVICE` sonucunu verdi. Bu,
yukarıdaki yaşam döngüsü düzeltmesini geçersiz kılmaz; fakat tek kök neden
olduğu çıkarımını geçersiz kılar. Paketleme tekrar incelendiğinde NTFS-3G'nin
Windows boot için zorunlu saydığı bölüm başlangıcı ve CHS alanlarının sıfır
bırakıldığı, üstelik aracın boot edilemez uyarısının script tarafından
gizlendiği bulundu. Stok PIT'te SYSTEM başlangıcı 83968, CACHE başlangıcı
1983488 4Kn LBA'dır. Güncel imaj üreticileri bu değerleri BPB'ye yazar,
255/63 uyumluluk geometrisini ekler, PIT kapasitesini ve bütün alanları içerik
kopyasından sonra tekrar doğrular. Bu paketleme düzeltmesi cihazda da aynı
0x7B sonucunu verdi; doğru geometri olarak korunur ama kök neden değildir.

Güncel sürücü ham telefon GPT'sine yazmadan, salt ve CRC-tutarlı Windows
snapshot'ında hem `SYSTEM` hem `CACHE` girdilerini Basic Data yapıp attribute
alanlarını temizler. SYSTEM imajı NTFS, CACHE imajı FAT16 olarak üretilir;
iki BPB'nin imzası, 4Kn sektör/küme alanları, PIT hidden/start LBA'sı ve 255/63
geometrisi ayrıca geri okunur. `SchemaVersion=26`
iki girdinin bulunma durumunu ve indexini ayrı telemetri olarak yayımlar.

PIT-uyumlu BPB paketi de cihazda aynı 0x7B'yi verdi. Baştan sona denetimde
disk BCD'sinin `device` ve `osdevice` öğelerini aynı `winload.efi` locator'ına
bağladığı bulundu. İlki boot uygulaması için doğrudur; ikincisi Windows sistem
kökünü göstermelidir. Güncel BCD bu nedenle
`device=locate:\Windows\System32\Boot\winload.efi` ve
`osdevice=locate:\Windows` üretir, ardından ikisini BCD deposundan geri okur.
Eski masaüstü BCD sonucu betik hash'ine bağlı istek kimliği olmadan kabul
edilmez; disk-boot WIM'i de her tur ISO'dan temiz çıkarılır. Bu düzeltme de
cihazda aynı 0x7B sonucunu verdi ve tek kök neden olmadığı kanıtlandı.

Güncel `0.0.8.0` yeni bir taşıma davranışı tahmin etmek yerine miniportun en
erken boot yaşam döngüsünü sıcak-reset uçuş kaydına alır. `0xCF050000` tabanlı
64 KiB alan UEFI tarafından `ARMED` yapılır; DriverEntry, kaynak/DMA, HCI
devri, capacity/LUN/GPT başlangıç probları, StorPort SRB'leri ve her gerçek UFS
komutunun begin/done sonucu commit'li kayıtlardır. Debug WIM'i
`AutoReboot=1` ile hazırlanır; 0x7B sonrasındaki yazılım sıcak resetinde
B2LGoster kaydı okur. Böylece bir sonraki tur varsayım değil, son tamamlanan
callback/CDB/OCS/interrupt status üzerinden ilerler.

Disk-boot akışı ayrıca INF/SYS/CAT paketini Windows DISM ile çevrimdışı
DriverStore/DriverDatabase'a kurar; sonra `Start=0` servis kaydı ve
`CriticalDeviceDatabase\ACPI#BYND9820` eşlemesini uygular. WIM geri alındığında
hem DriverStore paketinin üç dosyası hem de bütün mevcut control set'lerdeki
boot-critical kayıtlar doğrulanmadan SYSTEM/CACHE paketi üretilmez. Böylece
Faz 3'te doğrulanmış miniport mantığı korunur ve sorun doğru dağıtım katmanında
çözülür.

## Neden yapılabilir

Kalibrasyon mantığı elimizde ve **cihazda çalıştığı doğrulandı**:

- `Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.c` — UFSHCI protokolü,
  UTRD/UPIU, SCSI READ_10/WRITE_10, LUN taraması
- `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib/` —
  bizim yazdığımız 9820 katmanı: register adresleri, `mclk_rate`, CAL tabloları

Kanıt: `menu` varyantında `winload.efi` bu sürücüyle UFS üzerindeki NTFS'ten
**50 saniye boyunca kesintisiz** ~150 MB okudu. Tek hata satırı yok.

Yani ters mühendislik yapılmayacak. İş, çalışan C kodunu EDK2 arayüzünden
StorPort arayüzüne taşımak.

## Üç engel, ikisi zaten ölçülmüş

### 1. İmza — test yolu açık, üretim imzası kapsam dışı

ARM64 Windows çekirdek modu sürücü imzası istiyor. Bunu bir kez gördük:

```
File: \windows\System32\Drivers\ws2ifsl.sys
Status: 0xc0000428   (STATUS_INVALID_IMAGE_HASH)
```

Çözüm: mevcut kendi sertifikamızla imzalama ve BCD'de `testsigning on`
yoludur.

> **Geçerli çalışma kuralı:** Bu proje hâlâ donanım get-up/test evresindedir.
> Mevcut test-imzalama akışı korunur; WHQL/üretim imzası, imza politikası
> sertleştirme veya test modunu kaldırma şu an kapsam dışıdır. Her yeni
> `b2l*` test sürücüsü önce aynı yol üzerinden denenir. Böylece her flaş
> sürücünün donanım davranışına dair tek bir soruyu cevaplar; imza zinciri
> yeni bir değişken olmaz.

Secure Boot'un zorlamadığını **tahmin etmiyoruz, ölçtük**: kendi derlediğimiz,
hiç imzalanmamış `EBSTEST.EFI` cihazda çalıştı ve `bootmgfw.efi` yüklenirken
`[Security] 3rd party image[0] can be loaded after EndOfDxe` satırı geçti.
İmzasız EFI imajları çalışıyorsa Secure Boot zorlamıyor demektir.

### 2. Çıktı — ASIL SORUN, VE ÇÖZÜMÜ SÜRÜCÜNÜN KENDİSİNDE

Otuz turdur Windows'un içinden tek bir bayt duyamadık. Bir çekirdek sürücüsünü
bu koşulda yazmak imkânsız — her derleme bir Odin flaşı ve bir fotoğraf demek.

Ama çözüm elimizin altında: **çekirdek sürücüsü framebuffer'a doğrudan
çizebilir.** Adresi biliyoruz ve hafıza haritamızda ayrılmış:

```
{"Display Reserved", 0xCA000000, 0x01400000, AddMem, MEM_RES, ..., Reserv, WRITE_THROUGH}
```

`MmMapIoSpace` ile haritalayıp piksel yazmak birkaç satır. Bu, projedeki en
büyük tıkanıklığı — Windows'un sessizliğini — sürücü yazmaya başlamadan önce
açıyor.

### 3. Geliştirme döngüsü

Her tur: derle → WIM'e enjekte → ESP üret → Odin → fotoğraf. Yavaş. Bu yüzden
her flaşın tek bir soruyu cevaplaması şart, ve Faz 1 bittikten sonra sürücü
kendi durumunu ekrana yazabildiği için tur başına öğrenilen bilgi katlanıyor.

## Sıra

### Faz 2.5 — StorPort kaynak sözleşmesi (başladı)

İlk yanlış yaklaşım, Windows'un genel `storufs` sürücüsüne bir yardımcı
sürücü ekleyip sabit fiziksel adreslerden Exynos ayarı yazmak olurdu. Bu hem
iki sürücünün aynı denetleyiciye sahip çıkmasına yol açar, hem de PnP'nin
verdiği kaynakları atlar. Doğru sınır artık `windows/b2lufs/` altındadır:

- `b2lufs_resources.c`, altı 9820 kaynağını **taban adresine göre** doğrular;
  kaynakların `_CRS` içindeki sırasını varsaymaz;
- saf birim testi WDK olmadan çalışır;
- b2lufs INF turunda `_CID=BYND9820` yalnız yeni miniporta verilecek ve
  `storufs` aynı aygıta bağlanmayacaktır; o ilk bağlı tur yalnız kaynakları
  raporlayıp HCE yazmadan başarıyla duracaktır;
- PnP paketi, gömülü SYS imzasına ek olarak INF+SYS hash'lerini kapsayan
  aynı test sertifikalı bir `b2lufs.cat` içerir;
- HCE, DME, PHY, CAL veya UFS'ye yazma henüz yoktur.

İlk gerçek HCI kanıtı da alındı: `CAP=1303FF1F`, `VER=00000300`, `HCE=1`,
`1US_TO_CNT=000000A6`. Bu, denetleyicinin firmware devrinden sonra canlı
kaldığını, 32 UTR (`CAP[4:0]+1`), 4 UTMR (`CAP[18:16]+1`) ve 64-bit DMA
adreslemesini desteklediğini gösterir. Dolayısıyla generic `storufs`un Code
10 sonucu "denetleyici kapalı" değildir; Exynos'a özgü reset-sonrası PHY/CAL
get-up eksikliğiyle uyumludur. Bu dört kapasite değeri tekrar ölçülmeyecek;
sonraki tur PnP'nin yalnız `b2lufs.inf` ile **Started** oluşunu kanıtlar.

Bu, Microsoft'un StorPort modeline uyar: PnP'nin sağladığı `ACCESS_RANGE`
dışında başka HBA adresi aranmaz. Windows'un örnek deposu WDK/INF ve PnP
iskeletini doğrulamak için kullanılacak; 9820'nin register ve CAL içeriği
stok kaynak ile çalışan UEFI portundan gelecek.

### Faz 1 — "yaşıyorum" çubuğu

Yüz satırlık bir WDM/KMDF sürücüsü. `DriverEntry` içinde:

1. `MmMapIoSpace(0xCA000000, ...)` — `MmNonCached`
2. Ekranın üst şeridine düz renk yaz
3. `return STATUS_SUCCESS`

`Start = 0` (boot) yapılıyor ve `CriticalDeviceDatabase` gerekmiyor; sürücü
aygıta bağlanmıyor, sadece yükleniyor.

| Ekranda | Sonuç |
|---|---|
| Çubuk çıkıyor | Test imzalı sürücü yükleniyor **ve** Windows'un içinden çıkış kanalımız var |
| `0xc0000428` ekranı | İmza/`testsigning` yapılandırması eksik — düzeltilebilir, hata görünür |
| Hiçbir şey | Sürücü yüklenmiyor ve sebebini söylemiyor; `sos On` ile tekrar |

İki ihtimalin ikisi de bilgi. Şu ana kadarki sessiz turlardan farkı bu.

### Faz 1b — ayrılmış RAM'e halka tampon (ölüm sonrası kayıt)

Öneri kullanıcıdan geldi: *"madem sıfırdan driver yazacağız USB serial
driver'ı yazalım ki işimiz sonrasında kolaylaşsın."* Amaç doğru — kopyalanabilir
log, ekran fotoğrafından kat kat iyi. Ama USB'nin maliyeti çok yüksek:

Windows'ta pratik bir USB **device/gadget** modu yok. Linux'un gadget
çatısının karşılığı UFX (`Ufx01000.sys`) ve WinPE'de bulunmuyor; olsa bile
DWC3 için UFX istemci sürücüsü yazmak endpoint yönetimi, descriptor'lar ve
CDC-ACM sınıfı demek. Hepsi de çıktı kanalı **olmadan** yazılacak.

Aynı faydanın ucuz hali: ayrılmış belleğe sıcak-reset halka tamponudur. Planın
uygulanan güncel adresi **`0xCF050000`**'dır; `0xCF000000..0xCF03FFFF` EL2 ve
`0xCF040000..0xCF04FFFF` runtime tanı alanlarıyla çakışmaması için seçilmiştir.

- Bölge hafıza haritamızdaki `Lower dynamic guard` içinde ayrılmıştır
  (`EfiReservedMemoryType`)
- **DRAM'in sıfırlamayı hayatta geçirdiğini ölçtük**: `B2LResetHookDxe`
  sayacı 0x403'ten 0x404'e geçti
- B2LGoster aynı sabit ABI'yi yorumlayıp son kayıtları büyük yazıyla gösterir

Yani sürücü çöküş anında oraya yazar, cihaz sıfırlanır, sonraki açılışta
Shell logu ekrana basar. **Çöküş anındaki kaydı çöküşten sonra okumak** —
framebuffer'ın yapamayacağı şey, ve yirmi satır kod.

İkisi birlikte tam bir hata ayıklama düzeni kuruyor:

| Kanal | Ne için |
|---|---|
| Framebuffer | Canlı durum, ilerleme |
| Ayrılmış RAM | Ölüm sonrası; çöküşün son satırları |

#### "DRAM cihaz kapanıp açılınca silinmez mi?"

Silinir. Ama ihtiyacımız olan senaryoda silinmiyor, ve bu ölçüldü.

| Sıfırlama türü | DRAM | `RST_STAT` |
|---|---|---|
| **Sıcak** — SoC resetlenir, DRAM'e güç kesilmez | **Kalıyor** | `0x20000000` |
| **Soğuk** — batarya/güç kesilir | Gidiyor (hücreler saniyeler içinde bozulur) | `0x40010000` |

Windows'un çöküşü her seferinde `0x20000000` veriyor — yani sıcak. Ve
`B2LResetHookDxe` sayacının sıfırlama boyunca `0x403` → `0x404` geçtiğini
gördük: S-Boot o bölgeyi ne siliyor ne üzerine yazıyor.

**Tek kural: çöküşten sonra cihazı elle kapatma.** Zaten kendisi yeniden
başlıyor ve `startup.nsh` açılışta adresi döküyor. Araya güç kesintisi ya da
Odin flaşı girerse kayıt gider.

Bu, USB seriye göre gerçek bir dezavantaj: USB'de log gerçek zamanlı akar ve
güç durumundan bağımsızdır. Ama tek çöküş senaryosunda ikisi de aynı bilgiyi
veriyor ve biri yirmi satır.

USB seri kapatılmadı, üçüncü sıraya alındı. Faz 2'den sonra hâlâ gerekiyorsa
o zaman değerlendirilir — ama muhtemelen gerekmeyecek.

### Faz 2 — framebuffer konsolu

Aynı sürücüye gömülü 8x16 bitmap font ve bir `FbPrint()`. Artık Windows'un
içinden metin okuyoruz. Buradan sonrası normal geliştirme.

### Faz 3 — StorPort miniport

`UfsDxe.c` + `PlatformUfsLib` taşınıyor. Eşleştirme kabaca:

| EDK2 | StorPort |
|---|---|
| `InitUfsDriver` | `HwFindAdapter` + `HwInitialize` |
| `MmioRead32/Write32` | `StorPortReadRegisterUlong/WriteRegisterUlong` |
| `MicroSecondDelay` | `StorPortStallExecution` |
| `AllocateAlignedPages` | `StorPortAllocateContiguousMemorySpecifyCacheNode` |
| `UfsUtpCmdProcess` | `HwStartIo` (SRB → UPIU) |
| kesme yoklaması | `HwInterrupt` (GSIV 281 zaten doğrulandı) |

Kalibrasyon kodu (`UfsCal.c`, tablolar) **olduğu gibi** taşınıyor; MMIO
sarmalayıcıları dışında değişmesi gereken bir şey yok.

`_CLS` DSDT'den kaldırılacak ya da `_HID` kendi kimliğimize çevrilecek ki
`storufs.sys` araya girmesin.

## Not

`ufs-cal-9820.c` tabloları GPL. Kişisel araştırma için sorun değil; dağıtım
düşünülürse lisans durumu ayrıca değerlendirilmeli. Aynı durum EDK2 portunda
da geçerliydi.

## Ön koşul

Faz 1'in test edilebilmesi için WinPE'nin açılıp **ayakta kalması** gerekiyor.
Sıradaki flaş (RAM disk + `menuram` + üç satırlık `startnet.cmd`) tam olarak
bunu ölçüyor. Önce o.
