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

Yazılımsal reset bu platformda WinPE'den dönemediği için, aynı kayıt
`b2lcmd` görüldükten sonra WinPE RAM diskine de aktarılır:

```
%SystemRoot%\B2LUFS.TXT
```

`startnet.cmd` bunu otomatik tanının sonunda ekrana basar. Bu ikinci kanal
yalnız CMD'den fotoğraflanabilirlik içindir: dosya `X:` RAM diskindedir ve
UFS LUN'una yazı yapılmaz. Sıcak reset çalıştığında B2LGoster yine şu iki
satırı da gösterir:

```
UFS C=xxxxxxxx V=xxxxxxxx
UFS H=xxxxxxxx T=xxxxxxxx
```

`T` değerinin alt 10 biti UEFI'nin beklediği `166` değerini göstermelidir.
Bu kayıt HCE'yi değiştirmez, UIC/DME komutu göndermez, kesme durumunu
okumaz/temizlemez ve hiçbir UFS LUN'una erişmez.

Başka bir S9+ Windows get-up ekranında görülen “one-shot NOP” yaklaşımı,
bizim için bir uygulama tarifi değil bir **önkoşul listesi**dir. Kaynak kodu
ve donanım revizyonu bilinmediği için onun register yazma sırasını kopyalamak
güvenli değildir. Buna rağmen doğrulanacak noktalar doğru sıralamayı verir:

1. `CAP` içinden NUTRS/NTMRS ve adresleme kapasitesi; UTRD/UTMR ring boyutu
   tahmin edilmez.
2. `HCE` ve UFS owner/link durumu; firmware hâlâ sahibiyse denetleyici
   resetlenmez veya yeniden başlatılmaz.
3. StorPort'un verdiği fiziksel DMA belleğinin düşük-4-GB, bitişik ve
   sıfırlanmış olup olmadığı; bu varsayım yalnız `CAP` doğrulandıktan sonra
   kullanılabilir.
4. Bunlar kayda geçtikten sonra, **tek** yazmayan UTP NOP Out komutu ve
   yalnız tamamlanma kodunun okunması. UIC/DME/power-mode veya LUN I/O'su
   bu adımdan önce gelmez.

Mevcut Faz 0 yalnız ilk iki maddenin salt-okunur tarafını ölçer. NOP/DMA yolu
ayrı, imzalı test sürücüsü ve tamamlanma kaydı olmadan etkinleştirilmez.

`b2lufs_storport.c`, gerçek WDK `HwFindAdapter` çağrısını bu katmana bağlar.
Her `ACCESS_RANGE`, yalnız StorPort'un verdiği kaynak üzerinden
`StorPortGetDeviceBase` ile haritalanır; `RangeInMemory` değeri API'nin
`InIoSpace` parametresi için terslenir. Kısmi/uyuşmayan kaynak dizisi hemen
geri bırakılır. İlk bağlı testte `HwInitialize` **TRUE** döner; bu, yalnız
PnP'nin kaynak sözleşmesini "Started" olarak göstermesi içindir. Sürücü
HCI/PHY/UniPro register'ı okumaz ya da yazmaz, DMA ayırmaz, kesme kabul
etmez ve `HwStartIo` ile her isteği `INVALID_REQUEST` olarak tamamlar.
Dolayısıyla DiskPart'ın hâlâ dahili disk göstermemesi bu testin beklenen
sonucudur.

## Faz 0 WinPE kabul ölçütü

`startnet.cmd` önce sürücü kurulmadan önceki DiskPart sonucunu, ardından
`pnputil /add-driver`, yeniden tarama ve **kurulum sonrası** ikinci DiskPart
sonucunu gösterir. Fotoğrafta şu üç kanıt birlikte aranır:

1. `B2LUFS Faz 0: ozel miniportu PnP'ye ekle` altında INF'nin eklendiği ve
   yeniden taramanın tamamlandığı görülür.
2. `UFS0: CID, eslesen surucu ve stack` ile `SCSIAdapter` çıktılarında
   `ACPI\SMSN9820\0` için `Beyond2LTE Exynos 9820 UFS Host Controller`,
   `b2lufs.inf` ve `Started` yer alır. INF'nin eşleşmesi `ACPI\BYND9820`
   compatible ID'si üzerinden olur; instance ID'nin `SMSN9820` kalması
   normaldir.
3. İkinci DiskPart bloğu hâlâ `There are no fixed disks to show` ve
   `There are no volumes` göstermelidir. `X:` yalnız WinPE RAM diskidir.

Başarı ile bir iç disk görünmesi Faz 0 için **beklenmeyen** sonuçtur: bu
sürücü LUN keşfi veya SCSI I/O uygulamaz. Böyle bir durumda yazma yapmadan
ekranın fotoğrafını alıp testi durdururuz. Öte yandan `Code 10`/`FAILED_START`
altı ACPI kaynağının StorPort'a beklenen biçimde ulaşmadığını; imza veya
`pnputil` hatası ise sürücünün hiç çalıştırılmadığını gösterir. Her ikisi de
sonraki aşamada UFS register'larına dokunmadan teşhis edilir.

Bu testte DSDT `UFS0` aygıtı yalnız `ACPI\\BYND9820` compatible ID'sini
yayınlar ve altı MMIO penceresini yalnız b2lufs'a verir. Böylece inbox
`storufs` ile aynı HBA'ya iki sürücünün bağlanması mümkün değildir. WinPE
`startnet.cmd`, paket eklendiyse `pnputil /add-driver ... /install` çağrısını
otomatik yapar; fotoğrafta hedef PnP durumu `b2lufs.inf` + `Started`dır.

`tools_b2lufs.sh`, WSL'den Windows WDK'nin ARM64 `storport.lib` ve
`ntoskrnl.lib` kitaplıklarını kullanır. Kaynaklar WSL clang'iyle değil,
`build-b2lufs.ps1` üzerinden Visual Studio'nun `vcvarsamd64_arm64.bat` ile
kurulan **MSVC ARM64** ortamında `cl.exe`/`link.exe` ile derlenir. Betik üç
kaynak dosyasını önce Windows'un yerel `%TEMP%` dizinine kopyalar: böylece
`\\wsl.localhost\\...` yolu ne `cmd.exe`nin çalışma dizini olur ne de
derleyiciye aktarılır. Bu, `CMD.EXE was started with the above path...` UNC
hatasını kalıcı olarak önler.

Normal yol WSL'de tek komuttur:

```bash
bash tools_b2lufs.sh
```

Yalnız derlemeyi doğrudan Windows PowerShell'den çalıştırmak gerekirse:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

Bu ikinci yol yalnız `out\b2lufs\b2lufs.sys` üretir. Ardından WSL'de imza,
katalog ve WIM paketlemesi için:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
```

WDK ile **aynı build numaralı Windows SDK Headers** da kurulmuş olmalıdır:
`ntdef.h` SDK'nın `shared` başlığıdır; ADK bunu sağlamaz. Örneğin
`10.0.28000.0` WDK seçilmişse beklenen dosya
`Include/10.0.28000.0/shared/ntdef.h` olur. Araç, ayrıca `Inf2Cat.exe` ile
INF+SYS hash'lerini içeren `b2lufs.cat` üretir, onu ve SYS'yi b2lfb'nin var
olan test sertifikasıyla imzalar. PnP yüklemesi için yalnız gömülü SYS
imzasına güvenmek doğru değildir. `CopyFiles` bölümü için INF'de açık bir
`SourceDisksFiles` eşlemesi vardır; bu, Inf2Cat'in SYS dosyasını imzalanacak
paketin parçası olarak doğrulamasını sağlar.

Paketlenmiş WIM'de ayrıca `\Windows\B2LUFS\manifest.txt` bulunur. Bu dosya
INF/SYS/CAT SHA-256 değerlerini taşır. `cache_esp.sh --win --system`,
`out/b2lufs/b2lufs.sys` bulunduğunda bu manifestteki SYS hash'ini WIM'i geri
okuyarak doğrular; uyuşmazsa sparse `system.img` üretmez. WinPE'de
`startnet.cmd` manifestin içeriğini PnP kurulumundan hemen önce gösterir.
Dolayısıyla tekrar `B2LUFS paketi yok` görülürse çalışan ESP, bu doğrulama
kapısından geçmiş son artefakt değildir; UFS donanım sonucu olarak yorumlanmaz.

Ölçülen ilk HCI durumu: `CAP=1303FF1F`, `VER=00000300`, `HCE=1`,
`1US_TO_CNT=000000A6`. CAP alanında NUTRS `[4:0]` ve NUTMRS `[18:16]`
sırasıyla `31` ve `3` olduğundan ilk gerçek DMA turu 32 UTR + 4 UTMR yuvası
kullanacak; bit 24 de 64-bit adreslemeyi bildirir. Bu DMA turu henüz pakete
dahil değildir.

Yerel doğrulama:

```bash
clang -std=c11 -Wall -Wextra -Werror \
  windows/b2lufs/b2lufs_resources.c windows/b2lufs/test_resources.c \
  -o /tmp/b2lufs-resources-test && /tmp/b2lufs-resources-test
```

## Sonraki üç değişiklik

1. Ayrı ACPI `_CID`/INF'yi WDK ile **yalnız test WIM'ine** paketle ve altı
   `_CRS` kaynağının tam geldiğini fotoğraf logu ile doğrula. Bu değişiklik
   `PCI\\CC_010901`i kaldırarak genel `storufs` bağlamasını bitirdi.
2. CAP kanıtına göre UTR (32), UTMR (4) ve 64-bit DMA ringlerini ayır;
   önce yalnız fiziksel-adres/zeroing kaydını doğrula.
3. UEFI'den Exynos başlatması: VCC/PHY isolation, IO coherency,
   `1US_TO_CNT=166`, saat/refclk/clock-gating, CAL pre-link, standart
   `DME_LINKSTARTUP`, CAL post-link. UTRD/UPIU/SCSI bundan sonra gelir.

Bu sıra veri güvenliği içindir: eksik miniport hiçbir I/O isteği kabul etmez.
Test imza zinciri mevcut `windows/b2lfb/b2l-test.pem` üzerinden korunur;
üretim imzası bu get-up aşamasının kapsamı dışındadır.
