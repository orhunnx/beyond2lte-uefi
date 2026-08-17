# Driver yedekleri ve bileşen haritası

Bu dizin, derlemenin kullandığı canlı kaynaklardan bağımsız, bileşenlere göre
ayrılmış **ikinci kopyayı** tutar. Build araçları bu dizinden derleme yapmaz;
canlı yollar değişmeden kalır. Bu ayrım yanlışlıkla yedek üstünde çalışmayı veya
bir yedeği canlı sürücü sanmayı önler.

## Kullanım kuralı

- Kod değişikliği canlı kaynakta yapılır.
- Doğrulama ve commit öncesinde `bash drivers/sync-driver-backups.sh update`
  çalıştırılarak yedek yenilenir.
- `bash drivers/sync-driver-backups.sh check` hiçbir dosyayı değiştirmeden canlı
  kaynaklarla yedeğin aynı olduğunu doğrular.
- Araç yalnız Git tarafından izlenen dosyaları kopyalar. Test anahtarı, derleme
  çıktısı ve `windows/b2lufs/TANI_DISKPART.md` gibi yerel dosyalar yedeğe girmez.
- Yeni bir sürücü/bileşen eklendiğinde `MIRROR_MANIFEST.tsv` ve aşağıdaki tablo
  aynı commit içinde güncellenir.

## Dizin düzeni

```text
drivers/
├── windows/
│   ├── storage/b2lufs/          # Windows ARM64 UFS StorPort miniport yedeği
│   └── display/b2lfb/           # Windows framebuffer/tanı sürücüleri yedeği
└── firmware/
    ├── storage/
    │   ├── platform-ufs/        # Exynos 9820 UFS CAL ve board init
    │   └── ufs-dxe-overlay/     # Ortak UfsDxe INF düzeltmesi
    ├── input/keypad/             # Yan tuş platform kitaplığı
    └── platform/
        ├── el2/                  # EL2 geçiş sürücüsü ve payload
        ├── gic-suspend/          # GIC askıya alma kancası
        ├── reset-hook/           # Reset akışı kancası
        ├── runtime-hook/         # Runtime service kancası
        ├── seh/                  # Exception/SEH desteği
        └── memory-map/           # Platform bellek haritası
```

## Canlı kaynaklar ve görevleri

| Yedek bileşen | Canlı build kaynağı | Görevi | Nasıl kullanılır |
|---|---|---|---|
| `windows/storage/b2lufs` | `windows/b2lufs` | Exynos 9820 UFSHCI'yi Windows StorPort'a bağlar; SCSI komutlarını, GPT görünümünü ve blok I/O'yu sağlar. | SYS yalnız Windows PowerShell/MSVC+WDK ile derlenir; paketleme WSL'de `tools_b2lufs.sh` ile yapılır. |
| `windows/display/b2lfb` | `windows/b2lfb` | Framebuffer ve Windows açılış aşaması tanısı için kullanılan ARM64 sürücü/yardımcı bileşenler. | `tools_b2lfb.sh` ile derlenip WinPE/driver paketine alınır. |
| `firmware/storage/platform-ufs` | `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib` | Exynos 9820 UFS PHY/CAL tabloları, saat, GPIO ve link başlangıç ayarları. | EDK2, `PlatformUfsLib.inf` üzerinden ortak `UfsDxe` sürücüsüne bağlar. |
| `firmware/storage/ufs-dxe-overlay` | `mu-silicium-overlay/Silicon/Samsung/SamsungPkg/Drivers/UfsDxe` | Ortak Samsung UFS DXE paket tanımındaki platform düzeltmeleri. | `yap.sh` overlay'i Mu-Silicium ağacına kopyalarken etkinleşir. |
| `firmware/platform/el2` | `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LEl2Dxe` | Windows/OS devri için EL2 durumunu ve ilgili payload'ı yönetir. | Platform DSC/FDF bileşeni olarak UEFI imajına girer. |
| `firmware/platform/gic-suspend` | `.../Drivers/B2LGicSusDxe` | GIC durumunu askıya alma/geçiş akışı için hazırlar. | Platform DXE bileşeni olarak yüklenir. |
| `firmware/platform/reset-hook` | `.../Drivers/B2LResetHookDxe` | Platform reset davranışını yakalar ve düzenler. | Platform DXE bileşeni olarak yüklenir. |
| `firmware/platform/runtime-hook` | `.../Drivers/B2LRtHookDxe` | UEFI runtime service geçişlerini platforma uyarlar. | Platform DXE bileşeni olarak yüklenir. |
| `firmware/platform/seh` | `.../Drivers/B2LSehDxe` | AArch64 exception/SEH tanı ve koruma katmanı. | Platform DXE bileşeni olarak yüklenir. |
| `firmware/input/keypad` | `.../Library/KeypadDeviceLib` | Ses ve güç tuşlarının GPIO eşlemesini sağlar. | UEFI keypad sürücüsüne platform kitaplığı olarak bağlanır. |
| `firmware/platform/memory-map` | `.../Library/MemoryMapLib` | Framebuffer, UEFI heap ve MMIO bölgelerini bildirir. | Platform DSC tarafından seçilen kütüphane örneğidir. |

Tablodaki `...`, `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg`
ön ekidir. Kesin kaynak-hedef eşlemelerinin makinece okunabilir tek kaynağı
[`MIRROR_MANIFEST.tsv`](MIRROR_MANIFEST.tsv) dosyasıdır.

## Windows UFS dosya haritası

| Dosya | Görevi |
|---|---|
| `b2lufs_storport.c` | `DriverEntry`, StorPort callback'leri, SRB/PnP yaşam döngüsü ve disk numaralandırması. |
| `b2lufs_transport.c` / `.h` | UFSHCI, UTP/UPIU, PRDT, DMA ve SCSI READ/WRITE taşıma katmanı. |
| `b2lufs_resources.c` | ACPI/StorPort kaynaklarını doğrular ve MMIO aralıklarını bağlar. |
| `b2lufs_hw.h` | Exynos/UFS register, bit alanı ve donanım sabitleri. |
| `b2lufs.inf.in` | ARM64 PnP sürücü paketi şablonu. |
| `build-b2lufs.ps1` | Windows üzerinde MSVC ve WDK kullanarak yalnız `b2lufs.sys` üretir. |
| `b2lgptdump.c` | WinPE'de PhysicalDrive GPT bölgelerini salt okunur çapraz doğrular. |
| `test_resources.c` | Kaynak bağlama mantığının host tarafı birim testi. |
| `README.md` | B2LUFS tasarımı, derleme ve çalışma davranışı. |
| `AI_DRIVER_BUILD_REFERENCE.md` | Kaynak/build zincirinin ayrıntılı teknik referansı. |

## Derleme sınırı

Windows UFS sürücüsünde yalnız SYS derlemesi PowerShell'de yapılır:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

Var olan SYS ile imzalama, katalog, GPT aracı ve WinPE paketleme WSL'dedir:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
```

Üretilen dosyalar `out/b2lufs/` altında kalır. `drivers/` içindeki kopyalar
derleme girdisi veya çıktı dizini değildir.

## İlerleme kaydı

Projenin tarihsel ve güncel kilometre taşları kökteki [`STATUS.md`](../STATUS.md)
dosyasına yazılır. Bir sürücünün iç tasarımı değiştiğinde kendi canlı
`README.md` dosyası; bileşen listesi veya yedek yolu değiştiğinde bu belge de
güncellenir.
