# B2LTouchDxe

Galaxy S10+ beyond2lte üzerindeki Samsung `sec_ts` denetleyicisini UEFI
`EFI_ABSOLUTE_POINTER_PROTOCOL` olarak sunan deneysel sürücüdür.

Donanım tanımı stock device tree verilerine dayanır: HSI2C10 `0x104B0000`,
USI yapılandırma sayfası `0x10421000`, I2C adresi `0x48` ve 12 bit koordinat
aralığı `0..4095`. Bu adresler cihaz MMIO'sudur. Sürücü DRAM taramaz, platform
bellek haritasını değiştirmez ve kısıtlı RAM bölgelerine erişmez.

Güvenlik sınırları:

- Firmware güncellemesi, regülatör güç çevrimi ve GPIO/IRQ kurulumu yapmaz.
- HSI2C kesmesini açmaz; durum bitini sınırlı süreyle polling yapar.
- İlk çalışma zamanı I2C hatasında polling'i o boot için tamamen kapatır.
- `WaitForInput` ve `GetState` içinde donanıma erişmez.
- `ExitBootServices` sırasında HSI2C10'u susturur.
- APRIORI listesinde değildir ve normal derlemede firmware'e gömülmez.

WSL'de deneysel derleme:

```bash
cd ~/beyond2lte-uefi-8549
bash yap.sh --touch --acpi --clean
```

Yalnız `out/uefi-boot.tar` dosyasını Odin AP alanından, Re-Partition kapalı
olarak flashla. BCD/WIM/SYSTEM/CACHE dokunmatik deneyi için değiştirilmez.

Bilinen çalışan profile dönmek için:

```bash
bash yap.sh --acpi --clean
```

Oluşan BOOT tar'ını aynı şekilde flashla. Bu sürücü yalnız UEFI aşamasında
çalışır; Windows dokunmatiği için ayrı bir Windows sürücüsü gerekir.
