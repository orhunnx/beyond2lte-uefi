# Windows tarafına kendi sürücümüzü yazmak

Soru kullanıcıdan geldi: *"Linux'taki veriyle kendi UFS sürücümüzü baştan yazıp
ekleyemez miyiz?"*

Cevap: evet, ve `storufs.sys` kapısı yapısal olarak kapandığına göre (bkz.
STATUS.md, "_CCA=0 sonuç vermedi") **tek gerçek çözüm bu.** Ama sıra önemli.

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

Aynı faydanın ucuz hali: **`0xCF000000`'daki ayrılmış bölgeye halka tampon.**

- Bölge hafıza haritamızda zaten ayrılmış (`B2L EL2`, `Reserv`,
  `UNCACHED_UNBUFFERED`)
- **DRAM'in sıfırlamayı hayatta geçirdiğini ölçtük**: `B2LResetHookDxe`
  sayacı 0x403'ten 0x404'e geçti
- `startup.nsh` zaten `dmem` ile o adresi döküyor

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
