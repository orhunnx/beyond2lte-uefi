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

### 1. İmza — AÇIK

ARM64 Windows çekirdek modu sürücü imzası istiyor. Bunu bir kez gördük:

```
File: \windows\System32\Drivers\ws2ifsl.sys
Status: 0xc0000428   (STATUS_INVALID_IMAGE_HASH)
```

Çözüm: kendi sertifikamızla imzalayıp BCD'de `testsigning on`.

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
