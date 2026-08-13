# B2LUFS — Exynos 9820 Windows UFS miniportu

Bu klasör `storufs.sys` filtresi değildir. Hedef, `ACPI\\SMSN9820\\0`
aygıtına bağlanan bir **fiziksel StorPort miniportu**dur.

## Faz 0: PnP kaynak sözleşmesi

`b2lufs_resources.c`, UEFI `PlatformUfsLib` ile stok Exynos sürücüsünün
kullandığı altı MMIO penceresini bir StorPort miniportunun kabul edeceği
şekilde tanımlar:

| Kaynak | Taban | Minimum boyut |
|---|---:|---:|
| UFSHCI | `0x13D60000` | `0x1000` |
| Samsung VS-HCI | `0x13D61100` | `0x200` |
| UniPro | `0x13D50000` | `0x8000` |
| PHY PMA | `0x13D64000` | `0x800` |
| IO coherency | `0x13C10700` | `0x4` |
| PMU UFS PHY | `0x15860724` | `0x4` |

Kaynaklar dizi sırasına güvenilmeden tabana göre eşleştirilir. Eksik,
yinelenen, memory olmayan veya kısa bir pencere güvenli biçimde reddedilir.
Bu katman fiziksel adres keşfetmez, MMIO haritalamaz ve **hiçbir register'a
yazmaz**.

## Mevcut WinPE turu: sahiplenmeden önce HCI anlık görüntüsü

`b2lfb.sys`, b2lufs sürücüsü değildir ve UFS'ye bağlanmaz. Ancak sonraki
miniport başlatmasının doğru yerde kurulabilmesi için, Windows'un en erken
boot-driver aşamasında HCI'nin gerçekten hangi durumda olduğunu kalıcı
diagnostik kayda alır. Ölçüm `CAP`, `VER`, `HCE` ve Samsung
`1US_TO_CNT` yazmaçlarını **yalnız okur**. Eşleme aynı `DriverEntry`
çağrısında geri bırakılır; PnP/storufs ile eşzamanlı veya kalıcı ikinci bir
MMIO eşlemesi yoktur.

Sonraki UEFI açılışında B2LGoster şu iki satırı gösterir:

```
UFS C=xxxxxxxx V=xxxxxxxx
UFS H=xxxxxxxx T=xxxxxxxx
```

`T` değerinin alt 10 biti UEFI'nin beklediği `166` değerini göstermelidir.
Bu kayıt HCE'yi değiştirmez, UIC/DME komutu göndermez, kesme durumunu
okumaz/temizlemez ve hiçbir UFS LUN'una erişmez.

`b2lufs_storport.c`, gerçek WDK `HwFindAdapter` çağrısını bu katmana bağlar.
Her `ACCESS_RANGE`, yalnız StorPort'un verdiği kaynak üzerinden
`StorPortGetDeviceBase` ile haritalanır; `RangeInMemory` değeri API'nin
`InIoSpace` parametresi için terslenir. Kısmi/uyuşmayan kaynak dizisi hemen
geri bırakılır. `HwInitialize` şu an bilerek `FALSE` döner ve eşlemeleri
serbest bırakır; INF de WIM'e yerleştirilmemiştir. Böylece iskelet ne
`storufs` ile yarışır ne de herhangi bir UFS komutu başlatır.

Yerel doğrulama:

```bash
clang -std=c11 -Wall -Wextra -Werror \
  windows/b2lufs/b2lufs_resources.c windows/b2lufs/test_resources.c \
  -o /tmp/b2lufs-resources-test && /tmp/b2lufs-resources-test
```

## Sonraki üç değişiklik

1. Yalnız b2lufs için ayrı ACPI `_CID`/INF ve yukarıdaki altı `_CRS` kaynağı.
   Jenerik `storufs` ile aynı aygıta birlikte bağlanmayacak.
2. Ayrı ACPI `_CID`/INF'yi WDK ile **yalnız test WIM'ine** paketle ve altı
   `_CRS` kaynağının tam geldiğini fotoğraf logu ile doğrula. Bu değişiklik
   `PCI\\CC_010901`i kaldırarak genel `storufs` bağlamasını bitirecek.
3. UEFI'den Exynos başlatması: VCC/PHY isolation, IO coherency,
   `1US_TO_CNT=166`, saat/refclk/clock-gating, CAL pre-link, standart
   `DME_LINKSTARTUP`, CAL post-link. UTRD/UPIU/SCSI bundan sonra gelir.

Bu sıra veri güvenliği içindir: eksik miniport hiçbir I/O isteği kabul etmez.
Test imza zinciri mevcut `windows/b2lfb/b2l-test.pem` üzerinden korunur;
üretim imzası bu get-up aşamasının kapsamı dışındadır.
