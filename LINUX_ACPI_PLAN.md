# Linux'u ACPI test koşumu olarak kullanmak

## Neden bu belge var

Nihai hedef **Windows on ARM**. "Önce Linux boot edelim" refleksi doğru
görünüyor ama nasıl yaptığımıza bağlı olarak Windows'a hiç katkısı olmayabilir.

ARM64 Linux iki şekilde boot eder:

| Yol | Donanım tanımı | Windows'a katkısı |
|---|---|---|
| **DTB ile** | Device Tree (bizde hazır) | Neredeyse yok |
| **ACPI ile** | ACPI tabloları | **Doğrudan** |

DTB yolu kolay ve bizde çalışan bir DTB var. Ama Windows DTB okumaz — ACPI
zorunludur. DTB ile Linux boot etmek UEFI çekirdeğini doğrular (memory map,
ExitBootServices, GOP) ama Windows'un asıl zorluğuna hiç dokunmaz.

**ACPI ile Linux boot etmek ise bizim ACPI tablolarımızı test eder.** Ve Linux
bu konuda son derece konuşkandır: MADT/GTDT/FADT/DSDT'de ne bulduğunu,
neyi ayrıştıramadığını satır satır yazar. Windows aynı durumda sessizce ölür.

Yani Linux'u **hedef olarak değil, ACPI hata ayıklayıcısı olarak** kullanıyoruz.

## Elimizde ne var

| Parça | Durum |
|---|---|
| UEFI + Boot Services | ✅ |
| GOP (SimpleFbDxe) | ✅ → Linux `efifb` konsolu buradan çıkar |
| Depolama + FAT (FS0 = cache) | ✅ |
| EFI Shell + `startup.nsh` | ✅ → kernel'i buradan çalıştıracağız |
| S5E9820 ACPI tabloları | ✅ yazıldı, henüz derlenmedi |
| ACPI.inc | ⏳ kapalı |

## Hangi kernel

**Exynos 9820 sürücülerine ihtiyacımız yok.** Amaç root'a ulaşmak değil,
ACPI ayrıştırmasını görmek. Yani genel bir ARM64 dağıtım kernel'i yeterli:

- `CONFIG_EFI_STUB` — `Image`'i doğrudan UEFI uygulaması gibi çalıştırır
- `CONFIG_ACPI` — tabloları okur ve raporlar
- `efifb` — UEFI GOP framebuffer'ını devralır, ekrana yazar

Aday: **Alpine Linux aarch64 netboot** (`vmlinuz-lts` ~10 MB +
`initramfs-lts` ~15 MB). 75 MB'lık CACHE'e rahat sığar.

Beklenen davranış: kernel açılır, ACPI tablolarını ayrıştırır, CPU'ları
MADT'den kurar, timer'ı GTDT'den alır, ekrana log basar, sonunda root
bulamayıp panic verir. **Panic başarıdır** — oraya kadar gelmesi ACPI'nin
çalıştığı anlamına gelir.

## Beklenen hata akışı ve ne anlama geldikleri

| Linux çıktısı | Anlamı | Windows'a etkisi |
|---|---|---|
| `ACPI: Early table checksum verification failed` | Tablo checksum'ı bozuk | Windows hiç açılmaz |
| `ACPI: FADT not found` | FACP eksik/hatalı | Kritik |
| `smp: Brought up 1 node, N CPUs` | MADT doğru | ✅ |
| `arch_timer: cp15 timer(s) running` | GTDT doğru | ✅ |
| `efifb: framebuffer at 0x...` | GOP handoff doğru | ✅ |
| root bulunamadı → panic | Buraya kadar her şey tamam | ✅ |

Bu tablo, Windows'a geçmeden önce hangi ACPI parçasının çalıştığını tek tek
doğrulamamızı sağlıyor. Windows'ta bu geri bildirimin hiçbiri yok.

## Adımlar

1. **ACPI tablolarını derle ve etkinleştir**
   ```bash
   sudo apt install -y acpica-tools
   bash yap.sh --acpi --clean
   ```
   Bu, `iasl` ile `.dsl → .aml` çevirir, `ACPI.inc`'i açar ve UEFI'yi derler.
   Hedef: log'da `AcpiPlatformDxe start failed: Aborted` satırının **kaybolması**.

2. **Kernel + initramfs'i ESP'ye koy**
   ```bash
   cd ~/UEFI_beyond2lte/esp
   curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/vmlinuz-lts
   curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/initramfs-lts
   ```

3. **`startup.nsh`'ı kernel'i çalıştıracak şekilde düzenle** (aşağıda)

4. **Cache'i yaz, boot et, ekranı fotoğrafla**

## startup.nsh — ACPI testi

```
@echo -off
echo ACPI testi: Alpine aarch64, DTB YOK, sadece ACPI
fs0:\vmlinuz-lts initrd=\initramfs-lts console=tty0 earlycon acpi=force loglevel=7
```

`acpi=force` önemli: ARM64'te Linux DTB varsa onu tercih eder. Bizde DTB
vermediğimiz için zaten ACPI'ye düşer, ama açıkça belirtmek belirsizliği
kaldırıyor.

`earlycon` + `loglevel=7` erken ACPI mesajlarını görünür kılar.

## Sonrası

ACPI tabloları Linux'u tatmin ettiğinde sıra Windows'a gelir. O aşamada
eksik olan tek şey **DSDT'de UFS depolama cihazının tanımlanması** olacak —
Windows'un `INACCESSIBLE_BOOT_DEVICE` vermemesi için. Şu anki
`DSDT_minimal.aml` adı üstünde minimal; cihaz düğümü içermiyor.

Yani sıralama: ACPI çekirdeği doğrula (Linux) → DSDT'yi genişlet →
Windows dene.
