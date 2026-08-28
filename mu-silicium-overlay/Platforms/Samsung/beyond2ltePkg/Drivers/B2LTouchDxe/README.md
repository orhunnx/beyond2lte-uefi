# B2LTouchDxe

Galaxy S10+ beyond2lte üzerindeki Samsung `sec_ts` denetleyicisini UEFI
`EFI_ABSOLUTE_POINTER_PROTOCOL` olarak sunan deneysel sürücüdür.

Donanım tanımı stock device tree verilerine dayanır: HSI2C10 `0x104B0000`,
USI yapılandırma sayfası `0x10421000`, I2C adresi `0x48` ve 12 bit koordinat
aralığı `0..4095`. Bu adresler cihaz MMIO'sudur. Sürücü DRAM taramaz, platform
bellek haritasını değiştirmez ve kısıtlı RAM bölgelerine erişmez.

HSI2C aktarımı Android DT'deki `samsung,reset-before-trans` ve
`samsung,stop-after-trans` özelliklerini izler. sec_ts komut yazması ile veri
okuması ayrı mesajlardır ve ikisi de STOP ile tamamlanır.

Güvenlik sınırları:

- Firmware güncellemesi veya regülatör güç çevrimi yapmaz. DT'deki yalnız
  `tsp_io` (S2MPS19 LDO19M) ve `tsp_avdd` (LDO27M) hatlarını mevcut PMIC
  protokolüyle açar; voltaj ayarını korur ve UEFI'den çıkarken kapatmaz.
- Aktarım süresince yalnız `TRANSFER_DONE` kaynağını açar, sınırlı süreyle
  polling yapar ve her çıkış yolunda yeniden kapatır.
- Samsung başlangıç sırasına uygun olarak `0x30` ile touch/palm/wet işlevlerini
  açtıktan sonra `SENSE_ON` gönderir.
- Tek bir geçici I2C hatasından sonra polling'e devam eder; yalnız üç ardışık
  hata olursa o boot için tamamen kapatır.
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
