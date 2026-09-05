# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## 5 Eylül 2026: MSVC C4013/C4312 — UCM connector cihaz bağlamı

WSL derlemesi IRQ kaynağını geçti; UCM kaynağında
`WdfObjectGetParentObject` çağrısı C4013 verdi. Bu geçerli bir KMDF API'si
değildi; ardından gelen C4312, derleyicinin varsaydığı `int` sonucunun
ARM64 cihaz handle'ına çevrilmesinden kaynaklanıyordu. Uyarı kapatılmadı
ve geçersiz fonksiyona elle prototip eklenmedi.

Connector artık `WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE` ile kendi
`B2L_MAX_UCM_CONTEXT` alanını oluşturur. Başarılı `UcmConnectorCreate`
sonrasında gerçek parent `WDFDEVICE` bu alana kaydedilir; veri rolü callback'i
cihaz bağlamına bu handle ile erişir. Bağlantı henüz kurulmamışsa callback
`STATUS_DEVICE_NOT_READY` döner. Connector, cihazın WDF child nesnesi olarak
kalır; ayrıca global cihaz işaretçisi veya USB olayı eklenmez.
Microsoft'un [UcmConnectorCreate sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ucmmanager/nf-ucmmanager-ucmconnectorcreate)
nesne bağlamını ve parent cihaz ömrünü destekler. Linux CC/PD/MUIC akışı
ve tamamlanmamış rol değişikliğinin reddedilmesi aynen korunur.

Önceki host testinde geçersiz API'yi taklit eden bir yardımcı vardı ve cihaz
ile connector aynı adresi kullanıyordu; bu nedenle test API hatasını
yakalamıyordu. Yardımcı kaldırıldı. Test artık ayrı connector nesnesi ve
ayrı ayrılan bağlam kullanıyor; gerçek cihaz erişimi, boş owner, başarısız
create ve mevcut UCM bildirim sıraları sınanıyor. UCM host testi ve MAX
kaynak kontrolleri geçti. Host testi WDK API doğrulamasının yerine geçmez;
gerçek MSVC link ve telefon sonucu henüz yok. Parent `.21`, MAX `.42`,
ABI ve WSL komutları değişmedi.

## 5 Eylül 2026: MSVC C4701 — VBUSDet durum okumalarının kontrol akışı

Yeni WSL çıktısında MAX ana kaynak ve HSI2C kaynağı derlendi; IRQ kaynağı
539. satırda `CcStatus` için C4701 verdi. Önceki iç içe `NT_SUCCESS`
kontrolleri mantıksal olarak iki okumayı da koruyordu, fakat MSVC bu akışta
koşullu CC atamasını kesin olarak izleyemedi. Değişkene sahte başlangıç
CC değeri vermek yerine her okuma ayrı erken hata çıkışına bağlandı:
`BC_STATUS (0x08) → hata kontrolü → CC_STATUS0 (0x0A) → hata kontrolü
→ DetachRecheck`. `/W4 /WX` ve Linux PASS2 VBUSDet koşulu korunur.

IRQ testine her iki byte okuması için ayrı hata enjeksiyonu eklendi.
İlk okuma başarısızsa ikinciye gidilmediği, başarısız okuma sonrasında
detach/snapshot dispatch yapılmadığı, tüketilen ham IRQ baytının ve gerçek
I/O hata kodunun kaydedildiği, bekleyen opcode'un hatayla iptal edildiği
ve IRQ'nun maskeli kaldığı doğrulandı. Mevcut connector hata bildirimi
korunur; bu bir attach veya yeni CC geçişi değildir. IRQ/queue/MUIC/PD/
SYSMSG/GRL/recovery host testi ve MAX kaynak kontrolleri geçti.
Tam Windows MSVC build ve fiziksel IRQ/USB sonucu burada doğrulanmadı.
Parent `.21`, MAX `.42`, ABI, tanı sürümleri ve WSL build yöntemi değişmedi.

## 5 Eylül 2026: MSVC C4310 — MUIC byte maskesi

Yeni WSL çıktısı WDK `10.0.28000.0` altında UCM ve URS `1.0`
kütüphanelerinin doğru çözüldüğünü doğruladı. Derleme MAX ana kaynak
dosyasının 1419 ve 1441. satırlarında `/WX` nedeniyle C4310 ile durdu.
`(UCHAR)~0x80U`, unsigned genişlikte oluşan `0xFFFFFF7F` sabitini byte'a
kesiyordu. İki kullanım da `Bc = (UCHAR)(Bc & 0x7FU)` olarak düzeltildi.
Sonuç 0..127 aralığındadır; yalnız çalışma kopyasının bit7'si temizlenir.

Linux VBADC koşulu ve 523K RID sınıflandırma davranışı değişmedi; ham
`MuicSample` korunur. Donanıma yazı, yapay VBUS/CC veya rol olayı eklenmedi.
`/W4 /WX` korunuyor. Mevcut IRQ/queue/MUIC/PD/SYSMSG/GRL/recovery yürütme
testi ve MAX kaynak kontrolleri geçti. Bunlar yerel host testleridir;
Windows MSVC derlemesinin tamamlandığı veya USB'nin çalıştığı anlamına gelmez.
Parent `.21`, MAX `.42`, ABI ve build komutları değişmedi.

## 5 Eylül 2026: WSL build — UCM/URS stub arama yolu düzeltmesi

Kullanıcının WDK `10.0.28000.0` çıktısı derleme başlamadan
`UcmCxStub.lib yok: ...\km\arm64` hatası verdi. Betik yalnız düz kernel
kütüphane dizinini kontrol ediyordu. Bu çıktı kütüphanenin bütün WDK
kurulumunda eksik olduğunu kanıtlamaz; alt dizinler henüz kontrol edilmemişti.

`build-b2lusb.ps1` artık seçilen WDK'nın ARM64 kökü altında önce
`ucm\1.0\UcmCxStub.lib` ve `urs\1.0\UrsCxStub.lib`, ardından aynı
kökün düz yerleşimini kontrol eder. Başlıklarla aynı 1.0 sürümü korunur;
başka WDK, mimari veya class-extension sürümü aranmaz. Linker'a bulunan
kütüphanenin tırnaklı tam yolu verilir ve bu yol build çıktısında gösterilir.
Dosya bulunamazsa hata iki kontrol yolunu da yazar. Gerçek UCM/URS
kütüphaneleri zorunludur; bu değişiklik USB çalışma zamanı akışını değiştirmez.
Parent `.21`, MAX `.42`, USB ABI 8 ve tanı sürümleri aynı kalır.

Mac üzerinde WDK derlemesi yapılmadı; Windows kütüphane yerleşimi ve gerçek
link sonucu burada doğrulanamadı. WSL komutları değişmedi:
`bash yap.sh --touch --usb-test --clean`, başarılı olunca
`bash cache_esp.sh --win --system --baseline`.

## 5 Eylül 2026: parent `.21` — gerçek FSYS0A güç/saat yaşam döngüsü

Taban `d3303dd`; parent/host/function `0.1.0.21`, MAX `0.1.0.42`.
USB durum IOCTL ABI'si **8**, USB FOTO **v4**, MAX FOTO **v15**.
MAX olay ABI'si değişmedi. Kaynak işi yeni flaş beklemeden yürütüldü;
Mac'te WDK/UEFI/WIM build yapılmadı. Bu bölüm aşağıdaki `.20` bölümündeki
“parent boot'tan kalan erişilebilir saatlere dayanıyor” durumunu günceller.

### Kaynak zinciri ve önemli ayrımlar

Sabit cihaz kaynağı yine
`corsicanu/android_kernel_samsung_universal9820@55f042627bb7030c93550faafa117674d32b21d1`.
Stok birleşik DT'nin USB düğümünde `aclk=GATE_USB31DRD_SLV_LINK`,
`sclk=USB31DRD`, güç alanı `pd-fsys0a@15862200`, `power-down-ok=3`
bulunur. PHY aynı SLV_LINK saatine ikinci referansı ve sabit `fin_pll`
26 MHz saatini kullanır; `phyclk_mux=none` olduğu için ek mux listesi yoktur.

- [dwc3-exynos.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/usb/dwc3/dwc3-exynos.c#L155):
  runtime resume `aclk`, sonra `sclk`; runtime suspend aynı liste sırasıyla
  bırakır. PHY'nin ikinci SLV_LINK referansı, root referansı dururken ayrı
  bir fiziksel gate yazısı üretmez.
- [clk-exynos9820.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/clk/samsung/clk-exynos9820.c#L187)
  ve [composite.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/clk/samsung/composite.c#L1368):
  CCF parent zinciri `FSYS0_BUS_USER → FSYS0A_BUS_USER → SLV_LINK`.
  SLV_LINK, `VCLK_GATE` taşır fakat `VCLK_QCH_DIS` taşımaz. Bu durumda
  `cal_vclk_enable/disable` gate için register yazmadan döner. USB_REF,
  SLV_CTRL, APB, PCS QCH'lerinin de tabloda bulunması, hepsine keyfi enable
  yazılması gerektiği anlamına gelmez. `HWACG_VCLK` makrosu ek bir QCH_EN
  bayrağı da üretmez.
- [ra.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/soc/samsung/cal-if/ra.c#L195)
  ve [cmucal-sfr.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/soc/samsung/cal-if/exynos9820/cmucal-sfr.c#L3178):
  USER mux enable/disable `MUX_SEL` bit **4**'ü değiştirir, `BUSY` bit **7**'nin
  temizlenmesini 100 × 1 us sınırıyla bekler. TOP PLL/rate/DVFS ayarı bunun
  parçası değildir; geniş bir clock register dökümü yeniden oynatılmaz.
- [exynos-pd.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/soc/samsung/exynos-pd.c)
  → [pmucal_local.c](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/soc/samsung/cal-if/pmucal_local.c)
  → [FSYS0A CAL tablosu](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/soc/samsung/cal-if/exynos9820/flexpmu_cal_local_exynos9820.h#L1064):
  `15862200` bit0 konfigürasyonu, `15862204` bit0 gerçek durumunu bekleme,
  önceki kapanışta saklanmış kayıtların geri yüklenmesi. Bu DT düğümünde
  `need_smc` ve `devfreq-sync-voltage` yoktur. PMU status bitine yazılmaz.
  `pmucal_rae_wait` 2001 başarısız örnekte timeout verir; koşul registerının
  varlığı `PMUCAL_SAVE_RESTORE` işlemini `COND_SAVE_RESTORE` yapmaz.

### Uygulanan başlangıç ve kapanış

Yeni `b2lusb_power.c`, parent RoleLock altında gerçek USB tüketicisi için
çalışır. PrepareHardware yalnız verilen PnP kaynaklarını eşler; hiçbir
DWC3/PHY okuması yapmaz. Rol hazırlığındaki ilk GSNPSID okumasından önce:

1. PMU'dan gerçek alan durumu okunur. Kapalıysa CONFIG bit0 set edilir ve
   STATUS bit0 beklenir. Daha önceki off isteği timeout verdiyse önce on
   isteği ile o istek iptal edilir; sonra snapshot geri yüklenir.
2. Yalnız bu sürücünün gerçekten sakladığı 13 register varsa özgün sırada
   geri yüklenir. İlk açılışa varsayımsal clock/QCH değerleri konmaz.
3. `13000120` FSYS0_BUS_USER, `10a00100` FSYS0A_BUS_USER,
   `10a00120` USB31DRD_USER sırayla seçilir. Her mux için BUSY ve seçilmiş
   bit read-back'i doğrulanır. Başarısız saat önceki parent referanslarını
   Linux enable rollback sırasıyla bırakır; DWC3/PHY'ye ilerlenmez.
4. Bundan sonra `.20`deki denetleyici/PHY/reset akışı başlayabilir.
   HALTED olmayan xHCI veya kimliği bilinmeyen core'da saatler zorla
   kapatılmaz. Başarılı rol ile yalnız güç hazırlığı ayrı sonuçlardır.

Kapanışta gerçek rol çocuğu durduktan ve PHY exit tamamlandıktan sonra
[dwc3_otg_check_bus_act](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/usb/dwc3/otg.c#L253)
eklenmiştir: `GDBGLSPMUX_HST c170` içine `3f<<16`, ana PHY `+0c` üzerinden
22 bit outstanding read, `+10` üzerinden 22 bit outstanding write sayıları
okunur. Düşük 10 kanal-durumu biti işlem sayısı değildir. 100 örnek/1 ms
bekleme kullanılır. Windows pasif thread beklemesi zamanlama nedeniyle
1 ms'den uzun sürebilir. Linux timeout'u loglayıp devam eder; Windows bu
hatayı D-state başarısı yapmaz, saatleri açık bırakır ve hata döndürür.
Bu açık hata davranışı farkıdır; işlem sayısı yazılımla sıfırlanmaz.

Sonra root aclk bırakılır: önce FSYS0A_BUS, sonra CCF parent FSYS0_BUS;
sonra sclk bırakılır. PHY'nin sabit OSCCLK ve ikinci SLV_LINK referansının
ayrı fiziksel yazısı yoktur. `power-down-ok=USB`, **HOST sahipliğini**
izleyen `otg_is_connect()` dalıdır; genel CC attach değildir. HWC1
`CONFIG_SND_EXYNOS_USB_AUDIO=y` HOST-stop'ta bu sahipliği temizler.
Windows'ta olağan HOST D0 çıkışı RequestedRole'ü koruyarak alanı açık tutar;
gerçek URS HOST-stop veya son aygıt kaldırılması bu tutmayı bitirir.
Function aynı HOST vetosunu üretmez.

Son tüketicinin kapanışında CAL tablosundaki sırayla üç mux, sekiz QCH,
CMU controller option ve FSYS0 DRCG saklanır. Aradaki sekiz DBG_NFO okuması
yalnız tanıdır; geri yazılmaz. CONFIG bit0 clear → gerçek STATUS=0 bekleme
uygulanır. Off timeout, snapshot'ı kaybetmez. Sonraki on gerçek tabloyu geri
yükler. Yeni CC/PD/ID-pin olayı veya IRQ üretilmez. Özgün GSIV51/114/273
sahipliği değişmemiştir.

### PnP kaynakları ve mevcut kapsam

| Yeni parent kaynağı | Kullanımı |
|---|---|
| `15862200 / 8` | FSYS0A CONFIG/STATUS |
| `10a00000 / 8000` | Özgün CMU pencere boyu; USER mux ve CAL save/restore |
| `13000120 / 4` | FSYS0_BUS_USER CCF parent |
| `13020104 / 4` | CAL FSYS0_BUS_COMPONENT_DRCG_EN save/restore |

Mevcut üç kaynağa bunlar eklenir; parent tam yedi memory kaynağı ister.
Kaynak eksik, uzunluk/adres yanlış veya duplicate ise başlatılmaz. URS
çocuklarına yalnız DWC3 penceresi geçer; clock/domain kaynakları parent'ta
kalır. Aktif ACPI topolojisinde bu clock kaynaklarının tek Windows sahibi
USB parent'tır. Gelecekte DP/PCIe tüketicileri eklendiğinde ortak FSYS0
parent referansları merkezi sağlayıcı üzerinden paylaşılmalıdır; ikinci
sürücü aynı registerlara bağımsız yazamaz. Şu an var olmayan bir DP
sahibi, sahte referans veya manager READY kaydı eklenmedi.

**Kapsam sınırı:** Bu gerçek FSYS0A domain/USB clock consumer işlemlerini
uygular; bütün SoC clock/regulator sağlayıcısının tamamlandığı anlamına
gelmez. TOP PLL/rate/DVFS ve S2MPS19 ACPM regulator ömrü ayrıca açıktır.
USB/DP combo ortak sahipliği, USB3 isolation/GPIO/PMA/PCS/lane-lock zinciri,
DWC3 kalan phy_setup/global ayarları, SOURCE Rp ve OS PD tüketicileri de
aynı açık hedefin parçalarıdır. `USB2_ONLY` hâlâ bildirilmektedir.

### Tanı ve doğrulama

USB FOTO v4 artık güç/saat aşaması, ayrı NT sonucu, tamamlanmış clock
açılış durumu, son okuma maskesi ve gerçek AXI sayaçlarını gösterir.
`asama=6, NT=0, saat=1` yalnız clock hazırlığı; `11` alanın kapandığı,
`8` HOST nedeniyle alanın tutulduğu, `12` AXI kontrol aşamasıdır. `okuma`
maskesi `1=PD, 2=FSYS0, 4=aclk, 8=sclk`; olmayan bitin sıfır alanı ölçüm
sayılmaz. Değerler **son geçişten saklanır**, tek canlı/atomik snapshot
iddiası yoktur. `n=0` AXI okunmadı demektir. `status` ve foto komutları
kapalı alanı uyandırmaz. Başarısız clock başlangıcında register erişimi yoktur.

Üretim güç ve PHY C koduyla host fixture testleri; PMU/mux timeout, register
sırası, BUSY, AXI timeout/idle, HOST retention, off timeout sonrası on,
13 kayıt restore ve kapalı durumda tanının MMIO yapmaması doğrulandı.
Yeni PnP fixture tüm yedi eşlemeyi, her adımda allocation hatasını,
eksik/duplicate/yanlış adres/uzunlukları ve unwind'ı yürüttü. ASan/UBSan
geçti. USB foto başarı/hata ve kalabalık PnP senaryoları 64 sütun / en fazla
39 satır sınırında test edildi. MAX IRQ/lifecycle/connection/UCM ve Windows/
UEFI kaynak kontrolleri geçti. Bu testler gerçek MMIO/GIC teslimi veya USB
transferi değildir; WDK, ACPI derlemesi ve cihaz sonucu bu Mac'te üretilmedi.

WSL'de aynı üretim yolu kullanılacak; yeni ACPI kaynakları nedeniyle UEFI
ve WIM birlikte yenilenir, eski UEFI/yeni parent karışımı başlatılamaz:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Bir sonraki gerçek cihaz ölçümünde saat aşamasının `6` olması, güç hatasının
yeri ve AXI kapanışının gerçek sonucu görülebilecek. USB çalıştı ölçütü
yine gerçek port bağlantısı, enumeration ve veri transferidir. Kaynak
çalışması bu ölçümü beklemeksizin kalan Linux adımlarında devam eder.


## 5 Eylül 2026: parent `.20` — özgün DWC3 reseti ve ana PHY sırası

Parent/host/function `0.1.0.20`, MAX `0.1.0.42`, IOCTL ABI `7`, MAX FOTO
v15. Taban `8095fcf`. Bu değişiklik, USB3 araştırmasında bulunan **ana PHY
ve DWC3 reset sırası** farklarını kapatır. Tam SuperSpeed hazırlığı henüz
uygulanmış değildir; aşağıdaki açık adımlar başarı sayılmaz. Kaynak işi
flaş beklemeden sürdü; Mac'te WDK/UEFI/WIM build yapılmadı.

### Doğru cihaz ve kaynak dalı

Depodaki stok birleşik [beyond2lte DT](../mu-silicium-overlay/Resources/DTBs/beyond2lte.dts)
`dr_mode=otg`, `maximum-speed=super-speed-plus`, ana PHY `0x301`, combo
PHY `0x400`, `has_other_phy=0`, `has_combo_phy=1`, `phyclk_mux=none`
kullanır. HWC1 boot IKCONFIG'i `CONFIG_USB_DWC3_DUAL_ROLE=y`, HOST-only ve
`CONFIG_SOC_EXYNOS9820_EVT0` kapalı olduğunu ayrıca doğrular. Anlık HOST
rolü, Linux'un derleme/DT düzeyindeki HOST-only istisnası değildir.

Sabit kaynak `corsicanu/android_kernel_samsung_universal9820` commit'i
`55f042627bb7030c93550faafa117674d32b21d1` içinde:

- [dwc3_core_soft_reset](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/usb/dwc3/core.c#L400)
  LINK reseti, PIPE/UTMI SUSPHY temizliği, iki PHY'nin power/init sırası ve
  kendiliğinden temizlenen `DCTL.CSFTRST` kullanır. OTG yapılandırmasında
  bu device reseti HOST başlangıcında da atlanmaz. DWC_usb31 için bit
  temizlendikten sonra PHY alanına erişmeden önce 50 ms beklenir.
- [exynos_usbdrd_pipe3_init](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/phy/samsung/phy-exynos-usbdrd.c#L1157)
  gerçek GPIO yönünü okur; **Gen2** PMA/PCS/PLL/CDR yolunu çağırır. Aynı
  isimli eski `phy-exynos-usbdp.c` bu çağrı zincirinin yerine kullanılamaz.
- [USB3P1 ana PHY CAL](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/phy/samsung/phy-exynos-usb3p1.c#L369)
  LINK darbesini ve UTMI hazırlığını tanımlar. Doğrulanan dosyanın SHA256'sı
  `bc4fe792bfaa398b7ae626d85bcce470b5f439fb9e2be9b340025cdd86c70efa`.
- [Gen2 CAL](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/phy/samsung/phy-exynos-usbdp-gen2.c#L479)
  SHA256 `473a657fd0a0706c3b62cc8140059d28225184d8ddfacbed24ce80aac635954f`.
  Her iki CAL dosyası daha önceki d2s ortak kaynak kopyasıyla byte düzeyinde
  aynı çıktı. Farklı cihaz dosya adından akış farkı varsayılmadı; beyond
  driver çağrıları ve stok DT/IKCONFIG belirleyici kabul edildi.

### Düzeltilen Windows sırası

Önceki parent PHY'yi açıp host pulldown/tune değerlerini uyguladıktan sonra
SUSPHY temizliyor ve `GCTL.CORESOFTRESET` bitini 100 ms tutuyordu. Bu, bu
Samsung OTG yolunun reset işlemi değildir. Yeni sırada:

1. URS yetkisiyle önce eski function denetleyicisinin durması veya xHCI'nin
   halted olması aranır. Çalışan xHCI'ye reset/PHY yazısı yapılmaz.
2. `CLKRST.LINK_SW_RST` set → 10 us → clear uygulanır. Sonra önce
   `GUSB3PIPECTL.SUSPHY`, ardından `GUSB2PHYCFG.SUSPHY` temizlenir.
   Bu artık PHY POR ve güç açılışından öncedir.
3. Mevcut ana PHY power/POR akışı yürür. Stok provider'ın başlangıçta
   sıfırlanmış tune_param değerleri, sonraki açılışlarda ise son seçilmiş
   sütunu kullanması korunur. Bu yazılım başlangıç durumu bir donanım
   ölçümü değildir. Host/device sütunu erken POR aşamasına taşınmaz.
4. Stok `rewa_ready` tamamlanır: HS ReWA kapatılır; DPDM monitor seçimi ve
   digital bypass ayarı, HSTK=`1`, REFTO=`ff00`, altı olayın maskesi uygulanır.
   Bu retention'ı açmak veya wake IRQ teslim etmek değildir.
5. Stok UTMI-init sonundaki PIPE hazırlığı eklenir: LINK `PIPE3_FORCE_EN`
   ve RX electrical-idle, PHY_STATUS temizliği; ana pencere `+48`
   `PMA_LOW_PWRN` hold. Bunlar Linux'un gerçek geçici PHY hazırlık
   yazılarıdır; CC/PD bağlantısı veya IRQ üretilmez. **Tam PIPE3 init'in
   sonraki adımları eksiktir; bu hold SuperSpeed-ready sayılmaz.**
6. Genel GCTL reseti kaldırıldı. `DCTL.CSFTRST` yazılır, en fazla 1000 kez
   1 us aralıkla donanımın temizlemesi beklenir. Yazılımla clear edilmez.
   Gerçek self-clear sonrasında 50 ms beklenir. Timeout rol başarısı olmaz;
   PHY kapatılır ve hata döner.
7. Sonra gerçek rolün HS tune sütunu uygulanır; HOST için DP/DM pulldown
   ve VBUS-pad ayarı, ardından GCTL port rolü seçilir. Bu sıra stok
   core_init → phy_tune → OTG host ayarı sırasını izler. xHCI'nin kendi
   resetini ve ring/doorbell işlemlerini inbox USBXHCI yapar.

`has_other_phy=0` ve stok provider'ın `dual_phy=0` olduğu bu cihazda
koşulsuz DUALPHYSEL yazısı kaldırıldı. Stok tabloda bulunan `utmi_clk`
parametresine yeni register etkisi uydurulmadı: bu USB3P1 tune fonksiyonunda
onu uygulayan bir dal yoktur. Beş etkin HS tune alanı dışındaki bitler
korunur. Seçilmiş tune değeri detach/power-off sırasında kaybolmaz; sonraki
POR önce o provider durumunu, reset sonrasında yeni rolün sütununu kullanır.

### USB3'ün kalan gerçek donanım zinciri

Bu tablo tam akışın yerine USB2 başarısı konmasını önlemek içindir.
Parent `.20` hâlâ `USB2_ONLY` bildirir; `U3_DISABLE` sınırlaması kaldırılmış
ve USB3 adımları tamamlanmış **değildir**.

| Linux adımı | Windows'ta mevcut durum / kalan iş |
|---|---|
| phy_conn → LDO10M/11M/12M → 1 ms | UEFI gerçek boot-on raylarını açıyor. Windows native ACPM/regulator yaşam döngüsü henüz yok. |
| FSYS0A runtime PM → aclk/sclk enable | Parent, boot'tan kalan erişilebilir saat durumuna dayanıyor. Özgün clock/domain sağlayıcısı eksik. |
| LINK reset → PIPE/UTMI SUSPHY clear | Parent `.20` içinde doğru sıra uygulandı. |
| USB3 PMU `15860704` bit0 → USB2 PMU `1586072c` bit0 | Yalnız USB2 izolasyonu yönetiliyor. DT'deki pmu_mask=0 bit numarasıdır. |
| UTMI POR/tune/ReWA/PIPE hold | Ana pencere adımları yukarıdaki kapsamda uygulanıyor. |
| GPG0-4 → used_phy_port | Stok DT gerçek yön GPIO'sunu gösteriyor. Windows'ta sahiplik/okuma ve SS tüketicisi henüz yok; sabit yön veya CC pininden tahmin kullanılmayacak. |
| g2_pma_ready | Ana `+48` PMA reset/refclock/low-power sırası, PIPE override bırakılması ve APB reset release eksik. |
| g2_enable → analog tune → PCS | Doğru Gen2 kaynak doğrulandı; henüz Windows'a taşınmadı. |
| INIT reset release → CMN/TRSV release → PLL lock | Gerçek lock okumaları/timeout ve hata temizliği eksik. Stok dış döngü 100, iç kontrol 1000 × 1 us. |
| run_lane → seçilmiş RX CDR lock → LINK_PCLK_SEL → 3 ms | GPIO'ya bağlı lane/lock seçimi eksik. Kilit olmadan başarı üretilemez. |
| DCTL self-clear → 50 ms → phy_setup/role tune/global setup | Device reset ve HS role-tune sırası düzeltildi. Tam SS phy_setup/quirk/global ayarları ayrıca eşleştirilecek. |
| USB/DP ortak sahipliği ve son tüketicide kapanış | Stok combo_phy_control sahipliği, DP tüketicisi ve native clock/regulator kapanışı henüz yok. |

Stok `phy_exynos_usbdp_g2_disable` gövdesi boştur. Bu, bütün USB3 kapanışının
no-op olduğu anlamına gelmez: UTMI exit, PMU isolation, clock ve son
USB/DP sahibinin regulator sırası ayrıca vardır. Windows'ta boş bir
başarı fonksiyonu eklenerek bu zincir kapatılmadı.

### Tanı adları ve doğrulama

Eski UEFI ekranları, MemoryMap açıklamaları ve README'de combo pencere
adları tersti. Kaynak eşlemesi şöyledir:

| Adres / DT uzunluğu | Gen2 anlamı |
|---|---|
| `10b00000 / 200` | Ana DRD wrapper; `+48` combo PMA kontrolü |
| `10ae0000 / 2800` | Analog PMA; `usbphy_sub_info.regs_base` |
| `10af0000 / 800` | PCS; `regs_base_2nd`, g2_set_pcs burada çalışır |

UEFI ekran adları ve MemoryMap etiketleri bu yönde düzeltildi. Eski
R4/R5 abort ölçümleri `10ae0000+0c/+00`, R6 ölçümü `10af0000+00` olarak
aynı adreslerde kalır. Yeni combo MMIO okuması veya yeni ölçüm eklenmedi.

Windows durumunda eski belirsiz `resets` etiketi `devrst` oldu: yalnız
başarılı DCTL self-clear + 50 ms sayısıdır. Rol seçimi daha sonra hata
alabilir; `devrst>0` USB enumerate, PHY lock veya transfer kanıtı değildir.
IOCTL yapısının yerleşimi değişmedi; MAX FOTO sayfaları v15 kalır.

Yeni `tests/test_windows_usb_phy.py`, gerçek üretim C dosyasını adres ve
zaman sırası izleyen host fixture'ında yürütür. Host/function başlangıcı,
LINK/SUSPHY/POR/ReWA/PIPE/DCTL/tune sırası, ilk ve sonraki tune durumu,
1000'inci okumada self-clear, hiç temizlenmeyen reset, çalışır xHCI,
duramayan function, SUSPHY/PMU/SIDDQ hataları ve rol timeout temizliği
geçti. Kodun GCTL global reseti, xHCI ring/command yazısı, DUALPHYSEL veya
harici combo erişimi yapmadığı kontrol edildi. ASan/UBSan da geçti.
Mevcut MAX IRQ, foto, lifecycle, connection, UCM, URS/target-INF ve
Windows/UEFI kaynak kontrolleri geçti. Bunlar WDK build, gerçek MMIO bus
cevabı, GIC teslimi veya USB transfer testi değildir.

WSL yöntemi değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Kaynak işi yeni flaş beklemez. Yukarıdaki native güç/saat/USB3 adımları,
SOURCE Rp, OS DR/PR swap/RDO, manager/battery/AFC/VDM/RAM tüketicileri ve
uçtan uca function/transfer hedefi açık kalır.

## 5 Eylül 2026: `.42` — gerçek MUIC tamamlanmasından UCM bildirimine

MAX `0.1.0.42`, parent/host/function `0.1.0.19`, ABI `7`, FOTO v15.
`.41`de kaydedilen MUIC/OS sıralama açığının **MAX D+/D− anahtarı** kısmı
kapatıldı. USB3 yönlendirme/PHY, source Rp ve diğer açık tüketiciler bu
değişiklikle tamamlanmış sayılmaz. Kaynak çalışması flaş beklemeden sürdü;
Mac üzerinde WDK/UEFI/WIM build veya yeni telefon testi yapılmadı.

### Linux üreticisi ile Windows tüketicisinin birleştiği nokta

Sabit beyond2lte commit'inin
[notify_dr_status](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_cc.c#L189)
yolu gerçek DATAROLE değişiminde MUIC bildirimini USB bildiriminden önce
üretir. [max77705_switch_path / com_to_usb_ap](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/muic/max77705-muic.c#L314)
`CONTROL1_WRITE=06` ile USB_AP `09` isteğini kuyruğa alır; Linux tarafında
bu fonksiyon firmware yanıtını bekleyerek dönmez. Bu asenkronluk, Windows'a
başarılı mux hazırlığı bildirildiği anlamına gelmez.

Microsoft'un
[UCM attach sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system#2-report-the-partner-connector-attach-event)
OS/URS rol başlangıcından önce mux'un doğru kurulmasını ister. Önceki
Windows kodu gerçek DATAROLE ardından MUIC çiftini başlatıyor, fakat yanıt
beklenirken UCM attach veya veri rolü değişimini gönderebiliyordu.

`.42`, mevcut gerçek `06 WRITE → 05 READ` ve APCMD tüketim yolunu OS
bildirim kuyruğuna bağlar. Yeni donanım komutu, ek mailbox okuması, sabit
gecikme, polling, sahte CC/PD olayı veya elle HOST seçimi eklenmedi. Bu,
Linux'un senkron bir READ bariyeri kullandığı iddiası değildir; Windows
DDI çağrısının mevcut gerçek donanım tamamlanmasına bağlanmasıdır.

### Hazırlık ve bildirim ömrü

Yeni attach veya mevcut bağlantıdaki veri rolü değişimi için birlikte
aranan kanıtlar: güncel istek yolu `09`, MUIC çift sonucu tam
`STATUS_SUCCESS`, geçerli güncel readback ve okunan yol `09`. Su ve MFD
hatası hazırlık başarısı sayılmaz. `STATUS_PENDING=103` ve pozitif
`STATUS_TIMEOUT=102`, NT_SUCCESS makrosuyla yanlışlıkla hazır kabul edilmez.
OPEN, eski `09` örneği veya WRITE hatasını izleyen doğru READ yeterli değildir.

Hazır olmayan USB bildirimi kuyruğun başında kalır. Worker bekleme veya
hata durumunu yayımlayıp döner; kendini tekrar kuyruğa alarak dönmez.
Yalnız mevcut MUIC çiftinin son isteğinin emekli olması veya yeni gerçek
bildirim işi uyandırır. Eski MUIC neslinin readback'i yeni nesli doğrulamaz
ve uyandırma üretmez. WRITE yanıtı tek başına USB bildirimini ilerletmez.
Çift hata ile tamamlanırsa bekleyen OS bildirimi başarılı gösterilmez;
sonraki gerçek path işlemi veya detach kendi akışıyla ele alınır.

USB bildirim kayıtları artık bağlantı nesli taşır. Yeni bağlantı, önceki
bağlantının gecikmiş hazırlanmasını devralamaz. Kuyruktaki attach kaydı
son gerçek USB bildirimiyle aynı bağlantı/veri/güç/akım durumuna ait değilse
OS'de eski stack başlatılmadan elenir. **Detach kayıtları elenmez.**
Örneğin henüz hazırlanmamış A → detach → B dizisinde A OS'ye açılmaz;
varsa eski OS bağlantısı detach edilir ve B kendi hazırlığıyla ilerler.
Bu eleme fiziksel IRQ/frame günlüğünü veya Linux nested handler sırasını
değiştirmez; hazırlığı tamamlanmamış OS işinin iptalidir.

Mevcut OS bağlantısında veri rolü değişmiyorsa bağımsız akım/güç
bildirimleri MUIC tekrar yazımını beklemez; veri stack'i yeniden
başlatılmıyordur. OS'nin aynı rol isteğine verilen callback de hazır yol
yokken başarı bildirmez. Farklı rol için henüz uygulanmamış PD swap
başarısı üretilmez. Bir UCM attach çağrısı hata aldıysa sonraki MUIC
uyandırması o başarısız çağrıyı gizlice tekrarlamaz.

### Gerçek KMDF eşzamanlılık açığı

[WdfWorkItemEnqueue sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemenqueue)
aynı work item yeniden kuyruğa alındığında ikinci callback'in birincisi
bitmeden çalışabileceğini belirtir. `AutomaticSerialization=FALSE`
kullanılan mevcut UCM worker'ı yalnız kuyruk erişiminde kilit tutuyordu;
kilitsiz UCM çağrısı sürerken başka worker OS durumunu eski okuyabilirdi.
Attach sürerken ikinci worker detach'i OS henüz attached değil diye
atladıktan sonra ilk worker attached yazarsa USB bağlantısı açık kalırdı.

Yeni `ConnectorWorkerActive`, MfdLock altında bütün drain'in sahibini
belirler; kilitsiz UCM çağrısı sırasında da sahiplik korunur. Üreticiler
kuyruğa yazabilir; çakışan callback işi tüketmeden döner, asıl sahip bir
sonraki adımda işi görür. Hazırlık beklemesi, boş kuyruk, overflow ve
64 kayıtlık tur sonu sahipliği aynı kilitle bırakır. Tur devamı aynı
kilit altında planlanır. UCM çağrısı sırasında MfdLock tutulmaz; kilidi
OS çağrısının etrafına genişleterek yeni bir deadlock oluşturulmadı.
PnP stop üreticiyi kapatır ve worker'ları flush eder; bekleyen MUIC
bildirimi için donanım cevabı bekleyerek kapanışı asılı bırakmaz.

### Tanı ve doğrulama

FOTO v15'in UCM sayfası **31 satırdır**. Yeni alanlar:

| Alan | Anlamı |
|---|---|
| PATH_WAIT | Worker'ın baştaki USB bildirimini MUIC hazırlığı nedeniyle tutması |
| PATH_NT | Son hazırlık kontrolünün sonucu; gerçek UCM çağrısının sonucu UCM_NT'dir |
| PATH_WAITS | Yeni bekleme veya değişmiş bekleme sonucu sayısı; IRQ sayısı değildir |
| STALE | OS'ye gönderilmeden geçersizleşen attach/rol snapshot sayısı; kayıp fiziksel IRQ değildir |

`PATH_NT=0` son kontrolün başarılı olduğunu gösterir; tek başına güncel
bağlantı, USB3 hazırlığı veya transfer kanıtı değildir. Detach sonrası
son kontrol sonucu korunabilir, `PATH_WAIT` kapanır. Startnet aynı dört
alanı içerir. Registry 236 alanı ardışık yayımlar; REG_BINARY IRQ günlüğü
ve IOCTL ABI değişmedi. D0 yeni nesli ve bekleme sayaçlarını sıfırlar.

Gerçek sürücü C işlevleriyle host kontrolleri geçti: pending/error/OPEN ve
eski readback; gerçek son MUIC yanıtından uyandırma; WRITE hata + READ
başarı; aynı rol ile yeniden takma; bekleme sırasında DR değişimi ve
detach; bağlı portta bağımsız güç/akım; başarısız UCM çağrısının tekrar
edilmemesi; kapanış ve D0. UCM attach çağrısının içinden ikinci worker
çalıştırılan test, detach'in kaybolmadığını ve kilit tutulmadığını doğrular.
IRQ ve UCM fixture'ları ASan/UBSan ile de geçti. Foto ölçüleri, CC/PD,
lifecycle ve Windows USB/MAX kaynak kontrolleri geçti. Bunlar gerçek KMDF
scheduling, GIC teslimi veya telefon USB transferi ölçümü değildir.

WSL build yöntemi değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

İleride cihaz çıktısında USB_NOTIFY sonrasında geçici PATH_WAIT/103,
güncel USB_AP readback'i sonrasında PATH_WAIT=0 ve gerçek UCM/URS sonucu
ayrı izlenebilir. Negatif PATH_NT varsa hangi MUIC isteğinin başarısız
olduğu incelenir; başarılı OS bağlantısı yazılmaz. Kaynak çalışması bu
ölçümü beklemez. Full USB3 orientation/clock/reset/PHY, SOURCE Rp, manager
charger/UART, OS DR/PR swap ve RDO, battery/CHGINSEL, AFC/VDM/READY/RAM,
function sınıfı ve uçtan uca transfer hedefi hâlâ açıktır.

## 5 Eylül 2026: `.41` — gerçek RID değişimi ve USB bildirim ömrü

MAX `0.1.0.41`, parent/host/function `0.1.0.19`, ABI `7`, FOTO v14.
Kaynak çalışması flaş beklenmeden sürdürüldü. Yeni WSL veya telefon ölçümü
yoktur. Bu değişiklik, `.34/.35`teki RID sınıflandırmasının USB tüketicisine
ulaşmayan kısmını tamamlar; bütün manager/USB akışı bitmiş değildir.

### Aynı cihazın kaynak zinciri

Sabit `corsicanu/android_kernel_samsung_universal9820` commit'i
`55f042627bb7030c93550faafa117674d32b21d1` içinde üç ayrı durum vardır:

1. [max77705_pd_rid](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_pd.c#L1093)
   canlı `PD_STATUS1[3:0]` FCT değerini bir PD device sınıfına çevirir.
   Tüm tanımsız kodlar aynı `DEV_UNKNOWN` sınıfıdır. Önceki **PD device**
   değiştiyse MUIC RID bildirimi gönderilir; OPEN, UNKNOWN, 523K ve 619K
   için ayrıca USB DETACH gönderilir. İlk `kzalloc` durumu DEV_NONE'dır;
   ilk OPEN/UNKNOWN bu nedenle bir değişimdir.
2. [MUIC RID tüketicisi](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/muic/max77705-muic-ccic.c#L116)
   yalnız 1..7 aralığını kabul eder. Tanımsız RID son kabul edilen MUIC
   RID'ini silmez. 1K cache'e alınır fakat tek başına detect işi oluşturmaz.
   İlk MUIC RID'i OPEN'dır; bu, ilk PD device durumuyla aynı şey değildir.
3. [typec manager](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/usb/manager/usb_typec_manager_notifier.c#L724)
   kendi RID durumunu günceller. UART 523K/619K varken USB attach'i
   engeller; bu RID koşulu USB detach'i engellemez. Aynı fonksiyondaki
   bağımsız charger filtresi de vardır; bu sürümde o dal tamamlanmadı.

Manager ve MUIC tüketicisinin aynı commit üzerindeki dosyaları ayrıca
yeniden alınarak kaynak yolu ve içerikleri doğrulandı.
HWC1 `CONFIG_CCIC_NOTIFIER`, `CONFIG_TYPEC` ve
`CONFIG_USB_TYPEC_MANAGER_NOTIFIER` açıktır; kapalı bir konfigürasyon dalı
Windows'a gerekçesiz taşınmadı.

### Windows'ta düzeltilen ayrım

Önceki kod yalnız `MuicRid` değişimini izliyordu. Bu yüzden MUIC'in
reddettiği UNKNOWN veya başlangıçta zaten OPEN olan MUIC cache'i,
Linux'un ayrı USB-detach olayını kaybettiriyordu. UCM ayrıca sadece geçerli
CC/DR değerlerine bakıyordu; RID ile kapatılmış USB bağlantısını sonraki
CCI veya CC güncellemesi yeniden açabilirdi.

Yeni `PdRid`, PD device karşılaştırması için normalleştirilmiş sınıftır:
0=UNKNOWN, 1..7=FCT/RID, ff=henüz okunmadı. Bunlar Linux DEV enum sayıları
olarak sunulmaz. Ham `RidRaw` ve son kabul edilen `MuicRid` ayrı kalır.
`PdRidChanges`, tanımsız farklı ham kodlar arasında artmaz; ilk geçerli
okumadaki DEV_NONE geçişi ise kaybolmaz.

`UsbRoleAttached`, gerçek DATAROLE işleyicisinden çıkan USB bildiriminin
durumunu tutar. CC gücü ve PD veri rolünden ayrı olması zorunludur:

| Gerçek olay | USB tüketicisine etkisi | Korunan gerçek durum |
|---|---|---|
| PD device değişimi → OPEN/UNKNOWN/523K/619K | MUIC bildiriminden sonra USB detach kuyruğa girer | CC/DR geçerliliği ve değerleri, VBUS/OTG, PDO ve opcode kuyruğu |
| Aynı tanımlı RID veya farklı ham UNKNOWN kodu | Yeni RID olayı üretilmez | Ham son FCT okunması görünür kalır |
| RID nedeniyle detach sonrası aynı DATAROLE | Attach üretilmez; stok `current_dr` karşılaştırması değişmemiştir | Gerçek DR korunur |
| Yeni DATAROLE, manager RID 523K/619K | MUIC işi kendi akışındadır; USB attach engellenir | Yeni gerçek DR kaydedilir; özgün RpRd/RID önceliği korunur |
| Yeni DATAROLE, diğer RID | Mevcut bağımsız USB tüketicisi yeni rolü alır | SOURCE veya DFP tahmini yapılmaz |
| Fiziksel CC no-connection | Önceki `.39` queue-clear/VBUS/PD/detach akışı sürer | PD RID cache'i sırf CC detach nedeniyle sıfırlanmaz |

RID değişimi bir **USB bildirimi**dir; bunun için sahte CC kesmesi,
CC NO_CONNECTION değeri, VBUS-off veya firmware queue-clear üretilmez.
MUIC detect okuması hata verse bile bağımsız RID USB detach kaybolmaz;
RID okumasının kendisi hata verirse eski veriyle karar verilmez.
Aynı fiziksel frame'de DATAROLE ve FCTID varsa stok sıra korunur:
DATAROLE önce, FCTID sonra. İki USB bildirimi de ayrı kuyruk kayıtlarıdır.

UCM attach artık bu USB bildirim durumunu da gerektirir. RID detach sonrası
geçerli CC/DR değerleri tek başına attach oluşturamaz. Daha sonraki gerçek
attach, bekleyen detach kaydının arkasında kalır. Aynı rolü isteyen OS
callback'i de USB bildirimi kapalıyken veya MFD hatalıyken başarı dönmez.
Bu callback değişikliği, eksik OS rol değiştirme komutunu uygulamış değildir.

### Tanı, kontroller ve build

FOTO v14'ün UCM sayfası **25 satırdır**. `PD_RID`, `RID_TRANS`,
`RID_DETACH`, `RID_BLOCK`, `USB_NOTIFY` alanları eklendi; mevcut MUIC
sayfasının ham RID ve kabul edilen RID alanları ayrı durur.
`RID_DETACH` handler'ın USB-detach üretim sayısıdır; UCM çağrısı veya
fiziksel kablo çıkarma sayısı değildir. `USB_NOTIFY=1` OS kabulü, mux
readback'i veya USB transfer başarısı değildir; `UCM_ATTACHED` ve
`UCM_NT` ayrı değerlendirilir. İlk probe OPEN okumasında RID_DETACH artıp
sonraki gerçek başlangıç DATAROLE ile USB_NOTIFY=1 olabilir; bu çelişki
değildir. Registry 232 alanı ardışık yayımlar; sayfa atomik snapshot değildir.
Startnet özeti aynı beş alanı içerir. REG_BINARY olay biçimi/IOCTL ABI değişmedi.

Gerçek C işlevlerini çalıştıran host fixture'lar: tüm detach RID'leri,
UNKNOWN normalleştirmesi, ilk OPEN, tekrar RID/DR, UART attach engeli,
sonraki gerçek DR, tek frame DATAROLE→FCTID sırası, MUIC/RID okuma hatası,
CC detach ve UCM detach bariyeri için geçti. CC/PD, lifecycle, foto genişlik
ve satır sınırı, Windows USB/MAX kaynak kontrolleri de geçti. IRQ ve UCM
fixture'ları AddressSanitizer/UndefinedBehaviorSanitizer ile de geçti; 232
registry adı/ataması kontrol edildi. Bunlar WDK,
GIC IRQ teslimi, UCM/URS runtime veya telefonda USB transfer testi değildir.

Build yalnız WSL'de, DEVIR'deki aynı yöntemle:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

### Bu tur incelenen, henüz kapanmamış kaynak boşlukları

- **SOURCE akımı:** Linux'un `pwr_opmode=TYPEC_PWR_MODE_USB` yazılım
  başlangıcı elektriksel SOURCE Rp ölçümü değildir. MAX77705 header'ındaki
  `CCSrcCurCh/CCSrcCur` isimleri de encoding ve latched/current ayrımını
  tek başına kanıtlamaz. Komşu MAX77860'ın
  [resmî datasheet'i](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX77860.pdf)
  CC_CTRL1 alanının istek latch'i olduğunu, readback'in gerçek pullup'ı
  her zaman göstermediğini anlatır. Bu **başka silicon** açıklaması,
  MAX77705 için akım değeri atama yetkisi vermez. `Invalid` değerinin bu
  telefondaki UCM attach'i reddettirdiği henüz ölçülmedi; kesin kök neden
  diye sunulamaz.
- **Mux ve OS bildirimi:** Linux `notify_dr_status` MUIC bildirimini USB'den
  önce üretir; her iki tüketici asenkrondur. Windows kodu MUIC isteğini
  başlatıp ayrı UCM kuyruğuna geçiyor; MUIC'in gerçek tamamlanması ve USB3
  orientation/PHY hazırlığı UCM attach için bütünleşmiş bir hazır olma
  sözleşmesine henüz bağlı değil. Microsoft
  [UCM attach sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system#2-report-the-partner-connector-attach-event)
  öncesinde mux'un doğru kurulmasını ister. Sabit bekleme, sahte PATH_OK
  veya yalnız bir flag eklemek bu işi tamamlamaz; gerçek tamamlanma,
  hata, detach ve yeni nesil bildirim ömrü birlikte bağlanmalıdır.
- UART AP/CP tüketicisi, manager charger politikası, OS DR/PR swap ve RDO,
  battery/CHGINSEL, AFC/VDM/READY/RAM tüketicileri, USB3 ve function/gerçek
  transfer hedefi hâlâ açıktır. Bu tur bunların yerine başka bir akış koymadı.

## 5 Eylül 2026: `.40` — gerçek opcode yanıtının dağıtımı

MAX `0.1.0.40`, parent/host/function `0.1.0.19`, ABI `7`. `.39`daki gerçek
detach queue-clear ardından ortaya çıkan yanıt eşleme farkı düzeltildi.
Kaynak çalışması flaş beklenmeden sürüyor; yeni telefon veya WSL sonucu yok.
Bu bölüm, `.38/.39`daki **opcode uyuşmazlığı tüm kuyruğu/MFD'yi durdurur**
ve **GRL trim yalnız beklenen opcode eşleşirse çalışır** açıklamalarının
yerini alır. Gerçek I2C hatası ile yanıt türü uyuşmazlığı farklı sonuçlardır.

### Stok kaynak ve değiştirilen davranış

Sabit beyond2lte
[max77705_irq_execute / cmd_run](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_usbc.c#L1813)
yanıtı bekleyen düğümün `read_length` uzunluğuyla bir kez okur. İlk bayt
beklenenden farklıysa farkı kaydeder; `switch(response)` yine **gerçek
opcode** ile çalışır. GRL açık kaynak dalında firmware-clear istisnası
yoktur. `cmd_run` yanıt tüketiminden sonra sıradaki komuta ilerler.

Windows'un eski eşleştirmesi ise farklı opcode'u `MfdFault` yapıp tüm
istekleri iptal ediyor ve GPIO kesmesini maskeliyordu. Bu, Linux'un normal
dağıtımını kesiyor; özellikle yazılım queue-clear sonrası gelen yanıtlar
USB başlangıcını kalıcı durdurabiliyordu.

Yeni `b2lmax77705_response.c`, gerçek yanıtın tüketimini istek sahibinin
sonucundan ayırır. Örneğin `EXPECTED=0b / RECEIVED=70` ise gerçek GRL
handler'ı trim uygular; bekleyen `0b` isteği `STATUS_DEVICE_PROTOCOL_ERROR`
alır. Bu istek CCCTRL1 okumuş sayılmaz. Trim başarılıysa sonraki komut
ilerler; trim I2C hatası verirse kalan istekler gerçek I2C hatasıyla iptal
edilir. Uyuşmazlık sayacı ve her iki sonuç da görünür kalır.

| Gerçek yanıt | `.40` tüketimi ve kanıt sınırı |
|---|---|
| Register READ `01/03/05/07/09/0b/0d/0f/11/13/15/17` | Stok kaynak yalnız UPDATE_SEQ için yeni yazı üretir; mevcut Windows READ/WRITE isteklerinde ek yazı yok. Eşleşen isteğin readback doğrulaması ayrı çalışır. |
| CONTROL3 `1c` | Gerçek bayt ve geçerliliği saklanır. Bekleyen eşleşen CONTROL3 istemcisi kendi CC-open devam işini yürütür; farklı isteğin türü CONTROL3'e çevrilmez. |
| SBU `25` | İki gerçek ham SBU baytı saklanır; eksik veri geçerli sayılmaz. CC, su veya attach olayı üretilmez. |
| CURRENT_SRCCAP `30` | Gerçek opcode ile PDO okuyucu çağrılır. Bilinen eski bağlantı nesli elenir; canlı SINK/PS_RDY koşulu korunur. Başlık ve gerekli PDO baytları mevcutsa ham PDO gözlemi yayımlanır. |
| SRCCAP_REQUEST `32` | `fe/ff` yanıtı stok retry bayrağını kurar. Başka yanıt bu bayrağı kendiliğinden silmez. Gerçek detach ve CC SOURCE stok gibi temizler. |
| SET_ALTERNATEMODE `55` | Firmware yanıtı `ALT_ACK` olarak kaydedilir; manager READY, START isteği veya DP anlaşması üretilmez. |
| SELFTEST `59` | CC/SBU selftest baytları kendi geçerlilik alanlarında tutulur. Ayrı `25` SBU örneğiyle karıştırılmaz. |
| GRL `70` | Gerçek yanıt opcode'u `62:1f=04` trim ve 100 ms adımını bir kez çalıştırır. İstek emekli edilirken ikinci trim yapılmaz. Kapanış sırasında yeni politika yazısı yapılmaz. |
| AFC/QC `21/22`, GET_SRCCAP `31`, VDM `48/4b`, RAM_TEST `d1` | Gerçek tüketiciler tamamlanmadığı için `STATUS_NOT_SUPPORTED / RX_UNHANDLED` görünür. Tahmini güç/VDM işlemi gönderilmez. Bunlar hedefin açık parçalarıdır. |
| Diğer/default | Stok switch'in ek iş yapmayan dalı; tek başına bir özellik veya bağlantı başarısı değildir. HWC1 `CONFIG_DIRECT_CHARGING` kapalı olduğundan `5d` bu derlemede log-only'dir. |

PDO yerleşimi aynı commit'in
[max77705_current_pdo](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_pd.c#L743)
işleyicisinden izlenir. Aktif HWC1 `PDIC_PD30` dalı kapalıdır. Mevcut Windows
PDO listesi **ham gözlem** olmaya devam eder; battery power-list, RDO,
selected PDO ve OS PD sözleşmesi henüz değildir. Kaynağın
`srcccap_request_retry` bayrağını görmek, current PDO'yu selected PDO gibi
kullanmayı haklı çıkarmaz. Gerçek seçim üreticisi ve sonraki CCI=3 retry
isteği birlikte tamamlanmalıdır.

### Uzunluk, istek ömrü ve hata ayrımı

Beklenen kısa komutun yerine `30` gelirse sadece gerçekten okunan baytlar
vardır. PDO başlığının gerektirdiği `2 + 4*N` bayt yoksa sonuç
`STATUS_BUFFER_TOO_SMALL` olur; sıfırla doldurulmuş alanlardan PDO üretilmez.
Ek polling veya ikinci mailbox okuması eklenmedi. Kısa SBU/SELFTEST/CONTROL3
cevapları da geçerli veri sayılmaz. Başka opcode beklenirken gelen tam
`30` paketi gerçek yanıt türüyle çözümlenir; eski bekleyen istek yine kendi
uyuşmazlık sonucunu alır.

MUIC WRITE ve READ'in durumları artık ayrı korunur. WRITE farklı opcode
alıp READ sonradan istenen path'i döndürse bile çiftin sonucu başarılı
olmaz. `PATH_READ` gerçek baytı gösterebilirken `PATH_OK=0 / PATH_NT=...`
kalır. Slot yalnız çiftin son düğümü emekli edilince yeniden kullanılabilir.

Readback içerik hatası kontrol grubunun sonucudur; tek başına I2C arızası
ve GPIO maskesi üretmez. Başarısız CONTROL3/CCCTRL1 doğrulaması
`INIT_NT` ve doğrulama bayraklarında kalır, tüm grup başarılı gösterilmez.
Gerçek I2C okuma/yazma, trim ve deadline hataları mevcut iptal yolundadır.
Aynı frame'de gerçek mailbox okuma hatası ve POR varsa hata kaydı korunur,
POR yine işlenir. Sadece opcode farkı varsa POR öncesi `PRIOR_FAULT=0`
normaldir; bu fark artık sahte I2C arızasına dönüştürülmez.

### Tanı ve paketleme

`MAX FOTO v13`, kurtarma sayfasından sonra **29 satırlık gerçek yanıt
sayfası** gösterir. `EXPECTED`, `RECEIVED`, `RX_LEN`, `REQ_NT` ve
`DISPATCH_NT` ayrı alanlardır. `REQ_NT` tek opcode isteğinin sonucudur;
MUIC çiftinin ve init grubunun sonucu kendi sayfalarındadır.
`RX_MISMATCH`, `RX_SHORT`, `RX_UNHANDLED` farklı sayaçlardır.

`RAW0/RAW1` ilk sekiz baytın iki DWORD paketidir; düşük bayt önce gelir,
yalnız `min(RX_LEN,8)` bayt geçerlidir. Registry alanları ardışık okunduğu
için bu sayfa atomik packet günlüğü değildir. Mevcut REG_BINARY IRQ günlüğü
ve ABI değişmedi. Tam okunan yanıt sürücü context'inde 33 bayta kadar
saklanır. Son okuma başarısızsa `RX_LEN=0 / RECEIVED=ff` ve hata görünür;
sıfırlar gerçek payload olarak sunulmaz.

`ALT_ACK`, CONTROL3, SBU ve SELFTEST alanlarının geçerliliği yeni D0 ve
gerçek USBC resetinde kaldırılır. Snapshot, IRQ veya OS attach yerine
kullanılmazlar. Startnet özeti aynı alanları içerir. Yeni response modülü
WSL kopyalama/derleme/link listelerine eklendi; SYSMSG/control/response
kaynaklarının her derlemesinden sonra hata kontrolü yapılır. Başarısız bir
derleme sonraki komutun başarı koduyla örtülemez.

### Kontroller ve devam eden hedef

Host fixture gerçek sürücü C işlevlerini çalıştırır: uyumsuz opcode ile
sıradaki komutun ilerlemesi, uyuşmazlık/gerçek I2C hatası ayrımı, gerçek GRL
ve trim hatası, aynı frame POR, eski PDO nesli, tam/kısa PDO payload'ı,
MUIC WRITE hata + READ başarı birleşimi, CONTROL3 sahipliği, eksik
tüketiciler, SBU/selftest ayrımı, SOURCE/detach retry temizliği ve kapanış.
CC/PD, UCM, PnP lifecycle, foto satır/genişlik ve Windows kaynak kontrolleri
geçti. Aynı IRQ/yanıt fixture'ı AddressSanitizer ve UndefinedBehaviorSanitizer
ile de geçti; 227 registry adı/indeksi ve WSL modül listesi doğrulandı.
Bu sonuçlar WDK ABI, gerçek GIC teslimi veya telefon USB transferi
kanıtı değildir. Mac üzerinde WDK/UEFI/WIM build yapılmadı.

WSL komutları değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

İleride cihazda `.40` / FOTO v13 ile geçerli status, gerçek CC SOURCE ve
ayrı DFP, VBUS/MUIC doğrulaması, UCM/URS ardından root-hub ve USB aygıtının
gerçek transferi aranır. Yanıt uyuşmazlığı olursa beklenen/alınan opcode ve
iki sonuç birlikte değerlendirilir. Kaynak işi bu ölçümü beklemez:
source Rp, gerçek OS DR/PR swap, battery/CHGINSEL/RDO, AFC/VDM ve manager
READY tüketicileri, RAM_TEST, USB3 PHY ve uçtan uca function/transfer hedefi
hâlâ açıktır. Dispatch altyapısının taşınması bunların tamamlandığı değildir.

## 5 Eylül 2026: `.39` — gerçek CC detach ve bekleyen komutların ömrü

MAX `0.1.0.39`, parent/host/function `0.1.0.19`, ABI `7`. Kaynak işi
flaş beklenmeden sürdürüldü. Bu bölüm yeni telefon ölçümü değildir; `.28`
fotoğrafları daha sonraki sürümlerin çalıştığına kanıt olarak kullanılmaz.

### Kapatılan fark: CC No Connection dalındaki queue-clear

Aynı beyond2lte referansının
[max77705_cc.c / CCSTAT işleyicisi](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_cc.c#L538)
ile [USBC queue-clear/recover](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_usbc.c#L2108)
birlikte incelendi. Gerçek `cc_No_Connection` dalı aktif rol değiştirme
beklemesi yokken yazılım kuyruğunu temizler; ardından PD detach ve VBUS-off
gelir. Stok HWC1 `CONFIG_MAX77705_GRL_ENABLE=y` olduğundan burada firmware
queue-clear komutu gönderilmez.

Windows'ta bu çağrı eksikti. MUIC/PDO nesil kontrolü, eski yanıtın yeni
bağlantı için yayımlanmasını engelliyordu; **bekleyen eski fiziksel yazının
gönderilmesini engellemiyordu**. Örneğin USB_AP WRITE/READ ve ikinci USB_AP
çifti beklerken detach gelirse eski çiftler önce çalışabiliyordu.

`.39`, canlı CC okumasının `NO_CONNECTION` sonucu için `.38`in gerçek
queue-clear işlevini çağırır. Eski bağlı istekler `STATUS_CANCELLED` ile
emekli edilir; MUIC çiftleri ve PDO/control slotları serbest kalır. Sonraki
OPEN, VBUS-off başarılıysa ayrı yeni WRITE/READ çifti olarak kuyruğa girer.
OPEN doğrulaması gerçek APCMD ve readback gerektirir. VBUS kapatma hatasında
eski kuyruk yine temizlenmiş ve bilinen detach Windows bildirimine verilmiş
olur; güç kesildiği veya OPEN tamamlandığı söylenmez.

- İlk probe okuması kaynakta olduğu gibi yalnız SINK işler. Başlangıçta
  `CC=0` okumak queue-clear veya yeni fiziksel detach üretmez.
- SOURCE/SINK, audio/debug ve diğer CC durumları `NO_CONNECTION` gibi
  queue-clear dalına sokulmaz. Mevcut aksesuar tüketicilerinin eksikleri
  ayrıca sürer.
- Tekrarlanan gerçek no-connection IRQ'si yeniden temizler: arada kuyruğa
  eklenen komutlar olabilir. Sadece durum değiştiğinde temizleme yapılmaz.
- PASS2 VBUSDet'in gerçek BC/CC okumalarıyla doğrulanan detach yeniden
  dağıtımı da aynı dalı kullanır. Ham IRQ baytları değiştirilmez.
- Aynı fiziksel frame'de APCMD ve CCSTAT varsa stok nested sıra korunur:
  APCMD önce yanıtı emekli eder ve sıradaki yazıyı başlatabilir; CCSTAT sonra
  kalan kuyruğu temizler. CC'yi yapay olarak APCMD'nin önüne alma yoktur.
- Şu an OS rol değiştirme isteği başlatılmadığı için aktif `try_state_change`
  yoktur. Gerçek swap eklendiğinde stok detach/swap completion koşulu da
  birlikte uygulanmalıdır; bu değişiklik swap uygulanmış demek değildir.

### İptal edilen başlangıç grubunun tanısı

Detach normal probe komutları sürerken gelirse gönderilmemiş `0c/54`
recover listesinde kalır. Sonraki gerçek APCMD bu listeyi stok sırada işler.
Timer recover göndermez. `57/0b/1c` başlangıç adımları kaynakta recover
listesinde değildir; otomatik tekrar edilmez, eski başarıları üretilmez.

Bu durumda `INIT_NT=C0000120`, `INIT_DONE=0` başlangıç grubunun iptal
edildiğini anlatır. Recover yazıları daha sonra tamamlandı diye iptal
edilmiş başlangıç grubunun sonucu başarıya çevrilmez. `MfdFault=0` ile
birlikte görülebilir: yazılım iptali I2C arızası değildir. Gerçek resetin
başlattığı yeni kontrol grubu `.38`teki ayrı sonuç kurallarını korur.

`MAX FOTO v12` kurtarma sayfası **32 satır**, metinler 64 sütunun altındadır.
`DETACH_CLEARS` yalnız bu canlı CC dalının çağrı sayısıdır; `QUEUE_CLEARS`
reset/GRL dahil toplamdır; `QUEUE_DROPS` emekli edilen istek sayısıdır.
Bunlar farklı büyüklüklerdir ve sıfır olmayan temizleme sayısı tek başına
hata değildir. Startnet özeti aynı yeni alanı verir. Registry alanları
ardışık okunur, atomik frame sayılmaz; REG_BINARY olay formatı değişmedi.

### Aynı incelemede kesinleştirilen açık tüketiciler

[Type-C manager kaydı](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/usb/manager/usb_typec_manager_notifier.c#L802),
READY üreticisini açıkça gösterir: DP destekleniyorsa **battery + USB + DP**,
desteklenmiyorsa **battery + USB** dinleyicileri kaydolmalıdır. DP seçimi
aynı dosyada `displayport` düğümünün `dp,displayport_not_support` özelliğiyle
yapılır. Tek başına UCM nesnesi oluşturmak bu kayıtların karşılığı değildir.
Gerçek tüketiciler kurulmadan `READY|START` veya `55` üretmek eksik adımı
gizler; Windows alt mod/VDM yolu hâlâ tamamlanmalıdır.

CC kaynağındaki CCI, SINK iken karşı tarafın Rp bilgisidir. SOURCE akımı
olarak kopyalanamaz. Linux partner nesnesinin varsayılan USB güç modu da
MAX'ın elektriksel source Rp ölçümü değildir. Windows source advertisement
boşluğu, gerçek CCCTRL1 kaynak akım semantiğiyle kapatılmalıdır.

[Stok rol değiştirme isteği](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_usbc.c#L345)
yazılım queue-clear → `37` DR/PR isteği → gerçek rol olayını bekleme
şeklindedir. APCMD yalnız komut yanıtıdır; DATAROLE/CC/PS_RDY tamamlanması
ayrıdır. OS isteğini başarılı göstermek için rol alanına istenen değeri
yazmak kabul edilmez. UCM'nin OS swap sonrası partner DR_SWAP reddetme
koşulu da ayrı firmware/politika karşılığını gerektirir; yalnız `37`
göndermek tüm Windows swap sözleşmesini tamamlamaz.

Bir başka açık eşleme: stok `max77705_irq_execute`, beklenen opcode ile
ham yanıt uyuşmazlığını kaydettikten sonra **gerçek response opcode'una**
göre dispatch eder. Mevcut Windows, protokol uyuşmazlığında kuyruğu hatayla
durdurur. `.39` bunu başarılı yanıt saymaz veya gizlemez. Gerçek yanıt
tüketicilerini ve uzunluk/istek ömrünü birlikte taşıyan dispatch işi ayrıca
gereklidir; özellikle queue-clear sonrasında geç gelen yanıt için önemlidir.
Bu nedenle queue-clear dalının düzeltilmesi, mailbox ve tüm USB akışının
Linux ile eksiksiz eşitlendiği anlamına gelmez.

### Kontroller ve WSL

Gerçek Windows C işlevlerini kullanan host testleri geçti: bekleyen iki
USB_AP çifti sırasında CC detach, PDO isteği sırasında VBUSDet detach,
VBUS-off hatası, probe sırasında detach/recover, aynı frame APCMD+CCSTAT,
tekrar detach, başlangıç ve aksesuar ayrımı. IRQ, CC/PD, UCM, PnP lifecycle,
foto genişliği/satır sayısı ve iki Windows kaynak kontrolü de geçti.
Host fixture kesmeleri, telefonun gerçek kesme teslimi veya USB transferi
kanıtı değildir. WDK/UEFI/WIM bu Mac'te derlenmedi.

WSL yöntemleri değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

İlerideki cihaz ölçümünde MAX `.39` / FOTO v12, geçerli status snapshot,
gerçek SOURCE ve ayrı DFP, VBUS/MUIC doğrulaması, UCM/URS ardından root-hub
ve gerçek USB aygıt/transferi aranır. Detach sırasında `DETACH_CLEARS`
artışı, eski isteklerin iptali ve yeni OPEN yanıtı ayrı değerlendirilir.
Kaynak çalışması bu ölçümü beklemez. Source Rp, OS swap/PD, battery/CHGINSEL,
alt mod/VDM, gerçek response dispatch, USB3 ve uçtan uca transfer açıktır.

## 5 Eylül 2026: `.38` — asenkron başlangıç, POR/WDT ve GRL kurtarması

MAX `0.1.0.38`, parent/host/function `0.1.0.19`, ABI `7`. Kaynak eksikleri
flaş beklenmeden kapatılmaya devam edildi. **Bu bölüm cihazda çalışmış USB
veya yeni telefon ölçümü değildir.** `.28` fotoğraflarındaki `C00000B5`
status okuma hataları, kendi başına reset veya ForceError gerekçesi değildir.
Yeni reset yolu yalnız gerçek SYSMSG kesmesinin üç uyumlu canlı okumasından
çıkan `03` veya `05` için çalışır. GSIV114 HSI2C ve GPA1-6/GSIV51 MAX
kesmeleri korunur; bir timer attach, IRQ veya başarılı opcode yanıtı üretmez.

### Aynı cihazın kaynağında izlenen sıra

Referans, `.37`de gerçek HWC1 IKCONFIG'iyle GRL/AFC dalı doğrulanan
`corsicanu/android_kernel_samsung_universal9820` sabit
`55f042627bb7030c93550faafa117674d32b21d1` commit'idir:

- [USBC başlangıç ve SYSMSG işleyicisi](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_usbc.c):
  `max77705_usbc_probe`, `max77705_init_opcode`, `max77705_reset_ic`,
  `max77705_usbc_check_sysmsg`, `max77705_usbc_clear_queue`,
  `max77705_recover_opcode`, `max77705_set_forcetrimi` birlikte izlendi.
- [MFD kesme dağıtıcısı](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/mfd/max77705-irq.c):
  PASS2+ dalındaki `enable_nested_irq/usbc_irq` tüketimi doğrulandı. D2s ortak
  kaynağında görülmeyen bu beyond kurtarma ayrıntısı atlanmadı.
- [Alt mod hazır olma koşulu](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_alternate.c):
  `check_is_driver_loaded`, `ALTERNATE_MODE_READY`, `is_first_booting` ve
  START ayrı koşullardır. START isteği tek başına opcode `55` göndermez.

| Gerçek olay / aşama | Stok sıra ve Windows karşılığı |
|---|---|
| `SYSMSG=03`, WDT | Gerçek güç rolü SOURCE ise VBUS off; ardından aşağıdaki ortak reset sırası |
| `SYSMSG=05`, POR | Ortak reset; WDT'nin koşullu VBUS-off adımı POR'a eklenmez |
| Ortak reset | TOP `66:23` bit3 maskelenir → USBC `25:80=0f` → pasif 100 ms bekleme |
| Reset maskeleri | USBC `0e=04, 0f=20, 10=00, 11=f0`; normal probe'un `00/20/1c/ff` maskeleriyle karıştırılmaz |
| Reset UIC kaydı | Gerçek `25:02` okunur; tam bayt `RESET_UIC`, SYSMSG çıkarılmış `raw & bf` değeri bekleyen kayıt olarak tutulur |
| Komutların yeniden kurulması | Yazılım kuyruğu iptal edilir → `54 00 / R1` → audio destekli kart için `0c 1f / R1` → TOP bit3 açılır |
| `SYSMSG=64`, GRL | Yazılım kuyruğu iptal edilir → `70 01 / R2`; reset register'ı yazılmaz |
| Gerçek GRL yanıtı | Opcode eşleşen APCMD okumasından sonra DEBUG `62:1f=04` → 100 ms; yorum satırındaki unlock/b3 yazıları eklenmez |
| `SYSMSG=33`, GRL/VDM istisnası | `70`/`48` bekleyen yanıtı iptal edilir; retry, trim veya sonraki komut için sahte tamamlanma yapılmaz |

Resetin her I2C aşaması kontrol edilir. Hata varsa sonraki init adımı
çalıştırılmaz, eski ve yeni bağlı istekler gerçek hatayla sonlandırılır,
GPIO normal çalışmaya dönmüş sayılmaz. WDT'nin VBUS-off hatası da reset
başarısı olarak gizlenmez. AFC hatası, daha önceki GRL iptal sonucunun
arkasına saklanmaz; ilk MFD aktarım hatası korunur.

APCMD yanıt hatasıyla POR/WDT aynı ham kesme frame'inde geldiğinde nested
SYSMSG işleyicisi atlanmaz. Hatalı istekler kendi hata sonucuyla emekli
edilir; `PRIOR_FAULT` önceki MFD hatasını saklar. Yalnız gerçek reset yazısı,
100 ms bekleme, maskeler ve reset-sonu UIC okuması başarılıysa yeni mailbox
kullanımı açılır. Bir SYSMSG'nin görülmesi, GRL ya da timer eski hatayı
kendiliğinden silmez. Reset başarısızsa yeni init gönderilmez.

### Başlangıç ömrü ve yazılım kuyruğu düzeltmesi

Önceki Windows başlangıcı stack üzerinde dört opcode isteği tutup sırasıyla
bekliyordu. Reset bunları temizlerse dönen başlangıç hatası, yeni reset
komutlarını da kalıcı MFD hatasıyla iptal edebiliyordu. Linux'ta normal
probe/init komutları asenkron kuyruğa girer. `.38` bunları sürücüye ait sabit
slotlara taşıdı: başlangıç artık `STATUS_PENDING` döner; politika işçisi
foto/registry için uyandığında probe'u yeniden başlatmaz. `ProbeStarted`
ile `UsbcInitialized` farklı alanlardır.

Normal sıra korunur: MUIC ilk örneği → başlangıç CC/water/RID → `57 01`
AUTOIBUS → başlangıç PD veri rolü → normal probe maskeleri → `54 00`,
`0c 1f`, mevcut salt-okuma `0b` doğrulaması → TOP unmask → probe-sonu
CONTROL3 isteği. Gerçek `1c` yanıtında LockErrorEn varsa yalnız bit1
silinerek `1d` yazılır; mevcut ikinci `1c` tanı doğrulaması korunur.
Bir yanıtın oluşturduğu devam isteği sıranın başında zaten gönderildiyse
APCMD çıkışı aynı komutu ikinci kez göndermez.

Yeni `B2lMaxClearOpcodesLocked`, stok GRL derlemesindeki **yazılım**
queue-clear karşılığıdır. Her bağlı istemci `STATUS_CANCELLED` alır;
MUIC çiftleri ve PDO slotları son istekleri emekli edilmeden yeniden
kullanılmaz. Bu işlem MFD arızası, kablo detach bildirimi veya başarılı
yanıt değildir. `70/write_length=0` firmware-clear üretilmez.

Stok listede yalnız henüz gönderilmemiş `0c`, `54`, `55` yazı düğümleri
kurtarılır. Gönderilmiş baş isteğin Linux karşılığı artık bir response
node'dur; aynı komut recover listesine yanlışlıkla eklenmez. Sonraki gerçek
APCMD önce mevcut yanıtı ve sıradaki komutu işler; sonra recover listesi
opcode sırasıyla eklenir: `0c`, `54`, koşullu alt mod isteği. Örneğin probe
`57` yanıtını beklerken reset gelirse başlangıçtaki bekleyen `54/0c`
yazıları recover listesine girer; resetin yeni `54/0c` init çiftinden sonra
stok kaynak gibi ayrıca geri eklenir. Bu tekrar ile yanlış çift gönderme
aynı durum değildir.

GRL/APCMD_FAIL özel dequeue yolu yalnız başarısız baş yanıtı düşürür;
sonraki henüz gönderilmemiş isteği otomatik başlatmaz. Sonraki gerçek APCMD
o yazıyı gönderir; **bir sonraki** gerçek APCMD onun yanıtını okur. Timer
park etmiş düğümü ilerletmez. Kapanış sırasında mevcut yanıtlar alınabilir,
fakat yeni CONTROL3/recover devam işi veya GRL trim politikası başlatılmaz.

### Yakalanmış UIC biti, yeni ham IRQ değildir

Beyond MFD, reset sırasında read-to-clear ile tüketilen UIC bitlerini
**sonraki gerçek USBC üst kesmesinde** tekrar nested dağıtıma katar.
Windows'ta `Frame.Raw[]` son IRQ okumasını aynen tutar; `Frame.UicReplay`
ayrı saklanır ve sadece aktif dağıtımda birleştirilir. CHG-only IRQ bu
kaydı tüketmez. SYSMSG biti çıkarıldığından eski reset mesajı yeniden
çalıştırılmaz. VBUSDet varsa aynı stok canlı BC/CC detach kontrolü yapılır.

`RESET_UIC`, `UIC_PENDING`, `UIC_REPLAY`, `REPLAY_COUNT` alanları bu ayrımı
gösterir. Mevcut REG_BINARY olay sayfasının `irq` alanı hâlâ yalnız kendi
frame'inin ham okumasıdır; replay bilgisi o ham baytın içine yazılmaz.
Yeni genel tanı alanları ardışık registry değerleridir, atomik olay kaydı
olarak yorumlanmamalıdır.

### Başarı alanlarının anlamı ve açık alt mod adımı

`RESET_PHASE=8 / RESET_NT=0` reset-mask-enqueue-unmask sırasının bittiğini
söyler; komut yanıtlarının gelmiş olduğunu söylemez. `INIT_NT=103` ve
`INIT_Q>0` gerçek yanıtların beklendiğini gösterir. `INIT_DONE=1` ancak
mevcut başlangıç/kurtarma komut grubunun gerçek yanıtları tamamlandığında
oluşur. `GRL_NT=0` ayrıca gerçek `70` yanıtı ve trim yazısı gerektirir.

Reset eski AUTOIBUS, CCCTRL1 readback, CONTROL3 ve MUIC doğrulamalarını
geçersiz kılar. Stok reset init'i `57`, `0b` veya `1c`yi otomatik tekrar
etmediği için bu eski doğrulama bayrakları yeniden başarılı yapılmaz.
`0c` yanıtı kendi `CcControlResponse` alanında tutulur; eski ayrı `0b`
readback'iymiş gibi sunulmaz. MUIC yolu ancak yeni gerçek olayın komut ve
readback akışıyla yeniden doğrulanır; CC güç/veri rolleri reset nedeniyle
SOURCE/DFP veya detach olarak uydurulmaz.

Alt mod tarafı **tamamlanmış değildir**: gerçek Windows alt mod/VDM
tüketicisi henüz yoktur. Stok READY koşulu yerine `ALT_START_REQ=1` ile
istek, `ALT_NT=STATUS_DEVICE_NOT_READY` ile karşılanmamış koşul gösterilir.
`CONFIG_CCIC_ALTERNATE_MODE` kapalı diye MAX'ın kendi alternate dosyasındaki
bu yol yok kabul edilemez. READY üreticisi, `55`, koşullu SINK+DFP identity
`48`, VDM tüketicileri ve ilk-boot geçişi birlikte tamamlanmalıdır.
`INIT_DONE` tam alt mod, OS PD sözleşmesi veya USB transfer başarısı değildir.

### Tanı, kontroller ve WSL

`MAX FOTO v11` ayrı **30 satırlık başlangıç/reset/GRL sayfası** ekler.
Genel → USB → MUIC → UCM → PD → SYSMSG → kurtarma → olaylar sırası,
20 saniyelik klavyesiz sayfa geçişi ve 64 sütun altı metin korunur. Startnet
özeti aynı yeni alanları içerir. Ek `b2lmax77705_control.c` dosyası WSL WDK
paketinin kopyalama, derleme ve link listelerine eklendi.

Gerçek C ile host testleri: normal asenkron probe, probe'un yedi farklı
noktasında POR, SOURCE/SINK/bilinmeyen rol WDT ayrımı, reset aşamalarında
aktarımı kesme, gerçek UIC replay ve CHG-only koruması, GRL yanıtı/trim,
GRL yanlış/kayıp yanıtı, trim hatası, tekrar reset, MUIC/PD slot iptali,
GRL özel dequeue, CONTROL3 devamı, CC readback hatası ve kapanış. CC/PD,
UCM, PnP lifecycle, foto genişliği/satır sayısı ve Windows USB/MAX kaynak
kontrolleri de geçti. **WDK/UEFI/WIM build bu Mac'te yapılmadı.**

WSL girişleri DEVIR ile aynı:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Flaş yapıldığında ölçülecekler: yeni paketin MAX `.38` olması, normal probe
sonunda `INIT_Q=0 / INIT_NT=0`, geçerli status snapshot'ları, gerçek attach
sonrasında CC SOURCE ve ayrı DATAROLE DFP → VBUS/MUIC → UCM/URS → USB
root-hub/HID veya depolama zinciri. **Reset/GRL sayaçlarının sıfır olması
normaldir; test için zorla reset üretilmez.** Bu olaylar gerçekten gelirse
ayrı kurtarma alanları değerlendirilir. Kaynak işinin devamı bu ölçümü
beklemez: source Rp, OS DR/PR swap, PD/battery tüketicileri, alt mod/VDM,
USB3 PHY ve gerçek function/transfer zinciri halen açıktır.

## 5 Eylül 2026: `.37` — stok kernel yapılandırması, SYSMSG ve APCMD_FAIL

MAX `0.1.0.37`, parent/host/function `0.1.0.19`, ABI `7`. Ayrı SYSMSG
kesmesi ve mevcut opcode tüketicilerinin yeniden gönderme yolu eklendi.
Bu kaynak ilerlemesidir; WSL build/telefon/USB transfer sonucu değildir.

### Cihazın gerçek stok kernelinden çıkarılan yapılandırma

Sadece benzer d2s ağacının derleme seçeneklerine güvenilmedi. Depodaki
`stockfirmware/AP_G975FXXSGHWC1_CL25257816_QB62768724_REV01_user_low_ship_meta_OS12.tar.md5`
arşivinden **yalnız** `boot.img.lz4` okundu. LZ4 açıldıktan sonra Android
boot başlığının page size `2048`, kernel size `45053964` alanları kullanıldı.
Kernel içindeki `IKCFG_ST`/`IKCFG_ED` aralığı
`17552624..17591585` bulunup gömülü gzip yapılandırması açıldı.

```text
boot.img SHA256:
cc4a55cdb00278226a610d7dc129a9383376a545c1191e31f258cec59cbcbc45
açılmış IKCONFIG SHA256:
5d541b85c458ed2e77564faecb909d47afa8ebe697d383927460c44488cd5e0a

CONFIG_HV_MUIC_MAX77705_AFC=y
CONFIG_MAX77705_GRL_ENABLE=y
CONFIG_TYPEC=y
CONFIG_MUIC_MAX77705_CCIC=y
CONFIG_USB_TYPEC_MANAGER_NOTIFIER=y
# CONFIG_BATTERY_SAMSUNG_MHS is not set
# CONFIG_CCIC_ALTERNATE_MODE is not set
# CONFIG_PDIC_PD30 is not set
```

Bu değerler adı verilen **HWC1 stok kernelinin** kanıtıdır; fotoğraftaki
MAX firmware `1a.10` ile kernel sürümü aynı şey değildir. Aynı GRL/AFC
seçimi `corsicanu/android_kernel_samsung_universal9820` sabit
`55f042627bb7030c93550faafa117674d32b21d1` commit'indeki
[beyond2lte defconfig](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/arch/arm64/configs/exynos9820-beyond2lte_defconfig)
ve [beyond-stock defconfig](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/arch/arm64/configs/beyond-stock_defconfig)
dosyalarında da vardır. D2s ortak register/handler kaynağı olmaya devam
eder; koşullu kod dallarında bu gerçek cihaz yapılandırması önceliklidir.

**Önceki “swap öncesinde firmware queue clear” genellemesi düzeltilmiştir:**
bu GRL dalında `0x70` bir GRL komutudur. `max77705_usbc_clear_queue` yazılım
kuyruğunu temizler ve recover opcode listesini kurar; `!GRL` dalının
`0x70/write_length=0` firmware-clear komutunu göndermez.
[Cihaza ait USBC kaynağı](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/ccic/max77705_usbc.c)
`SYSMSG_SET_GRL=64` için ayrı `70 01 / read_length=2` yolu ve gerçek `70`
yanıtından sonra debug `62:1f=04`, 100 ms işlemini içerir. Bu dalın
tüketicisi henüz tamamlanmadığından `.37` o yazıları veya reseti üretmez.
UCM swap/reset portu bu ayrımı koruyarak devam etmelidir; non-GRL kuyruk
temizleme komutu varsayılan çözüm değildir. `.36`daki ham PDO okuması da
`PDIC_PD30`/APDO desteği ilan edildiği anlamına gelmez.

### Uygulanan gerçek SYSMSG akışı

Özgün nested sıra **BYP → APCMD → SYSMSG → MUIC → CC → PD** korunur.
SYSMSG, UIC interrupt bit6'dır. APCMD sırasında `0x07` okumak tek başına
SYSMSG işleyicisini çağırmaz. Yeni `b2lmax77705_sysmsg.c`, gerçek SYSMSG
kesmesi üzerinde `max77705_sysmsg_irq` gibi `USBC_STATUS2=07` adresini
üç kez okur. Üçü aynıysa mesaj geçerlidir. Uyuşmazlıkta alınan baytlar
korunur, `SYS_VALID=0`, `SYS_NT=C000009C` (`STATUS_DEVICE_DATA_ERROR`)
yayımlanır. Linux'un hata dalındaki yazılımsal `6d` sentinel'i donanımdan
okunmuş mesaj gibi yayımlanmaz. Kısmi I2C hatasında başarılı örnek sayısı
saklanır, gerçek hata ile bekleyen istekler iptal edilir.

Geçerli `SYSMSG=33` (`SYSERROR_APCMD_FAIL`) mevcut sıranın başındaki
**aynı** opcode ve payload'ı tekrar gönderir. İstek, event, MUIC/PD slotu
ve sonraki kuyruk korunur. `RETRY_NT=0` yalnız gönderimin sonucudur;
tamamlama sayacı artırılmaz ve yanıt ancak sonraki gerçek APCMD'de okunur.
APCMD ve SYSMSG aynı frame'deyse önce eski komut tamamlanır; sonra SYSMSG,
Linux'taki `last_opcode/next_opcode` sırası gibi o anda bekleyen komutu
ele alır. Timer SYSMSG veya opcode yanıtı üretmez.

Gönderme zamanı gerçek yeniden gönderimle güncellenir. Senkron bir çağrı
eski deadline'a kadar uyumuşsa, o beklemenin timeout'u yeni gönderimin
deadline'ı sanılmaz; kilit altında güncel zaman yeniden değerlendirilir.
Güncel deadline gerçekten dolarsa hata/iptal davranışı korunur.

GRL `70` ve VDM `48` komutlarının stokta özel dequeue yolu vardır.
Bu komutların üretici/yanıt tüketicileri henüz yoktur; yeniden gönderme
yardımcısı onları genel retry dalına sokmaz ve `NOT_SUPPORTED` gösterir.
Bu özel dequeue/recovery kolları tamamlanmış sayılmamalıdır.

### AFC saat düzeltmesi: gerçek tüketici ve hata ömrü

Gerçek kernelde AFC derleme seçeneği açık olduğu için
[max77705_muic_disable_afc_protocol](https://github.com/corsicanu/android_kernel_samsung_universal9820/blob/55f042627bb7030c93550faafa117674d32b21d1/drivers/muic/max77705-muic.c)
çağrısı da port edildi. Yalnız `SYSMSG=33` yolunda, kuyruk boş olsa bile,
stok sıralamayla `TOP 66:fe=c5` → `DEBUG 62:0f=04` → `62:0f=00` →
`66:fe=00` uygulanır. Bu kaynakta belgelenen D- yüksek/AFC clock
düzeltmesidir; Type-C attach, VBUS veya opcode tamamlaması üretmez.

Windows hata sonuçlarını da korur: unlock başarısızsa gizli register'a
devam edilmez; unlock sonucu belirsiz olsa da relock denenir. Clock-on
yazısı hatalıysa clock-off ve relock yine denenir. İlk hata korunur,
relock sonucu ayrıca gösterilir. Başarısız aktarım sonrası normal IRQ
yeniden açılmış veya komut başarılı olmuş sayılmaz. Aynı MfdLock bu dört
yazı sırasında diğer MAX işlemlerinin araya girmesini engeller.

### Tanı ve doğrulama

`MAX FOTO v10` yeni **24 satırlık SYSMSG** sayfasını PD/PDO ile olay
sayfalarının arasına koyar. READ0/1/2, READ_N, SYS_VALID, SYS_NT,
RETRY_OP/NT/TRIES, AFC_TRIES/NT ve RELOCK_NT ayrı alanlardır. Retry/AFC
alanları son ilgili işlemi gösterir; başka SYSMSG'nin başarılı sayılması
önceki retry'nin APCMD yanıtı aldığı anlamına gelmez. Genel alanlar ardışık
registry okumalarıdır, atomik olay fotoğrafı değildir. Startnet özetine de
bu alanlar eklendi; foto genişliği 64 sütunun altındadır.

Gerçek C koduyla host testleri: üç okuma uyumu/uyuşmazlığı, kısmi read
hatası, aynı komut/payload retry, APCMD+SYSMSG sırası, boş kuyrukta AFC,
dört yazının her birinde hata, relock, retry gönderim hatası ve eski
bekleme süresinin dolması. CC/PD, PnP lifecycle, UCM, foto ve Windows
USB/MAX kaynak kontrolleri de geçti. WDK/UEFI/WIM build bu Mac'te yapılmadı;
build listesine yeni SYSMSG modülü eklendi. WSL girişleri değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

**Açık işler:** gerçek POR/WDT reseti + GRL uyumlu queue/recover ömrü,
SYSMSG short/battery/VDM tüketicileri, source Rp, OS swap ve PD sözleşmesi,
USB3 ve uçtan uca transfer. `.37` bunları başarılı no-op olarak kapatmaz.
Kaynak çalışması flaş beklemeden devam eder.

## 5 Eylül 2026: `.36` — gerçek PDMSG/PS_RDY, VBUS sıralaması ve PDO yanıtı

MAX sürümü `0.1.0.36`; parent/host/function `0.1.0.19`, IOCTL ABI `7`
değişmedi. Kaynakta PD kesmelerinin tüketicisi tamamlanmaya devam edildi;
flaş beklenmedi. **Bu bölüm yeni telefon ölçümü değildir.** Eldeki `.28`
fotoğraflarında `v=0, nt=C00000B5`, 13 → 15 başarısız status snapshot'ı,
`CC_STATE=0`, `OTG_SW/HW=0` ve `PORTSC.CCS=0` görülüyordu. Bunlar bir PD
sözleşmesi, firmware reset gereği veya çalışan USB kanıtı değildi. `.36`
henüz WSL/WDK'da derlenmedi ve telefonda denenmedi.

### Linux kaynak zinciri ve uygulanan sıra

Referans sabit commit: `ianmacd/d2s@d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39`.
[max77705-irq.c](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c)
PD sırası PDMSG bit7 → PS_RDY bit6 → DATAROLE bit5 → FCTID bit0'dır.
[max77705_pd.c](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c)
içindeki `max77705_pdmsg_irq`, `max77705_pd_check_pdmsg`,
`max77705_psrdy_irq`, `max77705_check_pdo` ve `max77705_current_pdo`
birlikte izlendi. Windows'ta yeni `b2lmax77705_pd.c` bu olayları gerçek
MAX pasif ISR'sinin `MfdLock` korumasında tüketir. PDMSG için `0x0c`,
PS_RDY için `0x0d` canlı okunur; foto snapshot'ı politika girdisi yapılmaz.
Bir PD işleyicisinin hatası aynı frame'deki bağımsız PS_RDY/DATAROLE/FCTID
işleyicisini atlatmaz; ilk hata genel tanıda ve özel PD sonucu kendi alanında
korunur. HSI2C GSIV114 ve MAX GPA1-6 GSIV51 kaynakları değişmedi.

| Gerçek PDMSG | Samsung işlemi ve Windows karşılığı |
|---|---|
| `06` Source SenderResponse timeout | VBUS off; başarılı off sonrasında 800 ms gecikmeli kaynak VBUS işlemi |
| `16/19` hard reset received/sent | Yalnız gerçek SOURCE rolünde off; ardından 760 ms gecikmeli işlem |
| `18/1a/1b` Get_Vbus_turn_off / SRCTOSWAP / SWAPTOSNK | Gerçek charger off işlemi; eski gecikmeli on isteği iptal edilir |
| `1d` SWAPTOSRC | Gerçek PD firmware olayının VBUS-on isteği; CC güç/veri rolü burada yeniden etiketlenmez |
| `14/15` PR swap sent | Yalnız PR swap yönü kaydedilir; bitmiş swap sayılmaz |
| `17` Get_Vbus_turn_on | Stok switch dalı boş olduğundan register yazısı yapılmaz |
| `1c` SNKTOSWAP | Eski sink PDO/PS_RDY durumu geçersiz kılınır; eksik battery/CHGINSEL tüketicisi `STATUS_NOT_SUPPORTED` ile görünür kalır |

Diğer stok no-op dalları açıkça listelenir. Uygulanmamış MPSM, SRC_CAP
batarya bildirimi, Status/Alert, current-cable JIG ve bilinmeyen mesajlar
`PdUnhandledEvents` sayacını artırır ve `C00000BB` döner. Başarı sonucu
üreterek atlanmazlar.

Gecikmeli güç işi, [max77705_usbc.c içindeki
vbus_control_hard_reset](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c)
gibi süre sonunda gerçek rolün hâlâ SOURCE olduğunu kontrol eder.
Tek atımlık pasif WDF timer'ın amacı bu güç işlemidir; CC/PD/register
polling yapmaz ve kayıp IRQ'yu tamamlamaz. `schedule_delayed_work` gibi,
ikinci olay zaten bekleyen işin süresini ileri itmez. Başarısız off yeni
on planlamaz. Detach, SINK'e geçiş, su, BYP aşırı akım, mailbox hatası ve
PnP kapanışı bekleyen işi iptal eder. Kapanışta timer kilit dışında flush
edilir; kaynaklar unmap edildikten sonra I2C erişimi yapılmaz.

### PS_RDY ve gerçek 31 baytlık CURRENT_SRCCAP yanıtı

Gerçek PS_RDY kesmesinde `0x0d[4]` set ve CC güç rolü geçerliyse donanımın
PD desteği gördüğü kaydedilir. Yalnız SINK için stok `max77705_check_pdo`
gibi opcode `0x30`, write length `0`, read length `31` kuyruğa alınır.
PDMSG `01` bu kesmenin yerine geçmez; SOURCE PS_RDY sink PDO okuması üretmez.
Yanıt ancak gerçek APCMD IRQ ile tüketilir. Kuyruk okuma alanı opcode +
32 veri baytına genişletildi; mevcut MUIC/probe tek bayt tüketicileri korunur.

Yanıtın ilk veri baytında PDO sayısı `[2:0]`, mevcut PDO numarası `[5:3]`
çözülür. Sıfır veya sayıdan büyük seçim `STATUS_DEVICE_DATA_ERROR` olur;
ham başlık `CAPS_HEADER` alanında kalır. Geçerli sayımda en fazla yedi PDO,
little-endian ham 32 bit değerleri ve özgün numaralarıyla saklanır. Sabit,
batarya, değişken ve APDO nesneleri yeniden sıralanmaz. **`CAPS_VALID`, bu
okumanın ve sayı/seçim başlığının geçerliliğidir; bütün PDO semantiğinin,
RDO'nun, müzakere edilmiş gerilim/akımın veya OS PD sözleşmesinin doğrulaması
değildir.** Yeni voltaj isteği gönderilmez.

Asenkron istekler 16 sabit slot ve bağlantı nesliyle korunur. Detach veya
daha yeni PS_RDY sonrası eski yanıt tüketilip stale sayılır; yeni kabloya
aitmiş gibi yayımlanmaz. Dolu slotlar açık kaynak hatası verir. APCMD,
yanlış opcode, taşıma hatası ve iptal aynı istek emeklilik yolunu kullanır;
slot/counter ikinci kez bırakılmaz. `READ_DONE/STALE_DONE` hatalı veya iptal
edilmiş tamamlanmaları da kapsar; başarılı okuma sayacı olarak yorumlanmaz.

### CHGINSEL kaynak tuzağı ve halen eksik PD tüketicileri

Stok `PRSWAP_SNKTOSWAP` dalındaki `POWER_SUPPLY_EXT_PROP_CHGINSEL=0`
yorumundan `CNFG_12.bit5=0` sonucu çıkarılamaz.
[max77705_charger.c](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/battery_v2/max77705_charger.c)
property setter, `max77705_change_charge_path(charger, charger->cable_type)`
çağırır. Bu işlev wireless/WCIN yolu için biti temizler, diğer yolda set eder;
`charging_port` etkisi ayrıca `CONFIG_BATTERY_SAMSUNG_MHS` koşuluna bağlıdır.
Windows'ta bu özgün cable/battery tüketicisi kurulmadan tek bir bit yazısı
seçilmedi. Bu eksik `.36` ile kapatılmış sayılmaz.

OS'nin farklı DR/PR isteği, stok `0x37` swap komutundan önceki firmware
kuyruk temizleme/recovery, gerçek DATAROLE/PS_RDY ile sonuçlandırma, reddetme
ve timeout ömrü hâlâ eksik. Yalnız APCMD yanıtı swap tamamlanması değildir.
Microsoft [data-role callback sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ucmmanager/nc-ucmmanager-evt_ucm_connector_set_data_role)
ile swap sonrası partner DR_SWAP politikası da uygulanmalıdır. `PdConfig`
hâlâ NULL; [UcmConnectorPdConnectionStateChanged](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ucmmanager/nf-ucmmanager-ucmconnectorpdconnectionstatechanged)
ile PD sözleşmesi ilan edilmez. Source Rp, SYSMSG POR/WDT/APCMD_FAIL,
batarya/charger, RID detach/UART/CP, USB3 ve uçtan uca transfer açık işlerdir.

### Tanı, build ve doğrulama

`MAX FOTO v9`: genel → USB/PnP → MUIC/RID → UCM → **PD/PDO** → olay
sayfaları. Yeni PD sayfası 35 satır ve 64 sütundan dar; klavyesiz 20 saniyede
ilerler. PD mesajı/sonucu, PS_RDY ham baytı, CAPS_HEADER/VALID/NT, PDO1–7,
bekleyen/tamamlanan/eski okumalar ve VBUS gecikmeli iş sonucu ayrıdır.
Kritik `startnet.cmd` özetine de PD alanları eklendi. Registry'deki genel
alanlar ardışık okunur; atomik olay kaydı oldukları söylenmez.

WSL build girişleri korunur:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Yeni PD kaynak/nesne dosyası mevcut PowerShell WDK build/link listesine
eklendi. Bu Mac'te WDK/UEFI/WIM derlemesi yapılmadı. Host fixture testleri;
PD nested sıra, yedi ham PDO, hatalı seçim/yanlış opcode, send failure,
detach/yeniden PS_RDY ile eski yanıt, 16 slot dolması, 760/800 ms süre,
off hatası ve su/BYP/PnP iptalini çalıştırır. Taşıma testleri zaten 1–64
bayt gerçek transfer kodunu kapsar. Foto satır/genişlik testleri de geçer.
Bunlar gerçek IRQ teslimini, UCM/URS runtime'ını veya USB transferini
doğrulamaz; kaynak çalışmasının devamı kullanıcıdan flaş beklemez.

## 5 Eylül 2026: `.35` — gerçek MAX rolünden UCM/URS tüketicisine

Flaş beklenmeden Windows'taki eksik USB bildirim tüketicisi eklendi. MAX
`0.1.0.35`, parent/host/function paketleri `0.1.0.19`, IOCTL ABI `7`.
**Kaynak bağlantısı kurulmuştur; USB'nin çalışması veya Linux akışının
bütünü doğrulanmış değildir.** Özellikle aşağıdaki source Rp, PD ve hedef
Windows runtime sınırları yeni sürümün gerçek açık işleridir.

### Kapatılan statik HOST kopukluğu

`.34`te MAX güç/veri rolünü ayırmasına rağmen `.18` parent, PrepareHardware
ve D0Entry'de koşulsuz HOST seçiyordu. `_DEP` yalnız sürücü başlama ilişkisi
kuruyordu; fiziksel CC/PD sonucu Windows host PDO'sunun ömrünü yönetmiyordu.
Bu yol kaldırıldı:

1. Gerçek CCSTAT güç rolünü, gerçek PD DATAROLE veri rolünü günceller.
   Initial detect hâlâ Samsung'daki gibi SINK ile sınırlıdır; SOURCE/DFP
   tahmini veya ID-pin olayı üretilmez. MUIC analog anahtar tüketicisi korunur.
2. Bu işleyiciler `MfdLock` altında ayrı, 64 kayıtlık konektör kuyruğuna
   durum değişimini koyar. Aynı IRQ içindeki detach ve sonraki attach ayrı
   kayıt kalır. `PolicyWork` foto/registry işinin tamamlanması beklenmez.
3. Ayrı pasif `ConnectorWork`, kilidi bırakarak UCM'ye attach/detach veya
   veri/güç rolü değişimini bildirir. Yerel DFP için partner UFP, yerel UFP
   için partner DFP verilir. CC gücü ve PD verisi birbirinden türetilmez.
4. `UrsSetHardwareEventSupport(Device, FALSE)` Type-C sözleşmesini seçer.
   `UrsReportHardwareEvent(IdGround/IdFloat)` kullanılmaz. URS eski role ait
   stack'i kapatıp `EvtUrsSetRole` ile platform değişimini yaptırır.
5. Yalnız bu callback DWC3 rolünü değiştirir. D0 dönüşü yalnız daha önce
   OS'nin seçtiği rolü yeniden hazırlar; soğuk açılışın rolü NONE'dır.
   Kullanıcı komutuyla HOST zorlayan IOCTL reddedilir; `host` CLI kaldırıldı.

Linux eşlemesi: `ianmacd/d2s@d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39`
[notify_dr_status](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c),
[DATAROLE/CCI](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c).
Windows tarafındaki tüketici sözleşmeleri:
[Type-C → UCM → URS](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/dual-role-controller-bringup-for-a-usb-type-c-system),
[URS ACPI/child kaynakları](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture).
Microsoft'un genel örneklerindeki eski enum adları kopyalanmadı; DDI ve
WDK `UcmTypes.h/UcmManager.h` imzaları kontrol edildi. `UcmInitializeDevice`
WDFDEVICE oluşturulduktan sonra çağrılır. MAX INF artık `Class=UCM` ve
`Dependencies=UcmCx` kullanır; [resmî örnek INF](https://github.com/microsoft/Windows-driver-samples/blob/main/usb/UcmCxUcsi/UcmCxUcsi.inf)
ile aynı sınıf/sürücü bağımlılığı sözleşmesidir.

### ACPI, platform ve hata ömrü

BYN0002 parent `_CRS` artık DWC3 `10c00000/10000`, PHY `10b00000/200`,
PMU `1586072c/4` kaynaklarını taşır. Sabit BYN0003/PNP0D15 PDO kaldırıldı.
`USB0._ADR=0`, `UFN0._ADR=1`; her ikisinde özgün GSIV273 exclusive/level/high
vardır. URS aynı anda tek role ait child'ı başlatır. Parent gereksinim
filtresi yalnız DWC3 aralığını iki role aktarır; PHY/PMU child'a verilmez.
Parent artık ACPI tarafından atanmamış DWC3 adresini doğrudan map etmez.
MAX'ın GSIV114 ve GSIV51 kaynakları değişmedi.

HOST ve FUNCTION geçişleri önce eski denetleyicinin durduğunu kontrol eder;
sonra mevcut bounded PHY/core reset, GCTL rol seçimi ve gerçek GSTS kontrolü
çalışır. USB2 function tune değerleri stok `beyond2lte.dts` ilk hücresidir:
TXRES=3, SQRX=3; host ikinci hücre TXRES=1, SQRX=5. Ortak TXVREF=12,
TXPREEMPA=3, COMPDIS=7 korunur. Function normal Samsung v3p1 enable yolunu,
host `phy_exynos_usb_v3p1_config_host_mode` pulldown/VBUS-valid ayarını alır.
Bu PHY register isimlerindeki FORCE_BVALID/VBUSVLDEXT, stok PHY'nin gerçek
konfigürasyon bitleridir; Type-C attach veya güç ölçümü yerine kullanılmaz.
USB3 PCS/PMA erişimi eklenmedi; USB2-only sınırı hâlâ açıktır.

UCM çağrısı sırasında MAX pasif ISR kilidi tutulmaz. PnP kapanışında yeni
bildirimler kapatılır, worker flush edilir, gerçek UCM bağlantısı detach
edilir; connector ReleaseHardware'da silinir. Başarısız detach yeni attach
önüne bariyer koyar. Kuyruk taşması eski olayları polling ile yeniden
kurmaya çalışmaz: görünür hata bırakıp OS bağlantısını kapatmayı dener.
Fiziksel CC detach biliniyorsa charger-off hatası USB detach bildirimini
engellemez. Akım bildirimindeki hata daha önce gönderilen bağımsız veri/güç
bildirimini geri alınmış gibi göstermez.

CCI kesmesi ayrıca `max77705_ccistat_irq` gibi canlı CC_STATUS0 okur;
yerel SINK için Rp güncellemesini aynı kuyruğa taşır. Tek başına CCI,
SOURCE veya DATAROLE üretmez. Nested sıra WATER → CCI → CCSTAT → DATAROLE
olarak korunur. PD sözleşmesi/PDO tekrar isteği ve batarya bildirimleri bu
dar Rp taşımasının tamamladığı işler arasında değildir.

URS geçişi tanı timer'ını durdurur. NONE veya başarısız donanım geçişinde
register alanları okunmaz; ABI'deki `REGISTERS_VALID` bayrağı yoktur.
Function sırasında xHCI/PORTSC alanları okunmaz. Böylece kapalı PHY'ye
sonradan bir foto/observer erişimi yapılmaz ve sıfır alanlar ölçüm sayılmaz.
Başarısız D0 kapanışı da rolün hazırlık işaretini geçersiz kılar; resume eski
CurrentRole değerine bakıp başarı dönmez, donanım geçişini yeniden çalıştırır.

### Tamamlanmış sayılmayan önemli sınırlar

- **Source Rp akımı:** SINK'in `CC_STATUS0[5:4]` CCI alanı stok enumlardan
  default/1.5A/3A olarak çözülür. Yerel SOURCE'un gerçek Rp ilanını mevcut
  referansta kesin çözmediğimiz için `UcmTypeCCurrentInvalid` verilir.
  Charger boost limiti veya örnekteki 1.5A değeri yerine konmaz. Hedef UCM
  bu attach'i reddedebilir; bu durumda `UCM_NT` hata, `UCM_ATTACHED=0`
  kalır. **Bu nedenle source/host bağlantısı kapatılmış bir eksik değildir.**
- **OS tarafından DR/PR swap:** firmware'in yaptığı gerçek DATAROLE değişimi
  bildirilir. OS'nin farklı rol isteği henüz `STATUS_NOT_SUPPORTED` alır.
  İstenen rol zaten gerçek güncel rol ise UCM'ye mevcut rol bildirilir.
  Stok `0x37` komutunun APCMD yanıtı tamamlanmış swap kanıtı değildir;
  PDMSG/PS_RDY, reddetme ve timeout ömrü eklenmeden olumlu sonuç üretilmez.
  `PdConfig=NULL`; PD sözleşmesi/PDO desteği ilan edilmez.
- **Function:** URS rolü, USB2 PHY ayarı ve hedef inbox UFX'e bağlanacak INF
  üretimi vardır. Bu, MTP/USB ağ/storage gibi function sınıfının ve uçtan
  uca transferin tamamlandığı anlamına gelmez. O sınıflar ayrı açık iştir.
- SYSMSG POR/WDT/APCMD_FAIL, PDMSG/PS_RDY/hard-reset, RID USB-detach ve
  UART/CP tüketicileri, VCONN/accessory/alt-mode, AFC/hiccup/batarya, özgün
  iki saniyelik VBUS workaround'u ve USB3 clock/reset/PHY hâlâ açık.

### WSL build, paket ve tanı ekranı

Build girişleri aynı kaldı; bu Mac'te WDK/UEFI/WIM build yapılmadı:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

WSL'den çağrılan PowerShell, WDK UCM/URS 1.0 header'larını ve ARM64
`UcmCxStub.lib/UrsCxStub.lib` dosyalarını zorunlu kontrol eder. Paketleme,
**hedef boot.wim'in içindeki** UcmCx, Urscx01000, Ufx01000, ufxsynopsys ve
ufxsynopsys.inf dosyalarını ister. PE ARM64 mimarisi ve major/minor/build,
hedef ntoskrnl ile karşılaştırılır; servicing revision farkı kabul edilir.
Eksik veya başka build'e ait runtime varsa açık hatayla durur. Eski Windows
Mobile runtime'ı kopyalayan yardımcı geri getirilmedi. Hedef görüntüde
bileşenlerin servislenmiş olması ve gerçek Windows bağımlılıkları ayrıca
WSL doğrulamasına tabidir; sadece dosya bulunması kurulum kanıtı değildir.

`b2lusb-function.inf`, hedef ARM64 inbox INF'nin gerçek DDInstall,
Services/HW bölümlerinden Include/Needs ile üretilir. Belirsiz veya yalnız
x64 modeller reddedilir; section adı tahmin edilmez. Yeni INF ortak katalog,
DriverStore ve WIM geri okuma hash kontrolüne dahildir. PnP tanısı artık
`URS\BYN0002&HOST` ve `URS\BYN0002&FUNCTION` kimliklerini kullanır.

`MAX FOTO v8`: genel 40, MUIC/RID 34, yeni UCM sayfası 17 satır; aralarda
USB/PnP sayfası ve ardından dondurulmuş olay sayfaları vardır. Yirmi saniyelik
klavyesiz dönüş korunur. 141 registry alanı vardır. `UCM_NT` bildirim
sonucu, `UCM_ATTACHED` kabul edilen attach, `QUEUED/PENDING/ATTEMPTS`
kuyruk/gönderim denemesi, `OVERFLOW` kayıp sıralama, `SWAP_NT` OS rol
isteği sonucudur. Başarısız denemeler ATTEMPTS'e dahildir. Hiçbiri HID veya
veri aktarımı kanıtı değildir. `USB FOTO v3`, etkin HOST yokken PORTSC
sıfırlarını göstermeyip register okunmadığını yazar.

On kontrol grubu geçti: transport, IRQ, connection, lifecycle, events, yeni UCM
sıralama/hata fixture'ı, yeni URS/INF/runtime fixture'ı ve üç kaynak kontrolü.
UCM fixture'ı gerçek queue/worker C kodunu çalıştırır; attach/detach/attach,
duplicate, rol/akım değişimi, çağrı hataları, detach bariyeri, taşma ve
kapanış denetlenir. URS fixture'ı gerçek role/power/resource callback'lerini
çalıştırır; soğuk NONE, resume, function, hata ve adres aktarımını denetler.
Foto satır/genişlik kontrolü, shell sözdizimi ve diff kontrolü de geçti.
Bunlar KMDF/UCM/URS sürücülerini veya fiziksel kesme teslimini çalıştırmaz.

İleride flaşta aranacak sıra: gerçek MAX güç/veri rolü → UCM kabulü →
URS parent gerçek rol/GSTS → ilgili host/function PDO → root hub/alıcı/HID
ve gerçek transfer. Boş portta statik Started/HOST beklenmez. Sonraki kaynak
çalışması için yeni flaş önkoşul değildir; yukarıdaki açık akım/PD kolları
önce kaynak üzerinden tamamlanmalıdır.

## 5 Eylül 2026: `.34` — gerçek MUIC/BC sınıflandırması ve FCTID/RID

Flaş beklenmeden kaynak karşılaştırmasına devam edildi. `.33`ün UFP dalı
her durumda `OPEN` istiyordu; Samsung MUIC ise USB/CDP/OTG ve şarj
adaptörünü farklı değerlendiriyor. `.34` bu eksik analog-yol kararını
kapatır. **UCM → URS → DWC3 host/function zinciri henüz uygulanmadı**;
parent/host INF hâlâ `.18`, MAX INF `0.1.0.34` olmuştur. Bu bölüm kaynak ve
host-fixture kanıtıdır; yeni WSL build veya telefon sonucu değildir.

### İncelenen özgün kaynak ve gözden kaçan ayrıntılar

Referans yine `ianmacd/d2s`, commit
`d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39`:

- [MUIC VPS tablosu ve switch işlemleri](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/muic/max77705-muic.c):
  `muic_vps_table`, `max77705_muic_check_new_dev`, `max77705_resolve_chgtyp`,
  `max77705_muic_detect_dev`, `max77705_muic_logically_detach`,
  `max77705_muic_handle_attach`, `write_vps_regs`.
- [CCIC → MUIC bildirim durumu](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/muic/max77705-muic-ccic.c):
  `max77705_muic_handle_ccic_ATTACH`, `max77705_muic_handle_ccic_RID`.
- [PD RID ve DATAROLE işleyicileri](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c):
  `max77705_check_rid`, `max77705_pd_rid`, `max77705_datarole_irq_handler`.
- [MFD nested IRQ sırası](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c).

Sadece VPS tablosunun `muic_switch` alanını kopyalamak doğru port değildir:

1. `CHGTYP_TIMEOUT_OPEN=0xfb` bir Linux sınıflandırma değeridir, donanımdaki
   iki bitlik CHGTYP değildir. Normal runtime attach dalı USB_AP kullanır.
   İsimden yola çıkarak `OPEN=3f` göndermek yanlış olur. Firmware-update/erken
   boot özel DP-reset/charger-detection kolu bu sürümde uygulanmış sayılmaz.
2. `write_vps_regs()` içinde TA/unofficial TA için register yazan dal
   **`#if 0`** altındadır. İlk charger eşleşmesi Windows'ta da yeni OPEN
   komutu üretmez ve OPEN doğrulandı sayılmaz. Önceki kablo sınıfından
   logical detach gerekiyorsa OPEN o aşamada kuyruğa girer.
3. Duplicate OTG, diğer duplicate kablo türlerinin tersine switch komutunu
   yeniden gönderir. `.33`ün genel aynı-yol dedup işlemini burada kullanmak
   bu Linux workaround'unu düşürürdü. `.34` OTG/switch attach işlemi için
   yeniden uygulama yolunu kullanır; her yeni istek kendi gerçek APCMD ve
   READ yanıtıyla doğrulanır.
4. USB/CDP/timeout → OTG logical geçişinde araya OPEN girmez. USB_AP yeniden
   yazılır. Diğer desteklenen sınıf değişimlerinde önce logical OPEN gelir.
5. Samsung CCIC MUIC attach callback'i RpRd=1'de cached override'ı set eder;
   RpRd=0 olan attach onu temizlemez. Fiziksel CC detach temizler. `.34`
   bu kaynak davranışını korur; kendi DR-swap sezgisini eklemez. Windows OS
   detach/attach bildirimlerinin tamamı yine ayrı eksiktir.

### Eklenen yürütme yolu

`B2lMaxDetectMuicLocked()` gerçek USBC `0x06..0x0a` aralığını **beş bayt**
olarak okur. MFD'nin sekiz baytlık foto snapshot'ından karar çıkarmaz ve
read-to-clear `0x02..0x05` veya opcode response `0x51` tüketmez. `MfdLock`
altında çalışır; opcode yanıtını beklemeden mevcut asenkron kuyruğa istek
verir. UIC nested handler'ları APCMD'den sonra, CC/PD'den önce şu sırada
çalışır: VBUSDet, VBADC, DCD, FAKVB, CHGTYP, UIADC. Her aktif handler yeni
status okuması yapar. FAKVB burada gerçek donanım bayrağıdır; VBUS veya
CC/DFP olayı üretilmez. SYSMSG ve AFC/hiccup'ın kalan kolları açık kalır.

OPMODE_CCIC için ham UIADC, Linux'taki gibi son kabul edilmiş RID ile
sınıflandırılır; gerçek PD DATAROLE=DFP'den gelen RpRd override'ı sonra
uygulanır. `RID_OPEN=7` başlangıcı stok notifier başlangıcıdır. FCTID
`PD_STATUS1[3:0]` alanından probe ve gerçek PD FCTID bit0 olayında okunur.
Runtime sırası DATAROLE ardından FCTID'dir. Tanıda `FCT_RAW` ile MUIC'nin
kabul ettiği `RID_CACHED` ayrı tutulur: tanımsız FCT bildirimi MUIC'nin son
RID'sini silmez; 1K RID cache'i günceller ama tek başına detect işi başlatmaz.
Bu, RID'nin USB OS-detach/UART/batarya tarafının port edildiği anlamına gelmez.

| Gerçek girdiler / Linux kararı | `.34` analog yol davranışı |
|---|---|
| RID OPEN, VBUS high, CHGTYP USB | USB_AP WRITE/READ; gerçek eşleşme ile PATH_OK |
| RID OPEN, VBUS high, CDP | USB_AP WRITE/READ |
| RID GND veya gerçek DFP RpRd override'ı | OTG sınıfı; USB_AP; bundan güç rolü/VBUS kararı türetilmez |
| DCD+USB, yalnız init veya CHGTYP nedeni | Timeout sınıfı, normal runtime USB_AP |
| DCD+USB fakat DCD interrupt nedeni | DCD timeout dönüşümü yapılmaz; kalan özel-charger kuralları uygulanır |
| Samsung/Apple özel imza 1..5 + USB/CDP | Unofficial TA; USB olarak kabul edilmez |
| Özel imza 6 veya 7 | 1..5 kuralına yanlışlıkla dahil edilmez |
| TA/unofficial TA ilk eşleşmesi | Yeni switch yazısı yok; OPEN doğrulanmış varsayılmaz |
| RID 255K/301K ve VBUS high | USB jig sınıfı ve USB_AP; OS jig/function desteği iddiası yok |
| RID 523K/619K UART jig eşleşmesi | Sınıf görünür, DETECT_NT=NOT_SUPPORTED; UART AP/CP yönlendirmesi açık iş |

VBADC IRQ'sındaki stok `3.8..4.5 V + CC disconnected` kontrolü yalnız
çalışma kopyasının VBUS bitini temizler. Beş ham bayt değişmeden saklanır.
Diğer VBADC olayları sınıflandırma yapmadan döner. TA'nın VBUS high iken
CHGTYP=0 geçici sonucunda tutulması da korunur. Sıradan BC geçici sonucu
tek başına fiziksel CC detach veya yeni güç/veri rolü oluşturmaz.

Probe'da MUIC initial-detect, CC/PD init'ten önce yapılır. Ardından ilk
CC/SINK ve WATER okuması, RID kontrolü, AUTOIBUS, ilk PD veri rolü ve normal
init opcodes gelir. Başlangıçta gerçek USB/BC sonucu varsa `06/05`,
AUTOIBUS'tan önce kuyrukta olabilir. Hiçbir probe yanıt bekleyişi GPIO
unmask öncesine alınmaz. DATAROLE değişmediyse Linux gibi attach tekrar
üretilmez; yeni BC kararını kendi UIC olayı çalıştırır.

Linux'un ayrı CCIC notifier workqueue/USB tüketicisinin tamamı henüz bu
koda taşınmadı. Bu sürümde MUIC karar tüketicisi pasif MFD kilidi altında
çalışır; canlı status okuması yapar ve asenkron switch isteği verir. UCM
çağrılarının bu kilit altında yapılması planlanmaz: OS bildirimlerinin
sıralı, ayrı ömür/kilit yönetimi hâlâ yapılacak iştir.

### Tanı ve doğrulama

`MAX FOTO v7` genel sayfayı 40 satırda korur; USB/PnP sayfasından sonra ayrı
**34 satırlık MUIC/RID sayfası** gelir. Yirmi saniyede otomatik geçiş ve
klavyesiz döngü korunur. Alanlar tek açık registry anahtarından sırayla
okunur; atomik kernel snapshot oldukları iddia edilmez.

- `RAW_VALID=0`: son beş baytlık okuma başarısız; gösterilen ham baytlar
  önceki okumaya ait olabilir. Yeni kablo/anahtar kararı çıkarılmaz.
- `CLASS_VALID=0`: bu olay yeni sınıflandırma yapmadı; örneğin ilgisiz
  VBADC olayı. `RAW_VALID=1` tek başına yeni CABLE kararı anlamına gelmez.
- `RAW08_BC` donanım okuması; `BC_USED` yalnız stok VBADC/RID kurallarının
  uygulandığı çalışma kopyasıdır. `CHARGER_USED=fb/fc` yazılmış register
  değeri değil Linux yazılım sınıfıdır.
- `DETECT_NT` okuma/sınıflandırma sonucu; `PATH_NT/Q/OK` gerçek opcode
  isteği ve read-back sonucu; `DATA_NT` ayrı veri-rolü tüketicisi sonucu.
  UFP'de USB_AP doğrulansa bile function stack uygulanmadığından DATA_NT
  `STATUS_NOT_SUPPORTED` kalır. Bunlardan hiçbiri enumeration kanıtı değildir.

133 registry adı/değeri, TXT kısa özeti ve foto sayfası aynı sürümle
kaydedildi. Gerçek C işleyicilerini çalıştıran fixture'da 24 bağımsız kablo
vektörü, beş bayt hata/stale-data, VBADC çalışma kopyası, özel şarj/DCD
önceliği, RID cache ve tanımsız FCT, CC/DR üretmeden UIC işleme, ilk USB'nin
AUTOIBUS öncesi opcode sırası, logical switch sırası ve gerçek APCMD
read-back test edildi. Transport, IRQ, connection, lifecycle, events ve
üç kaynak kontrolü olmak üzere sekiz suite geçti. Foto genişliği `<64`,
40/34 satır ve eksik registry anahtarı görünürlüğü de test edildi.
Bu testler WDK/KMDF veya gerçek GPIO teslimini doğrulamaz.

WSL build yöntemi değişmedi; bu Mac'te WDK/UEFI/WIM build yapılmadı:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Flaş daha sonra yapıldığında `.34` ve aynı ACPI'yi içeren boot/system çifti
kullanılır. USB/CDP bağlantısında `CABLE=1/2`, `PATH_REQ/READ=09`, gerçek
APCMD sonrasında `PATH_NT=0/PATH_OK=1`; OTG'de `CABLE=3` ve ayrı gerçek
CC SOURCE/VBUS kararı aranır. İlk TA'da `CABLE=4` tek başına PATH_OK=1
beklentisi doğurmaz. Flaş beklemek kaynak çalışmasının önkoşulu değildir.

### Açık kalan işler ve sonraki bağlantı

UCM/URS belgeleri, firmware'in çözdüğü gerçek Type-C rolünün UCM'ye
bildirilmesini ve URS'nin eski host/function stack'ini kapatıp yenisini
başlatmasını tarif eder. Type-C olayını Micro-AB ID-pin olayı diye sunmak
bu yolun parçası değildir. Kaynaklar:
[UCM client akışı](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system),
[URS Type-C olay sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/urstypes/ne-urstypes-_urs_hardware_event).
Bu turda UCM/URS kodu veya ACPI rol sahipliği değiştirilmedi.

Kalan işler: UCM/URS ve function yaşam döngüsü; RID USB-detach ve UART/CP
consumer'ları; SYSMSG POR/WDT/APCMD_FAIL, PDMSG/PS_RDY ve swap/hard-reset;
short/VCONN/accessory/alt-mode; AFC/hiccup/batarya ve iki saniyelik özgün
VBUS workaround'u; kanıtlı PHY/USB3 clock/reset akışı. Normal MUIC tablosu
port edildi diye bu özel kollar kapatılmış sayılmaz. Kaynakta ilerlenebilir;
kullanıcıdan yeni flaş gerektiren bir durdurma koşulu yoktur.

## 5 Eylül 2026: `.33` — nested IRQ sırası ve asenkron MUIC

`.32` sonundaki somut gecikme kapatıldı: MUIC opcode yanıtını bekleyen
politika işi artık BYP/CC/PD işleyicilerini durdurmuyor. Bu kaynak çalışması
flaş beklenmeden yapıldı. `.33` **tam USB tamamlandı anlamına gelmez**;
aşağıdaki açık OS rolü/PD/function/USB3 maddeleri hâlâ hedefin parçasıdır.

### Önceki gecikme ve gerçek kesmedeki yeni sıra

`.32` pasif MAX ISR'si APCMDRES'i tamamlıyor, BYP/CC/PD'yi daha sonra
`PolicyWork` içinde işliyordu. İş, `CONTROL1_WRITE`/`CONTROL1_READ` yanıtını
senkron beklerken yeni aşırı akım veya detach kaydı aynı işi bekliyordu.
Samsung MFD tablosunda CHG BYP, USBC APCMD'den önce; CC WATER/CCSTAT ve PD
DATAROLE ise bunlardan sonra dispatch edilir. Linux notifier/opcode kuyruğu
komut yanıtını aynı nested handler içinde beklemez.

`.33` uygulanan alt küme için aynı kesme sırasını kullanır:

1. Özgün GPA1-6 / GSIV51 pasif ISR TOP, CHG ve USBC read-to-clear
   kaynaklarını alır; ham baytlar korunur. VBUSDet recheck ve status snapshot
   okumaları MFD'de kalır.
2. CHG BYP varsa unlock/details/off işlemi **APCMD yanıtından önce** çalışır.
   Bu işlem politika işi veya MUIC yanıtı beklemez. Sonradan USBC okuması ya
   da opcode yanıtı hata verse bile önceden alınmış BYP kaybolmaz.
3. Gerçek APCMDRES kuyruğun başını tamamlar ve sonraki komutu gönderir.
4. WATER, gerçek CCSTAT (ve stok VBUSDet detach recheck), gerçek PD DATAROLE
   mevcut canlı register'ları okuyarak aynı pasif ISR'de işlenir. Eski foto
   snapshot'ı rol kararı yerine kullanılmaz.
5. `PolicyWork` çalışma zamanında yalnız kayıtları foto günlüğüne geçirir ve
   registry yayımlar. CC/PD/BYP'yi tekrar dispatch etmez. Başlangıç probe işi
   aynı work item'da kalır; probe yanıt bekleyişi pasif ISR'yi kilitlemez.

Bu, bütün Samsung nested handler'ları port edildi demek değildir. SYSMSG,
PDMSG, PS_RDY, VCONN/accessory/alt-mode ve batarya bildirimlerinin kalan
kolları henüz aynı kapsamda uygulanmadı.

Kaynaklar: [Samsung MFD IRQ tablosu](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L75),
[CC/MUIC/USB bildirimleri](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L190),
[PD DATAROLE](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c#L1260).

### MUIC komut ömrü ve gerçek tamamlanma

`B2lMaxRequestMuicLocked()` `06 <path>` ve `05` read-back komutunu tek
kilit altında peş peşe bağlar; `STATUS_PENDING` döner. Yalnız gerçek
APCMDRES, `0x51` yanıtını tüketir. `MuicPathVerified` ancak hedef yol ile
read-back eşleştiğinde true olur. Mevcut doğrulanmış yol veya aynı bekleyen
istek gereksiz tekrar komut üretmez.

Windows nesne ömrü için context içinde 32 sabit istek çifti vardır. Kuyruk
callback döndükten sonra stack adresi tutmaz, IRQ içinde bellek ayırmaz.
WRITE ardından READ tamamlanmadan slot yeniden kullanılamaz. Her yeni yol
isteği bir nesil numarası taşır: eski `USB_AP` read-back'i daha yeni `OPEN`
isteğini doğrulayamaz. Eski ham okuma `MUIC_CTL1` olarak görülebilir ama
`MUIC_OK=0`, `MUIC_NT=103` kalır. Slot kapasitesi dolarsa önceki istekler
başarısız iptal edilir; detach sessizce düşürülüp eski attach kuyruğu
çalışmaya bırakılmaz. Bu kapasite donanım register'ı değil Windows bellek
sınırıdır; tükenme gerçek hata olarak yayımlanır.

Su durumu, tamamlanma callback'inde de veri-rolü sonucunda korunur: su
olayı eski isteğin yanıtından önce geldiyse başarılı analog switch read-back'i
`DATA_NT` engelini başarıya çeviremez. UFP'de OPEN tamamlanması hâlâ
`DATA_NT=STATUS_NOT_SUPPORTED` olur; function stack tamamlandı denmez.

### Süre sınırı ve kilit sahipliği

`MfdLock` pasif MAX ISR, opcode kuyruğu ve CC/PD/charger durumunu korur.
`TransportLock` bağımsız HSI2C aktarım çiftini; `BusLock` probe, shutdown ve
tanı yayımlayan işin ömrünü seri hale getirir. Runtime nested handler
`BusLock` almaz, APCMD completion beklemez ve registry yazmaz. Registry
alanları MFD kilidiyle kopyalanır, kilit bırakıldıktan sonra yayımlanır.
Foto günlüğü work item'a aittir; kernel stack'e büyük log kopyası eklenmedi.

Yanıtı bekleyen bir runtime thread kalmadığından Samsung'un 10*HZ opcode
süre sınırı, pasif **tek atımlık hata timer'ı** ile gözetilir. Timer:

- USBC status/yanıtı okumaz; `0x02..0x05` veya `0x51` tüketmez.
  Hata halinde yalnız GPIO maskesinin read/modify/write işlemi yapılır.
- SOURCE/DFP kararı, başarı veya sonraki opcode'u üretmez.
- Gerçek baş isteğin `SentAt` süresini denetler. Eski komuta ait gecikmiş
  callback yeni başı iptal edemez; yeni başın kalan süresine kurulur.
- Süre dolduğunda kuyruğu `STATUS_IO_TIMEOUT` ile iptal eder, GPIO'yu
  maskeler ve hatayı yayımlar. Bu, Linux SYSMSG/POR/WDT kurtarmasının
  uygulandığı anlamına gelmez; recovery hâlâ eksiktir.

Bu timer kesmenin alternatifi değildir. Linux kuyruğunun süre aşımı kontrolü
ile Windows callback ömrünü eşler; Linux'taki bütün retry/reset kararları
henüz port edilmedi. KMDF pasif timer periyodik değildir; shutdown, kilidi
bıraktıktan sonra timer'ın bitmesini bekler. IRQ'lar kapanmadan VBUS off ve
son OPEN komutu tamamlanır/negatif sonuçlanır; sonrasında kuyruk emekliye
ayrılıp HSI2C susturulur.
[Microsoft pasif IRQ](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/supporting-passive-level-interrupts),
[pasif tek atımlık timer sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-timers).

### Probe sırasındaki ek düzeltme

Önceki ilk CC/su/PD kontrolü opcode init ve GPIO unmask sonrasındaydı.
Artık handler kayıt/maskeleri ardından ilk CC (yalnız SINK), WATER,
AUTOIBUS kuyruğu, ilk PD veri rolü, normal PD/VDM probe maskeleri ve
54/0c/0b init kuyruğu gelir; sonra TOP USBC/GPA1 açılır. Böylece ilk durum
okuması gerçek kesmelerin ardından gelip yeni bir rolü eski probe işlemiyle
ezmez. Initial SOURCE yine VBUS açmaz. İlk SINK+DFP'de MUIC isteği kuyrukta
kalabilir; probe GPIO'yu açmadan yanıt beklemez. UFP/WATER tüketici engelleri
MFD taşıma hatasıyla bir tutulmaz.

[Özgün probe](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L3539),
[CC initial-detect](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L855),
[PD init](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c#L1389).
RID/short kontrolü ve diğer PD init işlemleri hâlâ ayrı açık maddelerdir.

### Tanı, doğrulama ve build

MAX INF `0.1.0.33`, `MAX FOTO v6` genel sayfası 40 satırdır. Yeni alanlar:

| Alan | Anlam |
|---|---|
| `MUIC_NT` | `103` kuyrukta/yanıt bekliyor; `0` son yolun read-back'i eşleşti; negatif NTSTATUS hata |
| `MUIC_Q` | Henüz tamamlanmamış WRITE/READ çifti sayısı; donanım IRQ sayısı değildir |
| `MUIC_OK` | Son istenen analog yol gerçekten doğrulandı mı |
| `DATA_NT` | Veri rolü tüketicisinin sonucu; WATER/UFP engeli analog yol başarısından ayrı |

Foto açıklaması CC SOURCE'u DFP'nin yerine koymaz. `MUIC=09/OK=1` yalnız
analog yolun kanıtıdır; OS host stack'i veya USB transferi kanıtı değildir.
115 registry adı/alanı, olay günlüğü ve TXT kısa özeti birlikte korunur.

Host testleri gerçek IRQ/CC/PD/opcode C fonksiyonlarını birlikte yürütür.
Hiç publication work item çalıştırmadan bekleyen MUIC sırasında
`BYP -> APCMD -> WATER -> CC -> PD` sırası; overcurrent off; eski yanıt/yeni
OPEN; read-back mismatch; WATER/yanıt yarışı; timer'da I2C/başarı olmaması;
slot tükenmesinde iptal; SINK+DFP/UFP/WATER probe kuyruğu; ham hata kaydı;
PnP shutdown ve 40 satırlık foto sınırı doğrulandı. IRQ, connection,
lifecycle, events, transport ve üç kaynak kontrolü olmak üzere sekiz suite
çalıştırıldı. **Bunlar WDK build veya telefon IRQ/USB kanıtı değildir.**

WSL yöntemi değişmedi; bu Mac'te WDK/UEFI/WIM build yapılmadı:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Flaş yapılacağı zaman `.30/.31` ACPI'yi de içeren aynı build'in
boot.img/system.img çifti kullanılmalı. Beklenen ölçüm: gerçek I2C/MAX IRQ ve
APCMD sayıları, güç/veri rollerinin ayrı değişmesi, MUIC `103 -> 0/OK=1`,
detach'te VBUS off + doğrulanmış OPEN. Flaş bu kaynak işlerinin önkoşulu değildir.

### Hedefte kalan işler

- Gerçek MAX Type-C rolünün UcmCx -> URS -> DWC3 host/function yaşam döngüsüne
  bağlanması; mevcut `.18` statik HOST başlangıcının kaldırılması.
- UFP'nin özgün MUIC/charger sınıflandırması ve Windows function yolu.
- SYSMSG POR/WDT/APCMD_FAIL; PDMSG/PS_RDY, power/data-role swap, hard-reset,
  VCONN/RID/short/alt-mode ve batarya/charger akışlarının kalan kolları.
- Linux PHY/USB3 akışının kanıtlı MMIO/clock/reset adımları; daha önce abort
  veren combo registerlarına doğrulanmamış erişim eklenmemesi.

Bu liste uygulanmamış parçaları başarılı saymamak içindir; kullanıcıdan yeni
flaş gelmesini bekleyen bir durdurma koşulu değildir.


## 5 Eylül 2026: `.32` — CC güç rolü, PD veri rolü ve charger olayları

`.31` commit'i `f01a40e` ile GPIO/opcode zinciri kaydedildikten sonra, flaş
beklenmeden CC/PD karşılaştırmasına devam edildi. Bu bölüm `.31` kalan-iş
listesinin aşağıdaki maddelerini günceller. **USB'nin bütünü tamamlanmadı:**
MAX rol bilgisini Windows role-switch/UcmCx/DWC3/xHCI yaşam döngüsüne bağlama,
SYSMSG/PD kurtarması ve function/USB3 tarafı açık işlerdir.

### Güç ve veri rolünün birbirinden ayrılması

Samsung `max77705_ccstat_irq_handler()` CC_STATUS0'dan güç rolünü çıkarır;
SOURCE geçişinde VBUS'u açar. Ayrı `max77705_datarole_irq_handler()` ise
PD_STATUS1 (`0x0d`) bit7'den UFP/DFP okur. CC güç rolü henüz atanmadıysa
veri-rolü olayını işlemez; initial-detect yine yalnız SINK içindir.
`max77705_notify_dr_status()` DFP için MUIC RpRd=1/attach bildirir;
çalışan Linux kaydında bu OTG eşleşmesi `CONTROL1_WRITE 06 09` üretir.

`.32` bu ayrımı uygular:

- CC SOURCE artık yalnız VBUS/power-role işlemi yapar. MUIC USB_AP doğrudan
  SOURCE dalından çıkarıldı; SOURCE, DFP yerine kullanılamaz.
- Gerçek PD DATAROLE interrupt biti5 geldiğinde `0x0d` yeniden okunur.
  PowerRoleValid olmadan DFP atanmaz. DFP doğrulanınca MUIC USB_AP kuyruk
  komutu/read-back'i çalışır. Güç rolü SINK iken DFP de ayrı, geçerli bir
  kombinasyondur; veri rolünden VBUS kaynak rolü çıkarılmaz.
- SOURCE -> SINK güç geçişi OTG'yi kapatır, mevcut veri yolunu sırf güç rolü
  değişti diye OPEN yapmaz. Gerçek disconnect veri rolünü de geçersiz kılar.
- CC işlemi artık önce uzun charger/BC/status tanı okumalarının tamamlanmasını
  beklemez. Kritik CC okumasını güç eylemi izler; kesmenin sekiz baytlık
  gözlem kaydı zaten MFD'de tutulur.

Kaynaklar: [CCSTAT](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L539),
[DATAROLE](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_pd.c#L1260),
[MUIC/USB bildirim ayrımı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L190).

**UFP henüz çalışan function bağlantısı değildir.** Gerçek UFP değeri tanıda
korunur; eski host MUIC yolu OPEN'a alınır ve `DATA_NT=STATUS_NOT_SUPPORTED`
döner. UFP'nin MUIC charger sınıflandırması ve Windows function stack'i
uygulanmadığından bu yol başarı diye gösterilmez. Bu durum mailbox hatası
sayılıp MAX GPIO maskelenmez; sonraki gerçek DFP/CC olayları işlenebilir.
Bu geçici açık sınırın kaldırılması, tam rol zinciri çalışmasının parçasıdır.

### Su ve BYP aşırı akım

CC init ve VCONNSC bit4 olayında CC_STATUS1 bit1 ile WATER/DRY okunur.
Linux `notify_dr_status()` gibi WATER iken yeni MUIC/USB attach uygulanmaz.
`DATA_NT=STATUS_DEVICE_NOT_READY` ile bu engel ayrıca görünür. Battery/charger
su bildiriminin tamamı ve aksesuar politikası henüz port edilmiş değildir.

Gerçek CHG_INT BYP bit0, charger unlock -> DETAILS_02 (`0xb5`) okumasını
çalıştırır. Samsung başlığında BYP_DTLS maskesi `0x0f`, shift=0; bit0 aşırı
akımdır. OTG açıkken bu bit görülürse OTG kapatılır; charging alanları mevcut
masked-update/off yolu ile korunur. `OvercurrentEvents` artar. Linux'taki
`current_pr` davranışı gibi, aynı SOURCE rolünün tekrar kesme üretmesi OTG'yi
yeniden açmaz. Yeni detach/güç-rolü geçişi ayrı işlenir. Başarısız off/read
sonucu donanım kapandı diye kaydedilmez.

BYP, politika işinde CC/PD eylemlerinden önce işlenir; mailbox hatası daha
önce kaydedilmiş olsa da tüketilmiş BYP olayı atlanmaz. **Tam threaded-MFD
sıralaması henüz aynı değildir:** APCMD yanıtı pasif ISR'de sonuçlandırılırken
BYP/CC/PD politika işine aktarılır. Senkron MUIC isteğini bekleyen politika
işi BYP eylemini geciktirebilir. Bu, kalan asenkron notifier/politika portunda
çözülmesi gereken somut bağımlılıktır; “bütün nested handler'lar aynı” denmez.
[Samsung BYP handler](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/battery_v2/max77705_charger.c#L2034),
[BYP bit tanımı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/battery_v2/include/charger/max77705_charger.h#L109).

### Linux'un VBUSDet detach kontrolü

PASS2+ MFD, gerçek VBUSDet interrupt'ı geldiğinde BC_STATUS ve CC_STATUS0
okur. VBUS low + No Connection ise CCSTAT detach işlemini tekrar dispatch
eder. `.32` aynı koşulu ayrı `DetachRecheck` alanıyla uygular. **Ham CC_INT
baytı değiştirilmez**, yeni donanım interrupt'ı veya SOURCE üretilmez;
`DetachRechecks` sayacı bunun Linux kuralından geldiğini ayırır. BC/CC okuması
başarısızsa bu karar üretilmez. VBUS high veya SOURCE iken recheck yoktur.
[Özgün MFD koşulu](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L324).

### Foto ve test sonuçları

MAX sürümü `0.1.0.32`, `MAX FOTO v5` genel sayfası 40 satır:
`PWR_ROLE` 1=SINK, 2=SOURCE, ff=atanmamış; `DATA_ROLE` 0=UFP, 1=DFP,
ff=atanmamış; `DATA_NT` son veri-rolü işleminin sonucu. Bunlar controller'ın
başladığı veya USB transferinin tamamlandığı anlamına gelmez. Water, PD ham
baytı, rol değişimi/BYP/recheck sayaçları kısa TXT özetinde/registry'de korunur.
CHG-only kesmede yapılmayan USBC snapshot okuması artık `FAIL` sayacını
artırmaz; gerçek başarısız snapshot hâlâ `v=0` ve NTSTATUS ile kaydedilir.

Gerçek C fonksiyonlarıyla host testlerinde doğrulananlar: initial SOURCE'un
VBUS/DFP başlatmaması; güç rolü yokken PD olayının host açmaması; SOURCE
sonrası gerçek DFP ile MUIC açılması; SINK+DFP; PR değişiminde veri yolunun
korunması; detach; UFP'nin açıkça desteklenmeyen sonucu; WATER kapısı; BYP
off ve aynı SOURCE'tan kendiliğinden yeniden açılmama; başarısız I2C/off
sonuçları; BYP -> WATER -> CC -> PD politika sırası; mailbox hatasında BYP ve
kayıtların korunması; VBUSDet recheck'in raw interrupt'ı değiştirmemesi.
Foto satır genişliği/yüksekliği, IRQ/transport/lifecycle ve kaynak kontrolleri
de çalıştırıldı. **Bunlar WDK/WSL build veya cihaz testi değildir.**

WSL build yöntemi değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

`.30/.31` ACPI değişikliklerini de içeren aynı build'in boot.img/system.img
çifti gerekir. Flaş sonrası gerçek zincirde I2C_GSIV=72, MAX_GSIV=33 ve
artan gerçek IRQ/OP_DONE; ardından CC SOURCE, PD DFP, MUIC=09 beklenir.
Bu ölçüm kaynak işlerinin önkoşulu değildir. Statik `.18` HOST başlangıcını
MAX veri rolüne bağlayan işletim sistemi yaşam döngüsü sıradaki açık iştir.


### Sonraki rol bağlantısının doğrulanan Windows sözleşmesi

Önceki URS kodu (`4124852` içindeki `b2lusb.c`), kullanıcı HOST isteğini
`UrsHardwareEventIdGround` olarak raporluyordu. Bu gerçek MAX Type-C olayı
olmadığından yeniden kullanılacak bir bildirim yolu değildir. Microsoft'un
Type-C sözleşmesinde connector client CC/PD sonucunu UCM'ye bildirir;
UCM rolü URS'ye iletir. URS eski host/function stack'i kaldırıp uygun yeni
stack'i başlatır. ACPI `_ADR=0/1` bu stack'lerin controller rolleriyle
ilişkilendirilmesidir; tek başına sahte donanım değildir. Eski belgelerdeki
“sentetik PDO” ifadesi, URS'nin bütününün hatalı olduğunu kanıtlamaz.

Ancak önceki Code 10 ölçümünde farklı Windows build'ine ait UrsCx runtime
kullanılmıştı. Yeni port, bu yabancı runtime'ı geri getirmemeli; hedef WIM'in
kendi sürümündeki gerekli bileşenleri doğrulamalıdır. Mevcut statik parent'ın
PrepareHardware/D0Entry içindeki HOST çağrılarını yalnız bir registry
bayrağıyla kapılamak da OS'nin gerçek remove/start sırasının yerine geçmez.
Bu bölüm bir uygulanmış URS/UCM değişikliği değil, sonraki portun doğrulanmış
bağımlılığıdır.
[Microsoft Type-C bring-up](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/dual-role-controller-bringup-for-a-usb-type-c-system),
[rol/ACPI stack sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture).

## 5 Eylül 2026: `.31` — MAX77705 özgün GPIO kesmesi ve APCMD kuyruğu

Bu bölüm `.30` durumunu günceller. **Flaş beklenmeden kaynak çalışmasına devam
ediliyor.** `.31` gerçek MAX GPIO/MFD ve opcode taşıma adımını ekler; USB'nin
çalıştığı veya Linux akışının tamamlandığı iddiası değildir. Son telefon kanıtı
hâlâ `.28` fotoğraflarıdır; `.29`–`.31` WSL/WDK ve telefon doğrulaması yapılmadı.

### Kaynak sahipliği ve iki ayrı kesme

| Katman | Özgün kaynak | Windows uygulaması |
|---|---|---|
| HSI2C45 aktarımı | SPI82 -> GSIV114 (`0x72`) | `.30` DIRQL ISR/DPC; FIFO yalnız bu ISR'den ilerler |
| MAX77705 olayları | GPA1-6 -> SPI19 -> GSIV51 (`0x33`) | `.31` ayrı pasif ISR; TOP ve USBC read-to-clear registerlarının tek çalışma zamanı sahibi |
| GPA1/EINT | ALIVE `0x15850000`, GPA1 bankı `0x20`, EINT offset `4` | BYN0004 beşinci MMIO sayfası; yalnız pin6 alanları değiştirilir |
| USB denetleyicisi | xHCI GSIV273 | Ayrı inbox USBXHCI kaynağı; MAX/HSI2C kesmelerinin yerine geçmez |

ACPI BYN0004 artık beş MMIO ve sırasıyla HSI2C/MAX olmak üzere iki interrupt
verir. Eksik, çift, ters sıralı, edge/MSI interrupt veya yanlış MMIO reddedilir.
Her KMDF interrupt enable callback'i kendisine atanmış translated vector'ü de
kontrol eder. GPA1 pin6 için FUNC=f, PUD=0, DRV=0; EINT CON `0x704` alanı LOW=0,
MASK `0x904`, PEND `0xa04` bit6. Fiziksel aktif-düşük GPIO, EINT tarafından
GIC'ye aktif-yüksek SPI olarak sunulur. Unmask öncesi yalnız bit6 W1C ack yapılır.
GIC route/EOI Windows HAL/KMDF'nindir; sürücü GIC dağıtıcısına yazmaz.

`pinctrl-samsung.c:1157` içindeki `bank->eint_base = virt_base[0]`, EINT
registerlarının gerçekten ALIVE MMIO tabanına göre olduğunu doğrular.
[Samsung bank tabanı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-samsung.c#L1157),
[EINT mask/ack](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-exynos.c#L64).

### Kuyruk, kilitler ve probe sırası

Bağlantı timer'ı ve APCMD polling kaldırıldı. Politika işi başlangıçta ve gerçek
MAX ISR'den kuyruğa alınır. `BusLock` politika durumunu, `MfdLock` MAX ISR/opcode
kuyruğunu, `TransportLock` tek I2C write/repeated-START/read çiftini korur.
Pasif MAX ISR `BusLock` almaz; bağımsız HSI2C ISR/DPC ile I2C bekleyebilir.
Opcode bekleyicisi `MfdLock` veya `TransportLock` tutmaz. Böylece isteğin
beklenmesi, yanıt kesmesini kilitlemez. KMDF automatic serialization kapalıdır.
[Windows pasif IRQ sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/supporting-passive-level-interrupts).

1. GPA1 maskeli tutulur. MFD TOP/nested maskeleri hazırlanır; TOP CHG bit0
   açılır, USBC bit3 henüz maskelidir.
2. USBC nested handler yazılım maskesi `00/20/00/f0` olarak uygulanır.
3. PD-init AUTOIBUS `57 01` kuyruğa girer; yalnız kuyruk başı donanıma yazılır.
4. Normal probe donanım maskeleri `00/20/1c/ff` yazılır. `54 00`, `0c 1f`,
   `0b` istekleri kuyruğa eklenir.
5. Probe sonunda TOP USBC bit3 açılır; GPA1 unmask edilir. Bundan **önce**
   yanıt beklenmez. Yalnız gerçek APCMDRES kesmesi `0x51` yanıtını okur,
   ilgili isteği sonuçlandırır ve sıradaki komutu gönderir.
6. Gerçek yanıtlar/read-back'ler alındıktan sonra probe-sonu CONTROL3/CC-open
   işlemi de aynı kuyruktan yürür.

**Maskelerle ilgili eski yorum düzeltildi:** `PD=1c, VDM=ff` normal probe
sonundaki donanım yazılarıdır. `REG_*_INIT=00/20/00/f0` reset-kurtarma yolunda da
kullanılır; bunları her normal probe sonunda tekrar yazmak kaynakla aynı
akış değildir. Linux `irq_masks_cur` yazılım maskesi, doğrudan probe register
yazılarından ayrı kalır. `.31` normal probe maskelerini korur; alt-mode/PD'nin
sonraki gerçek maske geçişleri ayrıca tamamlanmalıdır. Bu yüzden eski
`UsbcRuntimeMasksConfigured` bayrağı normal probe'da sahte biçimde 1 yapılmaz.
[Samsung probe](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L3510),
[MFD dispatch](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L293).

Gönderilmiş opcode için Samsung genel kuyruğundaki 10 saniyelik deadline
kullanılır; kayıp IRQ'da register taraması yoktur. Timeout/hatalı I2C/uyuşmayan
cevap bütün bekleyen stack isteklerini kilit altında negatif sonuçla ayırır.
Geç IRQ silinmiş isteğe erişemez; unsolicited APCMD hiçbir isteğe başarı vermez.
**Bu hata yolu Linux kurtarmasının tamamı değildir:** Linux genel opcode
uyuşmazlığını loglayıp alınan türe göre dispatch edebilir; SYSMSG WDT/POR ve
APCMD_FAIL farklı kurtarma yollarıdır. `.31` bunları başarı gibi kabul etmez,
MFD hatasını açıkça saklar. Tam kurtarma portu hâlâ gereklidir.

### CC başlangıç kontrolündeki ek düzeltme

`max77705_ccstat_irq_handler(CCIC_IRQ_INIT_DETECT)` yalnız SINK'i işler;
SOURCE dahil diğer durumlarda döner. Windows'taki tek seferlik başlangıç
okumasının SOURCE'dan VBUS açması kaldırıldı. SOURCE eylemi gerçek CCSTAT
(bit0) dispatch'inden gelir; CCPINSTAT (bit3) veya CCISTAT (bit2) yalnız başına
rol başlatamaz. `CC_SRC_EV` artık başlangıç okumasını değil işlenen SOURCE
CCSTAT olayını sayar. Pin/status tanısı gerçek snapshot'ta korunur.
[Samsung CC initial-detect/CCSTAT](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L539).

### Tanı, kapanış ve doğrulama

`MAX FOTO v4` genel sayfası 37 satırdır. `MAX_GSIV=33`, `MAX_IRQ`, `MAX_NT`,
`OP_DONE`, `MFD_FAULT` eklendi; I2C_GSIV/IRQ/DONE ayrı kalır. Olay origin=6
gerçek MAX ISR örneğidir. Binary kayıt v1 korunur. Sekiz baytlık snapshot
başarısızsa `v=0`, gerçek NTSTATUS ve sıfır payload korunur. Sonradan APCMD
hata verse de önceden tüketilmiş read-to-clear baytları kaybolmaz; politika
hatalı kuyruğu da tanıya aktarır. 64 frame taşarsa sayaç ve negatif hata
üretilir. Registry yazımı ISR'de yapılmaz. `startnet.cmd` kısa özetinde de yeni
alanlar vardır; TXT dışarı aktarmak önkoşul değildir.

Kapanış, iş kuyruğunu durdurup tamamlanmasını bekler; OTG off/MUIC OPEN
I2C ve MAX kesmeleri açıkken yapılır. Sonra GPIO maskelenir, bekleyen istekler
ayrılır, HSI2C quiesce edilir. ReleaseHardware yalnız kaynakları unmap eder.
Pasif ISR ile politika işinin aynı WDF work item'ını tekrar kuyruğa alması
mümkündür; eşzamanlı callback'leri `BusLock` seri hale getirir.
[WDF work item kuyruğu](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemenqueue).

Yerel gerçek-C fixture kapsamı: GPIO alan sahipliği/W1C, doğru ve ters IRQ
kaynakları, üç komutun gerçek APCMD sırası, unsolicited/uyuşmayan/geç yanıt,
kayıp IRQ deadline, snapshot hatası, kuyruk taşması, init öncesi USBC maskesi,
probe maskeleri, kapanış, hata sonrası kayıt koruma, pin/akım olaylarının rol
başlatmaması ve initial SOURCE'un VBUS açmaması. Bunlar host fixture'lardır;
WSL/WDK derlemesi ve fiziksel kesme teslimi değildir.

### Hedefe kalan kaynak işleri — flaş önkoşulu yok

- PD DATAROLE/PSRDY ve CC power-role ayrı yürütülmeli; SOURCE tek başına DFP
  veya USB veri yolu bildirimi değildir. Mevcut MUIC SOURCE dalı bu ayrımı
  henüz tamamlamaz.
- BYP aşırı akımın nested charger eylemi, VBUSDet/No-Connection detach
  koordinasyonu ve su algılama bildirimi tamamlanmalı.
- SYSMSG/firmware mailbox kurtarması ve PD/VDM maske geçişleri port edilmeli.
- MAX -> USB role-switch -> DWC3/PHY -> xHCI start/stop zinciri kurulmalı.
  Mevcut `.18` parent hâlâ statik HOST başlatır; bu Linux eşdeğeri değildir.
- USB3 PHY ve function/device tarafı mevcut USB2 HOST profilinin dışındadır;
  özgün USB'nin tamamı için bunlar da bitmiş sayılmaz.

WSL yöntemi aynı: `bash yap.sh --touch --usb-test --clean`, ardından
`bash cache_esp.sh --win --system --baseline`. ACPI değiştiğinden aynı turdaki
`boot.img` ve `system.img` birlikte kullanılmalı. Bu Mac'te WDK/UEFI/WIM build
yapılmadı. Flaş yalnız fiziksel sonucu doğrulamak içindir; bu listedeki kaynak
çalışmalarını durdurmaz.


## 5 Eylül 2026: `.30` — HSI2C45 gerçek aktarım kesmesi

**Hedef değişmedi:** cihazın özgün USB olay/rol/güç akışını Windows'ta gerçek
kaynaklarıyla çalıştırmak. `.30`, bu hedefin HSI2C kesme adımını uygular;
MAX GPA1_6 kesmesi ve CC/PD -> DWC3/xHCI yaşam döngüsü henüz tamamlanmadı.
Telefonun son kanıtı hâlâ paylaşılan `.28` fotoğraflarıdır. `.29/.30` için
WSL build, gerçek GSIV teslimi ve USB bağlantısı cihazda doğrulanmış değildir.
Bu bölüm aşağıdaki `.29` durum tablosundaki HSI2C maddelerini günceller.

### Kaynak ve uygulanan aktarım sözleşmesi

Birleşik DT `hsi2c@15D60000` düğümündeki `<0 0x52 0>` SPI82 -> GSIV114
(`0x72`) kaynağı ACPI `USBC/BYN0004` `_CRS`'ye level/active-high olarak
eklendi. MAX'ın ayrı GPIO kesmesi GSIV51 ile karıştırılmıyor. KMDF nesnesi
atanan interrupt kaynağını kullanır; raw GSIV, translated vector, eksik/çift
kaynak ve edge/MSI uyumsuzluğu sürücü tarafından denetlenir. GIC yönlendirme
ve EOI işlemleri Windows HAL/KMDF'ye aittir; sürücü GIC dağıtıcısına yazmaz.
GIC SPI için Samsung sürücüsünün desteklediği seviye türü active-high'dır.
[Samsung GIC türü](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/irqchip/irq-gic-v3.c#L297)

`b2lmax77705_hw.c` içindeki eski pasif RX/TX polling döngüsü kaldırıldı:

1. Mesaj, register adresi, repeated START/STOP seçimi ve FIFO eşikleri
   interrupt kilidi altında hazırlanır. Buffer ve uzunluk MASTER_RUN'dan
   önce yayımlanır. TX preload yolu da kaldırıldı; Samsung IRQ yolundaki gibi
   FIFO'yu TX almost-empty kesmesi doldurur.
2. RX için almost-full + trailing, TX için almost-empty; ikisi için
   `TRANSFER_DONE` ve `CHK_TRANS_STATE=0xf00` açılır. Hata grubu artık
   polling dalındaki dar kontrolle sınırlandırılmaz.
3. Gerçek `EvtInterruptIsr`, enabled/pending kesişimini kontrol eder.
   ISR bir çağrıda en fazla 16 FIFO baytını işler; bekleme, registry veya
   MAX opcode çağrısı yapmaz. FIFO işleme sonunda status yeniden okunur;
   geç gelen hata DONE'a üstün tutulur. Captured status W1C ile temizlenir.
4. RX, bütün istenen baytlar **ISR tarafından** alındığında tamamlanır;
   ayrı DONE koşulu yoktur. TX, tüm veri kuyruğa konduktan sonra gerçek
   DONE ve boş FIFO ister. Tamamlanma durumu ISR'de kaydedilir; DPC yalnız
   `KeSetEvent` ile pasif bekleyiciyi uyandırır.
5. Bekleyici her uyanışta kilit altında mesaj durumunu kontrol eder.
   Eski/gecikmiş DPC yeni mesaja başarı veremez, toplam süreyi uzatamaz.
   Timeout ile ISR yarışında buffer ancak interrupt kilidi altında ayrılır;
   geç kesme önceki stack buffer'ına erişemez.
6. DT'deki `samsung,scl-clk-stretching` nedeniyle gerçek tamamlanmadan sonra
   master INIT durumu en fazla 100 ms beklenir. Bu, transfer başlamadan
   firmware'den kalan BUSY üzerinde bekleyen eski hatalı kapı değildir.
   INIT'e dönülmezse başarısızlık bildirilir.

Samsung kaynakta `EXYNOS5_I2C_TIMEOUT=msecs_to_jiffies(1000)` kullanır;
`.30` IRQ bekleme sınırı bu nedenle 1 saniyedir. `.29`'un 100 ms pasif FIFO
okuma sınırı sürdürülmedi. Bu değişiklik kayıp kesmeyi gizlemez: testte RX
FIFO'da sekiz bayt hazır olsa dahi IRQ teslim edilmezse **0 bayt / timeout**
sonucu alınır; hiçbir polling fallback bulunmaz. Mevcut hata sonrası controller
recovery/retry sınırı korunur.
[Samsung IRQ ve mesaj sırası](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L616),
[özgün timeout](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L206)

### Güç ve sürücü yaşam döngüsü

HSI2C saat/pin/denetleyici hazırlığı `D0Entry` içinde, donanım kesmesi
etkinleştirilmeden önce yapılır. Başarısız adapter hazırlığı PnP'ye hata döner.
MAX istemci/kimlik/opcode işi ancak `D0EntryPostInterruptsEnabled` sonrasında
başlar. Pasif işçi D0 girişinde artık IRQ hazır olmadan yarışamaz.

Kapanışta timer durdurulup çalışan callback'in bitmesi beklenir. OTG off ve
MUIC OPEN işlemleri `D0ExitPreInterruptsDisabled` içinde, HSI2C kesmesi hâlâ
çalışırken yapılır. Bu işlemler hata verirse hata saklanır ve callback'e
iletilir; başarısız OTG-off, yazılımla donanım kapandı şeklinde işaretlenmez.
`ReleaseHardware` kesme ayrıldıktan sonra yeni I2C mesajı göndermez.
[KMDF başlangıç sınırı](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry_post_interrupts_enabled),
[KMDF kapanış sınırı](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit_pre_interrupts_disabled),
[ISR/DPC iş bölümü](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/porting-interrupt-functionality)

### MAX olay kesmesinin sonraki bağımlılığı

Samsung pinctrl kaynağı tekrar incelendi. GPA1 bankının offset'i `0x20`,
EINT offset'i `0x04`; DT wakeup-controller türü `exynos7-wakeup-eint`.
Bu tanımlardan EINT CON/MASK/PEND register offset'leri `0x704/0x904/0xa04`
çıkar. Pin6 için CON alanı `[27:24]`, MASK/PEND biti `6`dır. Linux MAX IRQ
isteği `IRQF_TRIGGER_LOW | IRQF_ONESHOT`; pin EINT işlevi `0xf`, PUD/DRV `0`.
Linux seviye kesmesinde unmask'ten hemen önce yalnız ilgili pending bitini
W1C ile temizler. Diğer pinlerin maskelerini/pending bitlerini değiştirmek
özgün kaynak sahipliğini ihlal eder.
[9820 bankı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-exynos-arm64.c#L579),
[EINT mask/ack/unmask](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-exynos.c#L64),
[MAX IRQ isteği](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L449)

**Bunlar `.30`'da MAX EINT uygulanmış anlamına gelmez.** Sonraki değişiklikte
EINT/pin sahipliği, mask/ack ve MFD UIC/CC/PD/VDM dispatcher birlikte
bağlanmalı. Mevcut `B2lMaxEvtPollTimer` tüm servis/opcode çağrısı boyunca
`BusLock` tutuyor; opcode ise APCMDRES'i okuyarak bekliyor. Ayrı MAX IRQ
işleyicisine aynı kilidi aldırıp bekleyiciyi değiştirmemek, yanıtın gelmesini
engeller. Gerçek IRQ portu bu kilit/yanıt düzenini de değiştirmelidir.
HSI2C IRQ'sının çalışması tek başına bu MAX olay portunu sağlamaz.

### MAX portundan önce doğrulanan kuyruk/probe bağımlılığı

`max77705-irq.c:400–476` ve `max77705_usbc.c:3510–3585` birlikte
okunduğunda, mevcut Windows senkron init zincirini aynen koruyup yanına
bir GPIO ISR eklemenin Linux sırasını sağlamadığı görülüyor:

1. MFD, nested kaynakları başlangıçta maskeler. TOP INTSRC_MASK alt dört
   biti maskelenir; gerçek GPIO threaded IRQ kaydedilir. MFD init sonunda
   CHG üst kaynağı açılır, USBC üst kaynağı maskeli kalır.
2. USBC probe; APCMD/SYSMSG/VDM, MUIC, CC ve PD handler'larını kaydeder.
   PD init AUTOIBUS isteğini gönderme/yanıt kuyruğuna alır; probe maskeleri
   `PD=1c / VDM=ff` ve init opcode'ları bundan sonra hazırlanır.
3. `max77705_usbc_opcode_write()` senkron APCMDRES polling yapmaz.
   Gönderme ve beklenen yanıt düğümlerini `op_lock` altında kuyruğa koyar.
   İlk komut gerçekten yazılabilir; kilit yanıtı bekleyerek tutulmaz.
4. Probe sonunda `cc_booting_complete=1` yapılır ve TOP maskesinin yalnız
   USBC biti (bit3) temizlenir. O ana kadar bekleyen donanım olayı böylece
   gerçek GPIO/MFD dispatch yoluna girebilir.
5. `max77705_apcmd_irq()` yanıt düğümünü işler. Kuyrukta kayıtlı uzunlukla
   okunan yanıtın opcode'u beklenen değerle karşılaştırılır ve yanıt türüne
   göre işlenir; ardından sıradaki komut gönderilir. CC/PD olayları
   aynı MFD handler'ında grup maskeleri uygulandıktan sonra dağıtılır.
   Probe-sonu CC-open isteği de bu kuyruğu kullanır.

Kaynakta opcode uyuşmazlığı her durumda sert hata değildir: genel durumda
loglanır ve alınan response türüne göre dispatch sürer; FW_OPCODE_CLEAR için
ayrı dönüş vardır. Bunu “Linux bütün uyumsuz yanıtları reddediyor” şeklinde
özetlemek yanlış olur. Windows portunun komut/yanıt sahipliği bu ayrıntıyı da
açıkça ele almalıdır; eski bir cevabı yeni isteğin başarısı sayamaz.

Windows'taki `.30` `B2lMaxExchangeOpcode()` hâlâ 300 kez 10 ms bekleyen
eski polling yoludur; bunun Linux IRQ kuyruk portu olduğu söylenemez.
Samsung genel opcode kuyruğundaki `MAX77705_MAX_APDCMD_TIME` **10*HZ**'dir;
bu, mevcut 3 saniyelik polling sınırıyla veya HSI2C adapterinin 1 saniyelik
mesaj sınırıyla aynı mekanizma değildir. Kuyruk temizleme, gecikmiş yanıt,
SYSMSG kurtarma ve yeni komut eşleşmesi de birlikte port edilmelidir.

Dolayısıyla sıradaki uygulama sınırı yalnız ACPI'ye GSIV51 eklemek değildir:
pin/EINT sahipliği + MFD dispatch + opcode gönderme/yanıt kuyruğu + probe
maskeleri aynı değişiklikte tutarlı olmalıdır. Başlangıç sırasını değiştiren
erken TOP unmask veya kayıp IRQ yerine timer ile register temizleme, bu
bağımlılığın çözümü olarak kabul edilmiyor. Windows'ta pasif IRQ işleyicisi
I2C bekleyebilir; ancak yanıtı bekleyen politika işiyle aynı kilidi tutamaz.
HSI2C aktarım kilidi yalnız tek I2C işlem çiftini korumalı, CC/PD politika
kilidi ve opcode kuyruğu kilidi ayrı sahiplikler olmalıdır.

Kaynaklar: [MFD IRQ init](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L400),
[opcode kuyruk yürütücüsü](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L2131),
[APCMD handler](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L2802),
[probe-sonu USBC unmask](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L3576),
[Windows pasif IRQ sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/supporting-passive-level-interrupts).

### Tanı ve doğrulama

MAX sürümü `0.1.0.30`, foto arayüzü `MAX FOTO v3`; USB foto sayfası v2 ve
binary olay şeması v1. Genel sayfa 32 satırdır. Yeni alanlar:

- `I2C_GSIV`: PnP raw kaynağı; beklenen `00000072`.
- `I2C_IRQ`: enabled/pending kaynağı gerçekten işleyen ISR sayısı.
- `I2C_DONE`: ISR'nin sonuçlandırdığı mesaj sayısı; hatalı mesajları da
  içerir, tek başına başarı değildir. STATUS/RX_NT ile birlikte okunur.

Translated vector ve son interrupt status ayrıca registry'de saklanır.
`I2C_IRQ=0`, veri hazır olsa dahi gerçek kesme tesliminin kanıtlanmadığını
söyler; artan sayaçlar da USB enumeration kanıtı değildir.

Yerel kontroller geçti: gerçek C ISR/DPC/transfer fonksiyonlarıyla 1–64 bayt
RX/TX, trailing, hata + DONE, FIFO sırasında geç NACK, masked status, kayıp
IRQ, 3/8 RX, eksik TX DONE, geç/eski DPC, sabit deadline, güç kapatma ve
clock-stretch; gerçek Prepare/D0/Release callback'leriyle kaynak ve güç
sırası; mevcut CC/opcode/olay/foto/log testleri ve üç kaynak güvenlik testi.
Bu host fixture'ları fiziksel GIC teslimi ve WDK derlemesi değildir.

WSL yöntemi değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

ACPI de değiştiği için aynı turdan **boot.img ve system.img birlikte** gerekir.
Cihazda ilk beklenen kanıt `I2C_GSIV=72`, artan IRQ/DONE, başarılı kimlik ve
`v=1/nt=0` snapshot'lardır. Hedefin tamamlanması için hâlâ özgün MAX/CC/PD
kesmeleri, gerçek rol/güç bildirim zinciri, USB enumeration/veri aktarımı ve
takma/çıkarma çevriminin cihaz kanıtı gerekir. Hedef aktif kalır.


## 5 Eylül 2026: `.28` fotoğraf sonucu ve `.29` kaynak düzeltmesi

**Güncel cihaz kanıtı artık `.28` olay tanısını içeren flaştır.** Kullanıcının
12 fotoğrafı incelendi. Bazıları aynı çıktının devamı/tekrarıdır; 12 ayrı
başarılı test değildir. Olay ekranlarının iki turunda da `D0=448517796`
bulunur: aynı oturumda sayaç 13'ten 15'e ilerlemiştir. MAX INF sürümü bu
fotoğraflarda görünmediği için `.28` tanımlaması olay şeması ve `MAX FOTO v1`
çıktısına dayanır; host INF `0.1.0.18` açıkça görünür.

**Çalışma ölçütü:** Linux/Android'in gerçek donanım ve olay sırası uygulanacak.
Bir aşama başka bir aşamanın başarılı görünmesiyle tamamlanmış sayılmayacak.
Özellikle pin aktifliği SOURCE rolü değildir; `STAGE=70` USB hazır anlamına
gelmez; `_DEP` MAX bağlantı durumunu beklemez; xHCI `PP=1` konektörde VBUS
ölçümü değildir. Bu bölüm alttaki tarihsel `.27/.28` planlarının önüne geçer.

### Fotoğrafların söylediği ve söylemediği

| Kanıt | Okunan değer | Yorum |
|---|---|---|
| Fotoğraf 1/3/4/5, host | `ACPI\BYN0003\0 Started`, `USBXHCI`, MMIO `10c00000..10c0ffff`, raw IRQ `273` | PnP start başarılı. Root-hub/alıcı/HID enumeration kanıtı değildir. |
| Fotoğraf 3/5, kaynak ve DMA tanısı | START completion `0`, DMA64 ve DMA32 buffer/enabler status `0`; raw IRQ `111`, translated vector `a00`, level `a` | Kaynak ataması ve buffer ayırma başarılı. Gerçek xHCI interrupt teslimini veya tamamlanan bir USB transferini kanıtlamaz. |
| Fotoğraf 4/6/7, controller | `CMD=2005`, `STS=0`, `CFG=40`, `DCBAAP=000000097feb0000`, `CRCR=0`; observer `161` örnek, `0` transition | Controller çalışır durumda görülmüş. CRCR sıfır okunmasını tek başına “command ring kurulmamış” diye yorumlamıyoruz. |
| Aynı fotoğraflar, port | USB2 `000002a0`, diğer port `0e0002a0`; `CCS=0`, `PED=0`, `PP=1`, `RxDetect` | Fiziksel USB bağlantısı xHCI tarafında görünmüyor. |
| Fotoğraf 2/6/7/10, MAX init | `STAGE=70`, `STATUS=0`, PMIC `15/rev2`, HW/FW `1a/1a`, minor `10`, maskeler `00/20/00/f0`, `CCCTRL1=1f` | Kimlik, kısa I2C okumaları ve init/read-back işlemleri tamamlanmış. Bütün I2C uzunluklarının çalıştığı sonucu çıkarılamaz. |
| CC-open | `CCOPEN_OK=1`, CONTROL3 `64 -> 64` | LockErrorEn zaten kapalı; bu önceki hipotez tekrar doğrulanmıyor. |
| Genel ekran tur 1 / tur 2 | CC `80/00 -> 00/00`; örnek sayısı `aca -> b6b`; rol/geçiş/source sayaçları `0` | CC2 pin durumu değişiyor; güç rolü örnekleri hâlâ No Connection. |
| Bağlantı yolu | MUIC `3f`, OTG SW/HW `0/0`, charger mode `15` | SOURCE görülmediğinden güç ve veri yolu açılmamış. HID servislerini değiştirmek bu katmanı onarmaz. |
| Fotoğraf 4 | `'findstr' is not recognized...` | Boş root-hub/SetupAPI özetleri “aygıt/hata yok” kanıtı değildir. Özetleme aracı çalışmamış. |
| Fotoğraf 8/9/11/12 | `count=13 -> 15`, `FAIL=13 -> 15`, her kayıtta `v=0 nt=c00000b5` | **Bütün 8 baytlık status snapshot'ları zaman aşımına düşmüş.** Sıfırlanmış `r` alanları donanım verisi değildir. |

`UIC=B2`, SYSMSG interrupt biti `0x40` içermiyor. `SYS=0`, yakalanan olaylarda
bu bitin görülmediğini söyler; geçerli SYSMSG register okuması bulunmadığı
için “firmware hatası yok” denemez. `ERR=1`, tek ham AttachSrcErrI örneğidir;
CC maskesindeki bit 5 de set olduğundan Linux nested IRQ'sının çalıştığı veya
hatanın o anda sürdüğü çıkarılamaz. CC_STATUS1 bit 2'yi geçerli okumak gerekir.

### Aynı D0 oturumundaki olay zaman çizgisi

Zamanlar MAX D0 başlangıcına göre milisaniyedir. `p`, ayrı ve başarılı
politika CC okumasıdır; `r` bütün satırlarda geçersizdir.

| Sıra | t (ms) | Kaynak / opcode / IRQ | Politika CC |
|---:|---:|---|---|
| 1 | 2331 | baseline, `FF`, `00000000` | yok |
| 2 | 2675 | opcode öncesi, `57`, UIC `32` | yok |
| 3 | 3020 | opcode bekleme, `57`, UIC `80` | yok |
| 4 | 3850 | opcode bekleme, `54`, UIC `80` | yok |
| 5 | 4273 | opcode bekleme, `0c`, UIC `80` | yok |
| 6 | 4696 | opcode bekleme, `0b`, UIC `80` | yok |
| 7 | 5400 | opcode bekleme, `1c`, UIC `80` | yok |
| 8 | 5823 | opcode bekleme, `1c`, UIC `80` | yok |
| 9 | 6309 | normal CC, `00000000` | `0080` |
| 10 | 6872 | opcode bekleme, `06`, UIC `80` | yok |
| 11 | 7279 | opcode bekleme, `05`, UIC `80` | yok |
| 12 | 819188 | normal CC, `00000902` | `0000` |
| 13 | 820018 | normal CC, `00002802` | `0080` |
| 14 | 1532845 | normal CC, `00000802` | `0000` |
| 15 | 1533675 | normal CC, `00000100` | `0000` |

12–15 numaralı kayıtlar pin/CC olaylarının sonradan da geldiğini gösteriyor.
Fotoğraflarda fiziksel takma/çıkarma anları etiketli olmadığından hangi
satırın hangi elle yapılan işleme karşılık geldiği kesinleştirilmedi.
Hiçbir başarılı politika örneğinde `CCStat=2` yok. Genel ekrandaki son
`I2C_RETRY=0/I2C_PHASE=0`, önceki snapshot hatalarını çürütmez: son başarılı
kısa transfer bu alanları yeniden yazmıştır.

### Kodda doğrulanan iki hata ve `.29` değişikliği

**1. HSI2C hazır FIFO verisi her baytta uyunarak tüketiliyordu.**
`B2lMaxTransferMessage()` yalnız bir RX baytı alıp 1 ms bekleme istiyordu;
mesajın toplam sınırı 100 ms. Windows beklemesinin ardından thread'in hemen
çalışacağı garanti edilmez. Bu, hazır 8 baytın bile süre içinde alınamamasına
neden olabilir. [Windows bekleme sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kedelayexecutionthread)

Samsung interrupt işleyicisi hazır RX FIFO'yu boşaltır, TX FIFO'nun boş
yerlerini doldurur. Windows `.29` da hazır veriyi aynı ziyaret içinde
alır/yazar; yalnız FIFO boş/dolu olup ilerleme bekleniyorsa uyur. İstenen
uzunluk, mevcut 100 ms timeout, error kontrolleri ve tekrar sınırı korunur.
Timeout'u büyüterek ya da sıfır veri döndürerek başarı üretilmez.
[Samsung FIFO işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L616)

Gerçek transfer fonksiyonuyla yapılan host testi, ilk baytların ilk
zamanlayıcı dönüşünde geldiği 15,625 ms'lik gecikme modelinde eski kodun
**7/8 baytta timeout** verdiğini, yeni kodun tek FIFO boşaltımıyla okuduğunu
gösterdi. Bu telefonda ölçülmüş zamanlayıcı periyodu değildir; kaynak
hatasının yeniden üretimidir. Cihazdaki 8 baytlık hatanın bu düzeltmeyle
kapanması WSL build + flaşla doğrulanacaktır.

`RX_FAIL`, `RX_NT`, `RX_WANTED`, `RX_READ` tanıları eklendi. Başarısız fiziksel
okuma denemelerinin sayısını ve son hatada istenen/alınan bayt sayısını
saklar; sonraki başarılı kısa okumalar bunları silmez. D0 başlangıcında
sıfırlanır. Retry sayıldığı için RX_FAIL ile olay başına artan FAIL birebir
aynı sayaç değildir.

**2. Windows MUIC ve VBUS sırasını ters kuruyordu.**
Linux `max77705_ccstat_irq_handler()` gerçek `cc_SOURCE` dalında VBUS'u açar;
MUIC OTG/RpRd akışı ardından `06 09` gönderir. Windows'taki “önce MUIC
read-back, sonra VBUS” kapısı Linux'a ait değildi. `.29`, bu iki adımı
`CC_SOURCE -> VBUS/OTG -> MUIC USB_AP` sırasına aldı. VBUS çağrısı başarısızsa
MUIC dalına geçilmez. SOURCE dışına çıkınca güç kapatma ve OPEN yolu sürer.
[Samsung CC source dalı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L683)

Gerçek CC çözümleme ve bağlantı fonksiyonlarını çalıştıran testler; pin
aktifliğinin SOURCE'a dönüşmediğini, bütün source dışı durumları,
`CC -> VBUS -> MUIC` sırasını, detach'i ve hata/yeniden deneme yollarını
kontrol eder. **Bu sıra hatası tek başına mevcut CCStat=0'ın nedeni olarak
kanıtlanmadı.** Çalışan Linux logunun yerel kopyasındaki ilk gerçek host
bildirimi `461.183854`, USB_HOST_ATTACHED `461.183916`, MUIC seçim kararı
`461.185836`dır; erken CC/VBUS zamanları aşağıdaki tarihsel tablodan gelir.

### Tanı ekranı ve toplama düzeltmeleri

- Beş `findstr` çağrısı kaldırıldı. Aynı WIM'de bulunan `b2lusbctl filter`
  büyük/küçük harf duyarsız sabit metin eşlemesi yapar. BOM'lu UTF-8/UTF-16LE
  UCRT Unicode modunda, BOM'suz çıktı normal metin modunda okunur.
  Eşleşmeme, açma/okuma hatası, desteklenmeyen UTF-16BE ve aşırı uzun satır
  ayrı `LOG_FILTER_*` sonuçlarıdır; hata boş özet gibi sunulmaz.
  [UCRT BOM davranışı](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen)
- Tam MAX registry dökümü için 86 ayrı `reg.exe` yerine tek key sorgusu
  kullanılır. Kısa özetin 55 eski alanı ve dört yeni RX alanı korunur.
  Toplam toplama süresinin ne kadar azalacağı cihazda ölçülmedi.
- `MAX FOTO v2`: geçersiz olaylarda sıfır ham registerlar yerine
  `r=OKUNAMADI`; genel ekranda kalıcı RX hata bilgileri ve STAGE açıklaması.
- Yeni `USB FOTO v2` sayfası, mevcut salt-okuma GET_STATUS arayüzünden
  port CCS/PED/PP değerlerini ve PnP'den mevcut USB/HID kimliklerini gösterir.
  Yeniden tarama, host restart veya rol komutu göndermez. Her enumerator için
  ilk 6 kimliğin ilk 48 karakteri ve toplam sayı gösterilir. HID listesi
  başka giriş aygıtlarını da kapsayabilir; sayının artması tek başına USB
  fare kanıtı değildir, alıcının VID/PID'si aranır.
- Sayfalar 20 saniye kalır: genel MAX, canlı USB/PnP, sonra aynı snapshot'ın
  olay sayfaları. Tam 64 kayıtla tur 8 sayfa/160 saniyedir. MAX alanları,
  USB/PnP ve olay kaydı birbirleriyle atomik anlık görüntü sayılmaz.

### 16 aşamanın yeniden denetimi ve açık işler

| Aşama | `.28` kanıtı ve `.29` sonrası durum |
|---|---|
| 1. Sahiplik | MAX ve host ACPI aygıtları var; bu iki sürücünün başlaması donanım olay zincirini bağlamıyor. |
| 2. Platform | CMGP, USI ve pinctrl hazırlığı kimlik/kısa transferlerle doğrulanmış. |
| 3. I2C mesajı | **Kısmen çalışıyor.** Sekiz bayt timeout kanıtı nedeniyle “taşıma katmanı tamamlandı” kararı geri alındı; FIFO düzeltmesinin cihaz doğrulaması bekleniyor. |
| 4. Kimlik | PMIC/HW/FW okunuyor; firmware değiştirilmedi. |
| 5. Kesme | Read-to-clear register olayları var; gerçek HSI2C/MAX ISR teslimi uygulanmış değil. Aşağıdaki DT kaynak bağlantısı bulundu. |
| 6. Init | Init opcode/read-back'leri başarılı, fakat bütün firmware/FSM hata durumları dışlanmış değil; snapshot'lar geçersiz. |
| 7. Attach/rol | CC pin olayı var; `CC_SOURCE` yok. SOURCE'un yerine pin/Rp/Rd tahmini kullanılmaz. |
| 8. VBUS | Source dalı var; cihazda açılmadı. `.29` Linux'taki VBUS-önce sırasını düzeltir. |
| 9. MUIC | OPEN read-back başarılı; gerçek attach üzerinde USB_AP henüz cihazda doğrulanmadı. |
| 10. Rol bildirimi | **Eksik.** BYN0002 hâlâ sabit HOST başlatıyor; MAX CC/PD bildirimine bağlı başlama/durma sözleşmesi yok. `_DEP` bunun yerine sayılamaz. |
| 11. PHY/runtime PM | Host PHY registerları açık görülüyor; Linux'taki bağlantı temelli runtime resume/suspend bütünü doğrulanmış değil. |
| 12. xHCI | Started/RUN var. IRQ273, root-hub veya alıcı transferlerinin tamamlandığı anlamına gelmez. |
| 13. Port | CCS/PED sıfır; bağlantı henüz yok. Yeni foto sayfası sonradan takma/çıkarma sonrası güncel sonucu gösterir. |
| 14. Enumeration | Logitech USB kimliği bu Windows kanıtında yok. |
| 15. HID | Servislerin Start=3 olması yeterli değil; mevcut fare PnP sorgusu aygıt bulmamış. |
| 16. Detach | OPEN/OTG-off başlangıcı var; önce başarılı attach görülmediği için tamamlanmış bir detach çevrimi doğrulanmadı. |

**IRQ kaynağı konusunda önceki ifade düzeltilmiştir.** Depodaki birleştirilmiş
`mu-silicium-overlay/Resources/DTBs/beyond2lte.dts` içinde:

- `hsi2c@15D60000` interrupt tuple'ı `<0 0x52 0>`: SPI 82, GIC GSIV 114
  (`0x72`). Bu, Linux `/proc/interrupts` satırını Windows'a kopyalayarak
  değil, DT'nin SPI tanımından `+32` ile türetilmiştir.
- MAX `irq-gpio=<0x135 6 1>`; `0x135` GPA1 bankıdır. GPA1 interrupt
  listesinin 6. pini SPI `0x13`/19'a gider: GIC GSIV 51 (`0x33`). Bank,
  `pinctrl@15850000` altındadır; pin işlevi `0xf` olarak tanımlanmış.
- Bu bağlantılar **kaynak düzeyinde bellidir**; mevcut ACPI BYN0004 `_CRS`
  bunları henüz taşımıyor. EINT türü, maskesi, pending/ack, pinctrl sahipliği
  ve Windows interrupt yaşam döngüsü kurulmadan yalnız iki numara eklemek
  bir IRQ portu sayılmaz. Fiziksel MAX pininin active-low olması GIC uç
  polaritesini tek başına belirlemez.

Tam Linux eşdeğerliği için 5 ve 10. aşamalar açık kalır: gerçek interrupt
sahipliği/işleme, CC/PD veri-güç rolü ayrımı, MAX'tan DWC3'e olay bildirimi,
xHCI child'ın bağlantı/power yaşam döngüsü ve detach'te ters sıra gerekir.
`.29` bunların yapıldığını iddia etmez. Statik HOST, sürekli polling veya
registry'deki READY'yi okuyan bir kapı nihai Linux akışının yerine kabul
edilmez. Fiziksel attach tamamlanmadan host'u yeniden başlatmak, SOURCE
uydurmak, maskeli AttachSrcErrI'yi başarı olayı saymak veya tek SYSMSG örneği
üzerinden firmware resetlemek bu eksikleri kapatmaz. Linux SYSMSG işleyicisi
karar öncesi registerı üç kez eşit okuma koşuluyla değerlendirir; `.28`
geçersiz snapshot'ları böyle bir kurtarma kararını desteklemiyor.

### WSL build ve flaşta aranacak kanıt

MAX sürümü `0.1.0.29`, parent/host `0.1.0.18`; olay binary şeması v1 kalır,
foto arayüzü v2 olur. macOS üzerinde WDK/UEFI/WIM derlenmedi. Aynı WSL repo
kökünde normal yöntem:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Aynı turda üretilen `boot.img` ve `system.img` kullanılır. MAX `.sys`, kontrol
EXE'si ve startnet'in birlikte WIM'e girmesi gerekir. İlk kabul ölçütü
`MAX FOTO v2`, `RX_FAIL=0`, olaylarda `v=1/nt=0` ve gerçek ham registerlardır.
Bunlar beklenen sonuçtur, gerçekleşmiş test sonucu değildir. Hata sürerse
RX_NT/WANTED/READ ve ilgili olay sayfası birlikte fotoğraflanır.

USB işlevinin kabulü ayrıca gerçek SOURCE `2`, OTG SW/HW `1/1`, MUIC `09`,
USB portunda CCS/PED, USB kimliklerinde `VID_046D&PID_C52E`, ardından ilgili
HID arayüzü ve fiziksel fare hareketidir. Sonra çıkarma ile OTG off/MUIC OPEN
ve port detach görülmelidir. SOURCE hiç oluşmazsa sonraki adım geçerli
CC/SYSMSG/PD verisini yorumlamaktır; diğer aşamalar tamamlandı sayılmaz.
Klavye veya TXT dışa aktarımı önkoşul değildir.

Host doğrulaması: gerçek C fonksiyonlarıyla FIFO/timing/hata testi, CC/OTG/
MUIC sıralama testi, olay/registry/33 satır–64 sütun foto testi ve log filtresi
testi; mevcut üç kaynak güvenlik testi. Bunlar WDK derlemesi, GIC/IRQ donanım
ölçümü veya gerçek USB veri aktarımı yerine geçmez.

## Tarihsel: 4 Eylül kaynak incelemesi ve `.28` tanı tasarımı

### USB olmadan kanıt: otomatik fotoğraf ekranı

USB bağlantısı henüz çalışmadığı için TXT dışa aktarımı kullanıcı testinin
önkoşulu değildir. `.28` kontrol aracı güncellendi: `startnet.cmd`, otomatik
HOST testi sonunda `b2lusbctl.exe maxscreen` çalıştırır. Komut yazmak, klavye
bağlamak veya dosya çıkarmak gerekmez.

Ekranda önce **MAX FOTO v1 / GENEL DURUM**, sonra olay sayfaları görünür.
Her ekran **20 saniye** kalır; son sayfadan sonra yeni tur otomatik başlar.
Genel ekranda STAGE/STATUS, CC0/CC1, rol/geçiş/source sayaçları, CONTROL3,
MUIC ve OTG değerleri bulunur. Olay sayfaları en fazla 12 kayıt/33 satırdır;
mevcut 64 sütun × 41 satırlık konsola göre düzenlenmiştir. Her kaydın iki
satırında zaman, opcode, IRQ, politika CC örneği, snapshot geçerliliği/hatası
ve sekiz ham register baytı yer alır.

Bir turdaki bütün olay sayfaları aynı registry snapshot'ından gelir; yeni
olaylar bir sonraki turda alınır. 64 kayıt doluysa genel ekran + 6 olay sayfası,
yani yaklaşık **2 dakika 20 saniyelik** bir tur vardır. Genel ekrandaki alanlar
ayrı registry okumalarıdır; olay snapshot'ı ile atomik eşzamanlılık iddiası yoktur.
Bu ekran döngüsü test sonunda bilerek devam eder; donma değildir. Bir klavye
mevcutsa Q/Esc ile terminale dönülebilir, fakat fotoğraf testi bunu gerektirmez.
Görüntüleyici yalnız kullanıcı modunda uyur/registry okur; I2C veya PnP taraması
başlatmaz. Kernel MAX sürümü `.28` ve kayıt şeması v1 değişmedi; **yeni EXE ve
startnet içeren WIM** aynı WSL build yöntemiyle yeniden hazırlanmalıdır.

İstenecek kanıt artık **fotoğraflardır**:

1. A açılışında alıcı baştan takılıyken GENEL DURUM ve aynı turun bütün
   numaralı olay sayfalarını fotoğraflayın.
2. B açılışında alıcı ayrıkken GENEL DURUM ekranında `STAGE=00000070`,
   `STATUS=00000000` görülmesini bekleyin ve fotoğraflayın. Alıcıyı takın;
   tur numarası ilerledikten sonra GENEL DURUM ve bütün olay sayfalarını çekin.
3. Alıcıyı çıkarın; sonraki turda aynı ekranları tekrar çekin. Hangi fotoğraf
   grubunun takılmadan önce, takıldıktan sonra ve çıkarıldıktan sonra olduğunu
   belirtin. Sayfalardaki tur/D0 kimlikleri grupları eşleştirmeyi sağlar.

TXT dosyaları yerel yardımcı kayıt olarak kalır. Aşağıdaki terminal/TXT
komutları yalnız terminal veya kalıcı depolama erişimi mümkün olduğunda
kullanılabilecek ek yöntemlerdir; USB çalışmadan bunları dışarı çıkarma talebi yoktur.

**Son cihaz kanıtı hâlâ `.27`dir. `.28` aşağıda açıklanan gözlem değişikliğidir;
henüz WSL/WDK ile derlenmiş veya telefonda doğrulanmış değildir.** Alttaki
16 adımlı tablo ve zaman çizgisi `.27` devir kaydını korur. ForceError ve
bir sonraki değişiklik hakkındaki yorumlar için bu bölüm önceliklidir.

### Linux kaynak incelemesinin düzelttiği teşhis

Karşılaştırma, `SAMSUNG_ORIGINAL_USB_STACK.md` içinde sabitlenen
`ianmacd/d2s@d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39` üzerinden yapıldı.
Bu GPL referansının çalışan recovery çekirdeğiyle aynı derleme olduğu iddia
edilmez; canlı log ve kaynak incelemesi ayrı kanıtlardır.

- `max77705_ccpinstat_irq()` CC pin durumunu okur/kaydeder; `CC1_ACTIVE`
  görünce yazılımla `cc_SOURCE` üretmez.
- `max77705_ccstat_irq_handler()` önce `CC_STATUS0 & 7` değerini okur.
  Sıfır olmayan durum için `PLUG_ATTACHED` kaydeder; `cc_SOURCE=2` dalına
  girdikten sonra güç rolü ve VBUS işlerini yapar. Bu yüzden
  `PLUG_ATTACHED -> SOURCE` bir donanım programlama tarifi değildir.
  [CC işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L539)
- `max77705_set_CCForceError()`, incelenen USBC kaynağında manuel CC-open
  **etkinleştirme** dalından çağrılır. CC-open bırakma dalı yalnız
  `LockErrorEn` temizler. Normal attach için eksik bir ForceError çağrısı
  bulunduğu doğrulanmadı; önceki “attach sonrasında ForceError koordinasyonu”
  teşhisi geri çekildi.
  [CC-open iş akışı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L1017)
- Windows `.27` opcode bekleme yolu `UIC_INT` registerını komuttan önce ve
  cevap beklerken okuyup temizler; APCMDRES dışındaki bitleri bu yolda
  korumaz. `B2LMaxUicInterruptSeen` yalnız normal taramayı kapsadığı için
  bütün init olaylarının kaydı değildi. Windows ayrıca `USBC_STATUS2/SYSMSG`
  okumuyordu. Linux SYSMSG işleyicisi bu alanı okur; WDT/POR durumlarında
  ayrı kurtarma yolları vardır. `.28` bunları yalnız gözler; reset/kurtarma
  davranışı eklemez.
  [SYSMSG işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L2526)
- `.27` `CC_SEEN=0x6d` birikimi `AttachSrcErrI` bit 5'i de içerir. Ancak
  `CC_INT_MASK=0x20` aynı olayı maskeler; ham okunması Linux'ta nested ISR
  çalıştığını veya hatanın hâlâ aktif olduğunu kanıtlamaz. İlgili olayda
  `CC_STATUS1` bit 2 ile birlikte değerlendirilmelidir.
  [Register/bit tanımları](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/include/linux/ccic/max77705.h#L112)

Önemli kanıt sınırı: bu makinedeki `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
dosyasının AFTER_PLUGGED log bölümü `461.183842` ile başlıyor. Devirdeki
`461.169117 / PLUG_ATTACHED / cc_SOURCE` satırları bu kopyada bulunmuyor.
Toplama betiği filtrelenmiş logun son 300 satırını saklıyor; erken attach
kayıtları kesilmiş olabilir. Aşağıdaki milisaniye tablosunun erken bölümü
önceki devirden aktarılmıştır, bu yerel dosyayla yeniden doğrulanmış değildir.
MUIC `06 09`, host açılışı ve alıcının enumeration kanıtı dosyada mevcut.

Linux betiği önce alıcı ayrıkken, sonra takıldıktan sonra snapshot alıyor;
`.27` Windows testi ise alıcı baştan takılıyken yapıldı. Aynı alıcı kullanımı
aynı başlangıç durumu demek değildir. `.27` sayaçları, **başarılı politika
örneklerinde** SOURCE görülmediğini kanıtlar; init öncesi veya iki örnek
arasındaki bütün geçici donanım durumlarını dışlamaz.

### `.28` neyi kaydeder?

`b2lmax77705.sys=0.1.0.28`; parent ve host INF sürümleri `0.1.0.18` kalır.

| Olay `o` | Yakalama noktası | `irq` / `policy` anlamı |
|---|---|---|
| 1 | Başarılı kimlik probe'undan sonra, ilk init opcode'undan önce | IRQ okunmadı; `policy=ffffffff` |
| 2 | Opcode öncesi eski UIC olaylarını temizleyen okuma | Yalnız UIC geçerli; `op` sıradaki komuttur, olayın nedeni olduğu iddia edilmez |
| 3 | Opcode cevabı beklenirken sıfır olmayan UIC okuması | APCMDRES ile aynı bayttaki SYSMSG dahil bütün bitler korunur |
| 4 | Normal taramada ilk CC örneği, ham CC0/CC1 değişikliği veya sıfır olmayan child IRQ | Politikanın CC örneği ve dört IRQ baytı korunur |
| 5 | IRQ temizlendikten sonraki politika CC okuması başarısız | IRQ korunur; eski CC kullanılmaz, `policy=ffffffff` |

Her olayda 100 ns saat kaynağından zaman, sıra numarası ve `0x06..0x0d`
registerlarının sekiz baytlık salt okunur snapshot'ı saklanır:

```text
raw = USBC_STATUS1 / SYSMSG / BC_STATUS / FW_MINOR / CC0 / CC1 / PD0 / PD1
irq = VDM PD CC UIC                 (hex DWORD, yüksek bayttan düşüğe)
policy = CC1 CC0                    (yalnız o=4; diğerlerinde ffffffff)
v=1: raw geçerli                    v=0: raw yorumlanmaz; nt okuma hatası
```

Snapshot, IRQ ve politika örneğinden **sonra** okunur; atomik donanım kaydı
değildir. `policy` ile `raw` CC alanlarının farklı olması bir geçişi veya
örnekler arası farkı araştırmaya yarar. Snapshot SOURCE gösterse bile bu veri
politikanın yerine geçip VBUS açmaz. Tanı okuması başarısızsa `SNAP_FAIL`
artar; asıl opcode/politika çağrısının hata veya başarı sonucu korunur.
`o=5` yolunda ek I2C okuması yapılmaz; `nt` asıl politika CC okuma hatasıdır.
Bu durumda `v=0` ve `SNAP_FAIL` artışı, snapshot'ın alınamadığını belirtir;
mevcut taşıma hata ayrıntıları korunur ve `STAGE/STATUS` da yayımlanır.

Sabit 3.128 baytlık kayıt alanı D0 başına ilk 16 olayı saklar, sonraki 48
slotta son olayları döndürür. `overwritten`, son bölümde üstüne yazılmış olay
sayısıdır; sıra numaralarındaki boşluk bu kaybı görünür kılar. Yeni D0'da kayıt
sıfırlanır; `MAX_EVENT_START_100NS` oturumları ayırır. Sıfır UIC bekleme
örnekleri ve değişmeyen, olaysız CC taramaları ek I2C okuması/kayıt üretmez.
`o=5` dışındaki olay başına ek bir sekiz baytlık I2C okuması vardır; gecikmeye etkisi cihazda
ölçülmelidir. Timer aralıkları ve güç/rol işlemleri değiştirilmedi.

Kayıt, mevcut tanı yayınında ve yalnız değiştiğinde tek `REG_BINARY`
`B2LMaxEventLog` değeriyle yazılır. Opcode döngüsünde registry yazılmaz.
`b2lusbctl.exe maxevents` bu değeri tek okumada alır; BYN0002 arayüzüne
bağımlı değildir. Son yayımlanmış olayları gösterir, canlı donanım sorgulamaz.
Normal tarama o ana kadar olay görmediyse `MAX_LAST` güncel CC ölçümü değildir.

`startnet.cmd` iki mevcut MAX toplama çağrısında bu çıktıyı alır:

- `X:\B2LUSB-MAX77705-EVENTS.TXT`: iki toplamanın tarih/saat başlıklı birleşik kaydı;
- `X:\B2LUSB-MAX77705-EVENTS-LAST.TXT`: son toplama;
- mevcut `X:\B2LUSB-MAX77705-SUMMARY.TXT`: yalnız kısa `MAX_` satırları.

Mevcut CACHE doğrulamalı dışa aktarma yolu `B2LUSB-*.TXT` dosyalarını zaten
kapsar; CACHE yolu başarılı değilse kalıcı dışa aktarım varsayılmaz. SYSMSG
sayaçları firmware reset sayısı değildir: ham kesme gözlemlerini sayar.
Linux'taki üçlü SYSMSG tutarlılık kontrolü/kurtarma işlemleri bu tanı sürümüne
taşınmadı. Tek bir `SYSMSG=03` veya `05`, sırasıyla WDT/POR hipotezini araştırma
gerekçesidir; zaman sırası ve tekrar okunmuş kayıtlarla desteklenmelidir.

### WSL build ve flaş sonrası test sözleşmesi

Bu makinede Windows sürücüsü/UEFI/WIM build'i yapılmaz. `DEVIR.md` bölüm
0.5–0.9'daki WSL2 Ubuntu + Windows ARM64 MSVC/WDK iş bölümü kullanılır.
WSL'deki güncel repo kökünde, kaynaklar bu değişikliği içerirken:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

İlk tar yalnız `boot.img`, ikincisi yalnız `system.img` içermelidir. Aynı
turun BOOT ve SYSTEM çıktıları Odin AP ile, Re-Partition kapalıyken kullanılır.
Bu baseline profilinde eski birleşik `beyond2lte-usb-diag.tar` kullanılmaz.
`B2L_SKIP_MSVC_BUILD` / `B2L_ALLOW_STALE_SYS` ile eski çıktılar atlatılmaz;
`b2lusbctl.exe` de değiştiği için yeniden derlenip WIM'e girmelidir.

İlk kontrol: manifestte `B2LUSB_MAX77705_DRIVER_VERSION=0.1.0.28`,
parent/host için `.18`; olay özetinde `MAX_EVENTS v=1`, `count>0`,
`SNAP_FAIL=0` beklenir. `MAX_EVENT_READ_ERROR` veya `FORMAT_ERROR` başarılı
tanı sayılmaz. İlk kimlik/init başarısızsa boş olay kaydı mümkün olabilir;
`STAGE/STATUS` ile birlikte değerlendirilir.

İki ayrı açılış karşılaştırılır; her açılışın logu/fotoğrafı yeniden
başlatmadan önce ayrı saklanır:

1. **A — baştan takılı:** aynı adaptör/alıcı açılış boyunca takılı kalır.
   `.27` ile aynı koşulda olay kaydı, kritik MAX özeti ve xHCI çıktısı alınır.
2. **B — hazır olduktan sonra takılı:** Windows alıcı ayrıkken açılır.
   MAX `STAGE=0x70, STATUS=0` görüldükten sonra aynı alıcı takılır; 10 saniye
   beklenir, olay kaydı yeniden alınır. Sonra çıkarılır, 10 saniye beklenir
   ve ikinci kayıt alınır. Otomatik iki toplama bittikten sonraki fiziksel
   değişiklikler için aşağıdaki komutlarla yeni dosyalar alınmalıdır.

WinPE terminalinde, uygun fiziksel adımın ardından:

```bat
X:\Windows\B2LUSB\b2lusbctl.exe maxevents > X:\B2LUSB-MAX77705-B-ATTACH.TXT
X:\Windows\B2LUSB\b2lusbctl.exe status > X:\B2LUSB-B-ATTACH-STATUS.TXT
rem Aliciyi cikardiktan ve 10 saniye bekledikten sonra:
X:\Windows\B2LUSB\b2lusbctl.exe maxevents > X:\B2LUSB-MAX77705-B-DETACH.TXT
```

Terminal girdisi mümkün değilse, ilk otomatik MAX dökümündeki READY'den
sonra alıcı takılıp ikinci otomatik toplamaya kadar takılı tutulabilir.
Bu durumda testin tam hangi aşamasında takıldığı fotoğrafla belirtilmelidir;
kontrollü sonradan çıkarma kaydı ayrıca alınana kadar detach doğrulanmış sayılmaz.

Bu sürümün ilk başarı ölçütü **nedenin görünürleşmesidir**; fareyi çalıştırdığı
iddia edilmez. SOURCE oluşursa politika `CC_STATE=2`, `CC_SRC_EV>0`, ardından
MUIC `0x09`, OTG aktif, xHCI `CCS=1` ve `046d:c52e` beklenir. Ayrık durumdan
takma senaryosunda `CC_TRANS>0` da beklenir; ilk politika örneği zaten SOURCE
ise mevcut sayaç tasarımı gereği `CC_TRANS=0` kalabilir. SOURCE hâlâ yoksa
özellikle `o=1/2/3` kayıtları, SYSMSG, CC1 bit 2 ve A/B farkı paylaşılmalıdır.

17 dakikalık tanı gecikmesinin faz ölçümü ve MAX READY → HOST kapısı ayrı
işlerdir. Bu değişiklik yalnız gözlem ekler; konsoldaki eski “observer 12
saniye” mesajını gerçek 5 saniyelik ayarla eşleştirir.

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı ve cihazda doğrulanmış kaynak: ekrandaki `b2lmax77705` sürümü `0.1.0.27`
- `.27` cihaz kanıtı: kullanıcının son flaştan sağladığı sekiz ekran görüntüsü. Logitech `046d:c52e` alıcısı, çalışan recovery/Linux analizindekiyle aynı biçimde bütün Windows testi boyunca fiziksel olarak takılıydı.
- Gözlenen duvar saati: ilk fotoğraf ile test sonunu gösteren son fotoğraf arasında yaklaşık 17 dakika. Scriptte aşama zaman damgaları bulunmadığından bu süre tek bir komuta kesin olarak yüklenemez.

## `.27` cihaz sonucunun özeti

`.27`, 6. probe/init adımını artık cihaz üzerinde baştan sona doğrulamıştır. Sürücü `STAGE=0x70`, `STATUS=0`, `PLATFORM_OK=1` durumuna ulaşmış; Linux çalışma maskeleri, `CCCTRL1=0x1f` ve probe-final CC-open işlemi tamamlanmıştır. Yeni kanıt `CCOPEN_OK=1`, `CTRL3_BEFORE=0x64`, `CTRL3_AFTER=0x64` değerlerini verir. `0x64 & 0x02 == 0` olduğundan `LockErrorEn` zaten kapalıdır: kod ve read-back çalışmıştır, fakat donanımda değiştirilecek bir bit yoktur. Böylece 6. adım kapanmış, “attach'i LockErrorEn engelliyordu” hipotezi yanlışlanmıştır.

İlk işlevsel uyuşmazlık artık kesin biçimde 7. Type-C attach/rol adımıdır. Fiziksel Logitech alıcı takılıyken `CC0/CC1=0x40/0x00`, yani `CCPinStat=CC1_ACTIVE` görülür; buna karşın `CC_STATE=CC_PREV=0`, `CC_TRANS=0` ve `CC_SRC_EV=0` kalır. `.27` boyunca `CC_SAMPLES=0x71f` (1.823), `CC_IRQS=4` ve `CC_SEEN=0x6d` elde edilmiştir. Olay registerları canlıdır, fakat MAX77705 durum makinesi fiziksel CC1_ACTIVE bilgisini Linux'taki `cc_SOURCE` rolüne çevirmemektedir.

Kesme registerlarının okunabilmesi, MAX77705 GPIO kaynaklı kesmenin Windows'a donanımsal IRQ olarak teslim edildiğini kanıtlamaz. Ekrandaki IRQ `273`, BYN0003/USBXHCI kaynağıdır; Linux'ta görünen HSI2C IRQ114 veya MAX77705 parent GPIO IRQ318 değildir. GPA1_6 için doğrulanmış ACPI/GIC kaynağı bulunmadığından `.27` sahte ISR kurmaz; TOP `INTSRC`, dört read-to-clear USBC baytı ve hemen ardından CC status timer ile okunur.

Polling azaltımı cihazda doğrulanmıştır. xHCI gözlem sayacı önceki yaklaşık 7.887 değerden 160'a inmiştir; bu yaklaşık `%98` azalmadır ve 5 saniyelik sınırlı gözlemle uyumludur. MAX sayacı ilk fotoğraf çekilmeden önce çalışmaya başladığı için `CC_SAMPLES=1.823` değerini 17 dakikalık fotoğraf aralığına bölmek örnekleme hızına bir üst sınır verir: toplam en fazla yaklaşık `1,79 örnek/s`; ilk 50 hızlı örnek çıkarıldığında geç dönem en fazla yaklaşık `1,74 örnek/s` veya örnek başına en az `575 ms` görünür. Bu değerler kalıcı 50 Hz agresif polling'i ve 1 ms xHCI gözlemcisini 17 dakikanın baskın nedeni olmaktan çıkarır. Gecikme artık daha çok seri çalışan PnP/SetupAPI, registry dökümleri ve ETW dışa aktarma tanılarında aranmalıdır; ancak kesin aşamayı söylemek için `startnet.cmd` içinde aşama başlangıç/bitiş zaman damgaları gerekir.

Ana mimari fark şudur:

```text
Samsung Linux:
MAX IRQ -> CC=SOURCE -> VBUS/OTG + rol bildirimi -> MUIC USB_AP
        -> DWC3/PHY runtime resume -> xHCI -> USB enumerate -> HID

Windows `.27` cihaz sonucu:
ACPI _DEP yalnızca BYN0004 sürücüsünün başlamasını bekler
        +-> BYN0002 hemen PHY/DWC3 HOST yapar -> USBXHCI çalışır
        `-> BYN0004 zamanlayıcısı platform + HSI2C + MAX init'i tamamlar
            -> CONTROL3 read/masked-write/read-back başarılı;
               LockErrorEn zaten kapalıdır (`0x64 -> 0x64`)
            -> TOP + USBC IRQ blok okuma -> hemen CC_STATUS örneği/geçiş kaydı
            -> takılı Logitech alıcıya rağmen CC1 pini görülür,
               fakat CCStat=No Connection kalır
            -> güvenli yol: VBUS kapalı + MUIC OPEN -> xHCI attach göremez
```

## Adım adım yan yana karşılaştırma

| Aşama | Çalışan Samsung Linux akışı | Mevcut Windows kaynağının hedefi | Son Windows flaşında gözlenen | Sonuç |
|---|---|---|---|---|
| 1. Donanım sahipliği | `15d60000.hsi2c -> i2c-14 -> 14-0066 -> max77705-usbc`; TOP/USBC/charger alt adresleri tek MFD altında | `BYN0004`, HSI2C `0x15D60000`, USI `0x15C22000`, CMGP CMU ve ilgili pinctrl sayfalarını tek başına eşler; `0x66/0x25/0x69` adreslerini kendisi yönetir | `.27`de `BYN0004` Started; `0x15C00000`, `0x15D60000`, `0x15C22000` ve `0x15C30000` olmak üzere dört MMIO alanı atanmış | Eşleşen sahiplik modeli ve kaynak kapsamı cihazda doğrulandı |
| 2. HSI2C güç/saat hazırlığı | Linux pinctrl ile GPM12-0/GPM13-0'ı işlev 2 yapar; DT 200 MHz ister, clock framework mevcut hızı okur; QCH/gate hazırlanır ve 400 kHz timing hesaplanır | `.24`, aynı pinctrl ve CMGP3 alanlarını BYN0004'e verir; 200 MHz'i deterministik kurar, QCH'yi ve buna karşılık gelen FS timing alanlarını yazar, geri okur | `PLATFORM_OK=1`, `CMGP_MUX3=1`, `CMGP_DIV3=1`, `CMGP_QCH3=0x3f0001`, `GPM12_CON=GPM13_CON=2` | Eşleşme cihazda doğrulandı; `.23`teki ilk engel giderildi |
| 3. HSI2C mesajı | Samsung `exynos5_i2c_xfer` adres, yön, uzunluk ve RUN sırasını kurar; Linux ad alanında IRQ 114 ile tamamlanır | Windows aynı mesaj kurulumunu yapar; karşılık gelen GIC/ACPI interrupt kaynağı doğrulanmadığı için bounded sleeping polling ile tamamlanmayı gözler | Son işlem `I2C_REGADDR=I2C_TRYADDR=0x0701a400` (`0x69` charger), `I2C_RETRY=0`, `I2C_PHASE=0`, `I2C_ERR=0`, genel `STATUS=0` | Taşıma katmanı çalışıyor; Linux IRQ numarasını doğrudan kullanmamak bu flaşın blokajı değil |
| 4. MAX kimlik doğrulama | Parent MFD TOP `0x66`, USBC `0x25`, charger `0x69` istemcilerini oluşturur | TOP ID/rev, USBC HW/FW ve charger registerları aynı sırayla okunur | `PMIC_ID=0x15`, `PMIC_REV=0x2`, `USBC_HW=0x1a`, `USBC_FW=0x1a`, `USBC_MINOR=0x10` | MAX77705 fiziksel erişimi ve kimlik doğrulaması başarılı |
| 5. Olay mekanizması | GPIO parent IRQ 318; TOP `INTSRC` sonrası alt CC/MUIC/PD/APCMD registerları blok okunur ve nested IRQ işlenir | Doğrulanmış MAX GPIO ACPI kaynağı yok; TOP `INTSRC` + dört read-to-clear USBC baytı tek blokta, hemen ardından CC status okunur. 20 ms yalnız ilk 50 CC örneği, sonra 200 ms hedeflenir | `.27`: `TOP_SEEN=0x9`, `CC_SEEN=0x6d`, `PD_SEEN=0xc0`, `CC_IRQS=4`, `CC_SAMPLES=0x71f`; BYN0004 üzerinde IRQ kaynağı yok | Okuma sırası ve sınırlı hızlı pencere cihazda çalıştı; olay görünürlüğü korunuyor |
| 6. Probe/init opcodes ve kesme düzeni | USBC/PD probe; `UIC/CC/PD/VDM` maskeleri önce probe, sonra çalışma vektörüne geçer; CCCTRL ve firmware komut kuyruğu tamamlanır; en sonda manual CC-open off isteği CONTROL3 `LockErrorEn` bitini temizler | Charger unlock; `57 01`; `00/20/1c/ff`; `54 00`; `0c 1f` + `0b`; `00/20/00/f0`; sonra `1c` read, gerekirse `1d` masked write ve tekrar `1c` read-back | `.27`: `CCOPEN_OK=1`, `CTRL3_BEFORE=CTRL3_AFTER=0x64`; bit 1 zaten sıfır olduğundan write gerekmemiştir | 6. adım cihazda kapandı; CONTROL3 kodu doğru, LockErrorEn blokaj hipotezi yanlışlandı |
| 7. Fiziksel takılma ve CC | IRQ'da `CC1_ACTIVE`, `PLUG_ATTACHED`, `cc_SOURCE`; veri rolü DFP, güç rolü SRC olur | Probe-final normalizasyondan sonra CC örnek/geçiş/source sayaçlarını sürdürür ve yalnız `CC_SOURCE=2` ile HOST/OTG'yi açabilir | `.27`: aynı Logitech alıcı takılıyken `CC0/CC1=40/00`, `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0`; 1.823 örnek, dört CC interrupt örneği ve birikimli olay baytları canlı | İlk işlevsel blokaj artık kesin olarak 7'dedir; sıradaki karşılaştırma CC event/FSM ve ForceError koordinasyonudur |
| 8. VBUS/charger OTG | `vbus_turn_on_ctrl(1)`, CHGIN-OTG; charger mode `0x04 -> 0x0e`; VBUS HIGH bildirilir | Charger kilidi açılır, interrupt maskesi ayarlanır, `CNFG_00` mevcut moda göre `0x0e` benzeri OTG moda geçirilir ve read-back doğrulanır | `CHG_UNLOCK=1`, `CHG_CFG06=0x0c`, `CHG_MODE=0x15`; `OTG_SW=OTG_HW=0` | Source görülmediği için VBUS güvenli biçimde kapalı tutuluyor; xHCI `PP=1` gerçek konektör VBUS'unu kanıtlamaz |
| 9. MUIC D+/D- yönlendirme | OTG algılanır, switch `0x09`; opcode `06 09` ile USB yolu AP'ye verilir | Source durumunda `CONTROL1_WRITE 06 09`, ardından `CONTROL1_READ 05` ile `0x09` doğrulanır; detach'te `OPEN=0x3f` seçilir | `MUIC_REQ=0x3f`, `MUIC_CTL1=0x3f`, `MUIC_OK=1` | İletişim ve read-back başarılı, fakat CC kararı nedeniyle D+/D- kasıtlı olarak OPEN bırakıldı |
| 10. Rol bildirimi ve DWC3 | `Turn On Host(DFP)` ve `USB_HOST_ATTACHED`; DWC3 role-switch work başlar | Windows'ta Linux notifier zinciri yok. `BYN0002` PrepareHardware içinde doğrudan sabit HOST seçer | BYN0002 ve BYN0003 Started | Windows bu aşamayı MAX sonucundan önce yaptı |
| 11. PHY/runtime resume | PHY connect, LDO on, runtime resume, UTMI init, tune, reset release | PMU izolasyonu kaldırılır; q-channel, PHY reset, UTMI, tune ve DWC3 core reset uygulanır; USB3 gizli tutulur | PHY registerları beklenen host değerlerinde | Host tarafının bu bölümü büyük ölçüde çalışıyor |
| 12. xHCI başlatma | DWC3 host olduktan sonra xHCI başlar, USB bus 1/2 kaydolur | `USBXHCI` inbox sürücüsü ACPI `BYN0003`'e bağlanır; parent önceden DWC3'ü HOST'a hazırlar | xHCI `RUN=1`, command ring/DCBAA/config kurulmuş | Controller çalışıyor; konektör yolu hazır değil |
| 13. Port bağlantısı | Yaklaşık 357 ms sonra full-speed aygıt görülür | USBXHCI'nin PORTSC değişimi görmesi beklenir | Logitech alıcı fiziksel olarak takılıyken controller `RUN=1`, `DCBAA=1`, `CFG=1`; portlar `CCS=0`, `PED=0`, `PLS=5/RxDetect`; sınırlı `.27` gözleminde 160 örnek ve sıfır transition | Elektriksel bağlantı xHCI'ye ulaşmıyor; gözlem maliyeti yaklaşık `%98` azaldı |
| 14. Enumeration | `new full-speed device`, `046d:c52e`, descriptor/config/interface işlemleri | Inbox hub/USB stack aynı standart enumeration'ı yapar | Aynı Logitech alıcı takılı olmasına rağmen root hub altında aygıt yok | Enumeration başlamadı; sorun HID sınıf sürücüsünde değil |
| 15. HID bind | Arayüz 1.0 `hid-generic` keyboard, 1.1 mouse | `USBHUB3 -> HIDUSB -> MOUHID/MOUCLASS` zinciri bağlanır | Hizmetler mevcut fakat “No devices were found” | Fare sorununun kökü HID değil, daha aşağıdaki Type-C/VBUS/MUIC yoludur |
| 16. Detach | VBUS kapatılır, roller geri alınır, MUIC açılır, DWC3 device/idle yoluna döner | Windows CC source dışına çıkınca önce OTG kapatır, sonra MUIC `OPEN=0x3f` doğrular | `.27`de takılı alıcıya rağmen attach oluşmadı; güvenli ayrılma sonucu `OPEN=0x3f` ve OTG off doğrulandı | Güvenli durum kanıtlandı; attach oluşmadığı için gerçek detach geçişi sınanamadı |

## Çalışan Linux zaman çizgisi

Fiziksel attach olayının başlangıcı yaklaşık `461.169117` kabul edildiğinde:

| Zaman | Linux olayı |
|---:|---|
| `+0.000 ms` | `PLUG_ATTACHED`, `cc_SOURCE` |
| `+0.746 ms` | `vbus_turn_on_ctrl enable=1` |
| `+2.802 ms` | charger mode `0x04 -> 0x0e` |
| `+8.336 ms` | DFP/SRC/ATTACHED kararı |
| `+14.737 ms` | `Turn On Host(DFP)` |
| `+14.799 ms` | `USB_HOST_ATTACHED` |
| `+16.776 ms` | MUIC opcode `06 09` |
| `+16.818 ms` | DWC3 host work ve PHY açılışı |
| `+24.795 ms` | `AUTOIBUS 57 01` kuyruğa alınır |
| `+35.519 ms` | İlgili opcode cevapları başarıyla tamamlanır |
| `+353.635 ms` | Logitech alıcı full-speed olarak görülür |
| `+543.232 ms` | HID keyboard bind olur |
| `+610.512 ms` | HID mouse bind olur |

Bu süreler Windows için bir performans hedefi olmaktan önce bir sıralama kanıtıdır: MAX/CC/VBUS/MUIC tarafı yaklaşık ilk 35 ms'de anlamlı biçimde tamamlanmış, USB enumeration ancak bundan sonra başlamıştır.

## Komut ve register eşleşmesi

| Amaç | Samsung Linux kanıtı | Windows uygulaması | Durum |
|---|---|---|---|
| USB D+/D- yolunu AP'ye ver | `CONTROL1_WRITE: 06 09` | Source görüldüğünde `06 09`, sonra `05` read-back | Uygulama var; `.27`de source görülmediği için bu dala girilmedi |
| Firmware AUTOIBUS | `57 01`, cevap `57` | `57 01`, APCMDRES ve cevap opcode kontrolü | `.27` READY sonucu, opcode taşıma yolunun tamamlandığını doğruluyor |
| Otomatik VBUS'u kapat | Samsung init davranışı | `54 00` | `.27` init zincirinde başarıyla tamamlandı |
| Kesme maskeleri | Çalışan log: `UIC/CC/PD/VDM=00/20/00/f0`; probe sırasında `PD/VDM=1c/ff` ara durumu | `00/20/1c/ff`, init sonrası `00/20/00/f0`; her register readback ile doğrulanır | `.27`de `00/20/00/f0` ve `MASKS_OK=1`; cihazda doğrulandı |
| CC kontrolü | `CCCTRL1_W 0c 1f`; sonrasında IRQ tabanlı rol durumu | `0c 1f`, `0b` readback=`1f`; TOP/USBC kesme blok okumasından hemen sonra `CC_STATUS0/1` | `.27`de `CCCTRL1=0x1f`; olay baytları yakalanıyor fakat takılı alıcıya rağmen `CC_STATE=0`, `CC_TRANS=0` |
| Probe-final CC-open bırakması | `cc_open_req=1`; `pdic_manual_ccopen_request(0)`; `CONTROL3_R=0x1c`, gerekirse `LockErrorEn` bit 1 temizlenerek `CONTROL3_W=0x1d` | Runtime maskelerinden sonra aynı read/masked-write/read-back; `CCOPEN_OK`, `CTRL3_BEFORE`, `CTRL3_AFTER` yayımlanır | `.27`: `CCOPEN_OK=1`, `0x64 -> 0x64`; LockErrorEn zaten kapalıydı, dolayısıyla işlem değişiklik yapmadan doğrulandı |
| Charger OTG | `CNFG_00 0x04 -> 0x0e` | Source görülürse mevcut moda göre `0x0e/0x0f/...`; read-back ve rollback | Source görülmediği için çalıştırılmadı; `CHG_MODE=0x15` OTG içermiyor |
| Detach izolasyonu | VBUS off, switch open | OTG off, ardından MUIC `0x3f` | `.27`de `MUIC_OK=1`, `MUIC_CTL1=0x3f`, `OTG_SW/HW=0` ile cihazda doğrulandı |

## Kritik farklar ve önem sırası

### 1. Host başlangıcı MAX77705 READY durumuna bağlı değil

ACPI `_DEP`, `BYN0002`yi `BYN0004`e bağlar; ancak `BYN0004` D0Entry, MAX probe'unu beklemeden timer kurup başarı döndürür. Windows bağımlılığı tamamlanmış sayar ve `BYN0002` PrepareHardware doğrudan HOST rolünü seçer. Bu nedenle MAX identity/CC/MUIC/VBUS başarısız olsa da USBXHCI başlar.

Bu, Linux sırasındaki en büyük yapısal farktır. Doğru eşdeğerlik için HOST handoff, en azından şu koşullar doğrulanmadan yapılmamalıdır:

```text
IdentityReady
ChargerUnlocked
AutoIbusConfigured
UsbcMasksConfigured
UsbcRuntimeMasksConfigured
AutoVbusDisabled
CcControlConfigured
CcOpenNormalized
CC_STATUS == SOURCE
MuicPathVerified == USB_AP
OtgHardwareActive
```

`.27` bu ayrımı kesinleştirmiştir: MAX tarafı Linux probe-final CC-open işlemi dahil READY'e ulaşsa ve CC interrupt baytlarını yakalasa bile `CC_STATUS == SOURCE`, `MuicPathVerified == USB_AP` ve `OtgHardwareActive` koşulları oluşmamıştır. BYN0002/BYN0003 yine HOST/RUN durumuna geçtiği için controller çalışır, fakat konektör zinciri hazır değildir.

### 2. `.27` 6. adımı kapattı; ilk işlevsel uyuşmazlık 7. adımda

Son Windows kanıtında platform, HSI2C, kimlik, opcode, kesme maskeleri, CCCTRL1 ve probe-final CC-open katmanları başarıyla tamamlanmış; olay tanısı da canlı veri üretmiştir:

```text
STAGE=0x70                 STATUS=0x0
PLATFORM_OK=0x1            I2C_RETRY=0x0
PMIC_ID=0x15               PMIC_REV=0x2
USBC_HW/FW/MINOR=1a/1a/10  CHG_UNLOCK=0x1
UIC/CC/PD/VDM=00/20/00/f0  MASKS_OK=0x1
CCCTRL1=0x1f               CC0/CC1=0x40/0x00
CCOPEN_OK=0x1              CTRL3_BEFORE/AFTER=0x64/0x64
TOP_SEEN=0x9               CC_SEEN=0x6d
PD_SEEN=0xc0               CC_IRQS=0x4
CC_SAMPLES=0x71f           CC_TRANS=0x0
CC_STATE/PREV=0x0/0x0      CC_SRC_EV=0x0
MUIC_OK=0x1                MUIC_CTL1=0x3f
OTG_SW/HW=0/0
```

`CTRL3_BEFORE=CTRL3_AFTER=0x64` sonucu belirleyicidir. `LockErrorEn` bit 1, `0x64` içinde zaten sıfırdır; sürücü Linux'taki işlemi uygulamış, read-back koşulunu geçmiş ve `CCOPEN_OK=1` yayımlamıştır, fakat donanım durumunu değiştirmemiştir. Bu nedenle 6→7 sınırında bulunan kod eksikliği giderilmiş olsa da attach arızasının sebebi değildir. 6. adım artık kapalı, ilk uyuşmayan adım 7'dir.

`CC0=0x40`, `bits[7:6]=1` ile stok Samsung tanımında `CC1_ACTIVE`; `bits[2:0]=0` ile `cc_No_Connection` değeridir. Aynı Logitech alıcı test boyunca takılıyken 1.823 örneğin hiçbirinde rol değişmemiştir. Dört sıfır olmayan CC interrupt örneği ve `CC_SEEN=0x6d` birikimi, TOP/USBC read-to-clear okumasının canlı olay verisi aldığını gösterir; buna rağmen `CC_TRANS=0` ve `CC_SRC_EV=0` kalmıştır. Sonuç, sorunu olayın hiç görülmemesinden çok CC olay/durum-makinesi koordinasyonuna, Linux'un attach sonrasında uyguladığı ForceError/status güncelleme akışına daraltır.

BYN0004'e atanmış gerçek interrupt kaynağı bulunmadığından bu sayaçlar parent GPIO interruptının Windows ISR'ına ulaştığı anlamına gelmez; olay timer tarafından okunmuştur. Ancak uzun test boyunca birden fazla interrupt-register örneği yakalanıp hiç SOURCE oluşmaması, “tek kısa olay polling aralığında kaçtı” açıklamasını daha da zayıflatır.

### 3. HSI2C platform hazırlığı `.24` ile cihazda doğrulandı

`BYN0004` artık HSI2C ve USI yanında CMGP CMU `0x15c00000..0x15c07fff` ile CMGP pinctrl `0x15c30000..0x15c30fff` kaynaklarını da tek sahip olarak alır. Başlatma sırası şöyledir:

1. GPM12-0 ve GPM13-0 için pull/drive sıfırlanır, function `2` seçilir.
2. `DIV_CLK_USI_CMGP3` (`+0x1824`) oran `1` ile divide-by-two durumuna alınır.
3. `MUX_CLK_USI_CMGP3` (`+0x1020`) 400 MHz CMGP bus parent'a alınır; sonuç 200 MHz'tir.
4. `USI_CMGP3_QCH` (`+0x304c`) normal enable durumuna (`bits[2:0]=1`) alınır.
5. Linux'un `200 MHz / 400 kHz` hesabıyla FS1=`0x00f00000`, FS2=`0x000000e0`, FS3=`0x00210000` alanları controller resetinden sonra kurulur.
6. Pin, clock ve timing alanları geri okunur; doğrulama başarısızsa HSI2C transferine geçilmez ve `HsI2cFailurePhase=0x08` yayımlanır.

Canlı `.24`–`.27` değerleri bu sıranın tamamlandığını gösterir: `PLATFORM_OK=1`, mux/div değerleri `1`, QCH alt üç biti `1`, GPM12/GPM13 function alanları `2` ve ilk kimlik okumasından sonraki charger erişimleri de başarılıdır. `.23`teki `ADDR1` timeout'u artık yeniden üretilmemiştir.

Linux logunda HSI2C transfer completion için görülen IRQ 114, Linux IRQ ad alanındaki sanal numaradır; Windows ACPI'ye doğrudan `114` olarak aktarılamaz. BYN0004 için karşılık gelen GIC SPI ve trigger türü doğrulanmış bir interrupt kaynağı bulunmadığından Windows tamamlanmayı PASSIVE bağlamda bounded polling ile gözler. `.24`–`.27` arasındaki hatasız kimlik/opcode/charger erişimleri bu seçimin önceki `ADDR1` arızasının veya mevcut `CCStat=No Connection` sonucunun sebebi olmadığını kanıtlar. ISR/DPC yolu ancak DT/GIC eşlemesi doğrulanıp doğru ACPI interrupt kaynağı yayımlandıktan sonra eklenmelidir. HSI2C completion kesmesi, Linux'ta IRQ 318 olarak görünen MAX77705 GPIO parent kesmesinden de ayrıdır.

Samsung DT `default-clk=200000000` ister. Vendor CAL tablosunda USI_CMGP3 için yalnız 400 MHz normal seviye bulunduğundan Linux'taki `clk_set_rate(200 MHz)` çağrısı platform durumuna göre başarısız olabilir; sürücü yine `clk_get_rate()` ile etkin hızı okuyup timing hesaplar. Windows clock framework kullanmadığı için bu belirsizliği taşımıyor: 400 MHz parent + oran `1` ile DT'nin amaçladığı 200 MHz'i açıkça kuruyor ve timing'i bu hıza göre yazıyor.

### 4. İki ayrı IRQ zinciri yerine sınırlı, doğrulanabilir polling kullanılıyor

Linux'ta HSI2C transfer completion Linux IRQ114 ile; MAX77705 CC/MUIC/PD/APCMD olayları ise Linux GPIO parent IRQ318 ve nested IRQ'larla yürür. Windows her iki alanda da polling kullanır: HSI2C transferi bounded sleeping polling; MAX olayları `.27`de ilk 50 başarılı CC örneğinde 20 ms, sonrasında ve yerleşmiş rolde 200 ms hedefli timer taramasıyla izlenir. TOP kaynak registerı önce, dört child kesme registerı tek blokta, CC status da hemen arkasından okunur. Read-to-clear örnekler ayrıca OR-birikimi ve sayaçlarla korunur. `.27` cihaz sonucu bu yolun `CC_SEEN=0x6d`, `CC_IRQS=4` ve 1.823 CC örneği ürettiğini doğrular; ancak geçiş/source üretmemiştir. BYN0003 üzerinde görülen IRQ273 yalnız USBXHCI kaynağıdır ve bu iki MAX/HSI2C kesme yolunun yerine geçmez; doğrulanmış GPA1_6 kaynağı gelene kadar gerçek bir ISR iddiasında bulunulmaz.

MAX sayacı ilk fotoğraftan önce başladığı için 17 dakikalık fotoğraf aralığıyla hesaplanan toplam hız en fazla yaklaşık `1,79 Hz`'dir. İlk 50 hızlı örnek çıkarıldığında geç dönem üst sınırı yaklaşık `1,74 Hz`, yani örnekler arası alt sınır yaklaşık `575 ms` olur. Bu, hedeflenen 200 ms periyodun WinPE zamanlama/tanı yükü altında gecikebildiğini gösterir; fakat eski sürekli 20 ms/50 Hz davranıştan çok uzaktır. xHCI tarafında örnek sayısının yaklaşık 7.887'den 160'a inmesi de 1 ms gözlem yükünün kaldırıldığını bağımsız olarak doğrular. Konsoldaki “xHCI startup observer 12 saniye için kuruldu” metni `b2lusbctl.exe` içinde güncellenmemiş sabit bir mesajdır; 160 sayacı gerçek sürücünün 5 saniyelik sınırlı gözlemini destekler. Dolayısıyla polling azaltımı işe yaramıştır ve 17 dakikanın baskın açıklaması değildir.

### 5. On yedi dakikalık gecikme tanı orkestrasyonuna daralıyor

İlk ve son fotoğraf arasındaki yaklaşık 17 dakika, polling azaltılmış olmasına rağmen oluşmuştur. Bu süreyi tek bir sürücü callback'ine yükleyecek aşama zaman damgası yoktur; dolayısıyla kesin neden henüz kanıtlanmış değildir. Bununla birlikte `windows/startnet.cmd` kalan başlıca maliyeti açıklar:

- `:B2LUsbCollectMax77705State` bir çağrıda 86 ayrı `reg.exe query` çalıştırır; 55 özet alanı da ayrı `reg.exe` süreçleriyle tekrar okur. Toplam 141 registry süreci üreten bu fonksiyon test yolunda iki kez çağrılır.
- Aynı yol çok sayıda eşzamanlı olmayan `pnputil /scan-devices` ve ayrıntılı `/enum-devices /services /stack /drivers /resources` sorgusu çalıştırır; ayrıca USB, HIDClass ve Mouse sınıflarını ayrı ayrı tarar.
- `HKLM\\...\\Enum\\ACPI` altında özyinelemeli `reg.exe ... /s /f B2LMon` araması, tam `setupapi.dev.log` üzerinde `findstr`, ETW için `logman` başlat/durdur ve senkron `tracerpt` CSV dönüşümü yapılır.
- Üretilen büyük raporların bir bölümü `type` ile dar ve yavaş WinPE konsoluna yazılır. Görüntülerde aynı uzun PnP ve registry listelerinin ekrana basılması bu yolun gerçekten çalıştığını doğrular.
- Sürücü tarafı agresif örnekleme yapmasa da `B2lMaxPublishDiagnostic()` her 10 başarılı geç dönem örneğinde geniş registry tanı kümesini yeniden yazar. Nominal 200 ms periyotta bu yaklaşık iki saniyede bir yazma grubu demektir; tek başına 17 dakikayı kanıtlamaz, fakat registry/tanı yükünü büyütür ve gerçek timer hızının hedefin altına düşmesine katkıda bulunabilir.

Bu nedenle eski “dakikaları sürekli MAX/xHCI polling oluşturuyor” hükmü geri çekilmiştir. Ölçümün desteklediği çalışma hipotezi, gecikmenin seri PnP/SetupAPI/registry/ETW toplama ve konsol çıktı yolunda birikmesidir. Bir sonraki performans doğrulaması, controller davranışını değiştirmek yerine her pahalı `startnet.cmd` aşamasının başına ve sonuna zaman damgası koymalı; ardından tekrar eden ikinci MAX dökümü, sınıf-geneli PnP sorguları, ETW dönüşümü ve tam registry özetleri ayrı ayrı süre ölçülerek daraltılmalıdır.

### 6. Windows attach sırası Linux ile tam aynı değil ve source kararı oluşmuyor

Canlı Linux kaydında aynı Logitech alıcıyla `CC1_ACTIVE` ardından `PLUG_ATTACHED` ve `cc_SOURCE` oluşur; charger/VBUS ve host notifier çok erken çalışır, MUIC `06 09` hemen arkasından gelir. `.27`de Windows tam probe/init ve olay taraması sonrasında yalnız `CC1_ACTIVE` kısmını görmüş, aynı status byte'ında `cc_No_Connection` kalmıştır. Dört CC interrupt örneği yakalanmasına rağmen 1.823 status örneğinde değişiklik olmamış; source dalına hiç girilmemiştir.

Source oluştuğunda Windows güvenlik amacıyla önce MUIC `06 09` + `05` doğrulamasını, sonra charger OTG/VBUS açılmasını şart koşar. Bu sıra Samsung ile birebir değildir. `.27` stok probe-final CONTROL3 işleminin blokaj olmadığını gösterdiği için sıradaki iş, 7. adımda Linux CC event/FSM ve ForceError koordinasyonunu karşılaştırmaktır. CC source doğrulanmadan VBUS'u zorla açmak bu belgedeki güvenlik koşulunu ihlal eder.

### 7. Linux koordinasyon katmanları Windows'ta sadeleştirilmiş

Linux akışı charger, battery, MUIC, Type-C manager, USB notifier, runtime-PM ve role-switch bileşenlerini birlikte yürütür. Windows sürücüsü gerekli register/opcode çekirdeğini uygular ama bu framework koordinasyonunun tamamını modellemez. Sabit HOST hedefi için kabul edilebilir olabilir; attach/detach, hata kurtarma ve güç politikası açısından tam eşdeğer değildir.

## `.27` devir teşhisi ve sonraki değişiklik sınırı

**Kaynak incelemesi güncellemesi:** Aşağıdaki cihaz sırası geçerlidir; bu
bölümde korunmuş ForceError odaklı eski önerinin yerini dosyanın başındaki
`.28` olay tanısı almıştır. SOURCE görülmemesi ForceError eksikliğini kanıtlamaz.

Son `.27` Windows flaşında, Logitech alıcı test boyunca takılıyken fareyi engelleyen sıra şöyledir:

1. BYN0004 dört MMIO kaynağıyla Started olur; CMGP/pinctrl/HSI2C hazırlığı doğrulanır.
2. TOP, USBC ve charger erişimleri ile init opcode zinciri tamamlanır; Linux çalışma maskeleri `00/20/00/f0`, `CCCTRL1=0x1f` ve probe-final CONTROL3 kontrolü geri okumayla doğrulanır. `CCOPEN_OK=1`, `0x64 -> 0x64` sonucu LockErrorEn'in zaten kapalı olduğunu ve 6. adımın tamamlandığını gösterir.
3. Timer tabanlı Linux okuma sırası olay baytlarını yakalar: `TOP_SEEN=0x9`, `CC_SEEN=0x6d`, `PD_SEEN=0xc0`, `CC_IRQS=4`.
4. Buna rağmen 1.823 örnekte `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0` kalır. Fiziksel alıcı varken `CC_STATUS0=0x40`, CC1 pinini aktif gösterir fakat güç rolünü `cc_No_Connection` bırakır; Linux'taki `PLUG_ATTACHED -> cc_SOURCE` geçişi oluşmaz.
5. Windows güvenli olarak VBUS/OTG'yi kapalı ve MUIC yolunu `OPEN=0x3f` tutar.
6. Buna rağmen `_DEP` karşılanmış sayıldığı için BYN0002 DWC3/PHY'yi HOST yapar ve USBXHCI çalışır.
7. xHCI elektriksel attach göremez; sınırlı 160 örnekte portlar `RxDetect`, `CCS=0` kalır ve USBHUB3/HIDUSB/MOUHID aşamalarına gelinmez.
8. Polling azaltımı çalışmıştır, fakat ilk ve son fotoğraf arasında yaklaşık 17 dakika geçmiştir. Bu süre sürücü timerlarından çok seri PnP/SetupAPI/registry/ETW tanı yoluna işaret eder; aşama zaman damgaları olmadan tek bir komut kesin neden ilan edilemez.

Bu nedenle mevcut işlevsel öncelik HID, xHCI ring, HSI2C platformu, kesme maskesi, CONTROL3 veya Linux'taki sanal IRQ114 numarasını doğrudan kullanmak değildir. Bir sonraki sürücü değişikliği 7. adımda, Samsung Linux CC interrupt işleyicisinin `CC1_ACTIVE` sonrasında `PLUG_ATTACHED/cc_SOURCE` üretirken uyguladığı CC event/FSM ve ForceError koordinasyonunu eşlemelidir. Değişiklik şu ayrımı korumalıdır:

1. Önce Linux'taki ilgili CC interrupt/status ve ForceError işlem sırası kaynak düzeyinde birebir çıkarılmalı; Windows tanısına bu ara durumları ayıran read-back alanları eklenmelidir.
2. CC karar mekanizması gerçek `SOURCE` üretmeden MUIC `USB_AP` veya charger OTG/VBUS zorlanmamalıdır.
3. Başarı ölçütü `CC_STATE=2`, `CC_TRANS>0`, `CC_SRC_EV>0`; ardından MUIC `0x09`, OTG/VBUS ve xHCI portunda `CCS=1` zinciridir.
4. Performans işi işlevsel CC değişikliğinden ayrı tutulmalıdır. Önce `startnet.cmd` pahalı aşamalarına zaman damgaları eklenmeli; 141 registry süreci üreten MAX dökümünün iki çağrısı, PnP sınıf taramaları, SetupAPI araması ve ETW/`tracerpt` ayrı ayrı ölçülmelidir.

`cc_SOURCE` güvenilir biçimde doğrulandıktan sonraki ayrı iş, MAX READY ile BYN0002 HOST başlangıcı arasındaki çalışma zamanı kapısını kurmaktır.

## Kaynak konumları

- Linux canlı kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`, özellikle 309-375, 538-715, 747-789, 842-925 ve 978-1016.
- ACPI MAX ve USB bağımlılığı: `silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl`, 254-329.
- [Samsung GPL MAX77705 tanımları](https://github.com/ianmacd/d2s/blob/master/include/linux/ccic/max77705.h#L492-L520): `OPCODE_CTRLREG3_R=0x1c`, `OPCODE_CTRLREG3_W=0x1d`, `GET_CONTROL3_LOCK_ERROR_EN` bit 1.
- [Samsung GPL CONTROL3/CC-open uygulaması](https://github.com/ianmacd/d2s/blob/master/drivers/ccic/max77705_usbc.c#L899-L993): `max77705_set_lockerroren()` ve `max77705_manual_ccopen_request()`.
- [Samsung GPL probe sonu](https://github.com/ianmacd/d2s/blob/master/drivers/ccic/max77705_usbc.c#L3333-L3379): `cc_open_req=1` ve `pdic_manual_ccopen_request(0)`.
- Windows MAX durum makinesi: `windows/b2lusb/b2lmax77705.c`; özellikle `B2lMaxNormalizeCcOpen()` ve `B2lMaxServiceConnection()`.
- Windows MAX timer/PnP: `windows/b2lusb/b2lmax77705.c`; özellikle `B2lMaxEvtPollTimer()`.
- Windows HSI2C platform ve taşıma katmanı: `windows/b2lusb/b2lmax77705_hw.c`, 162-727.
- Windows sınırlı xHCI gözlemcisi ve sabit HOST başlangıcı: `windows/b2lusb/b2lusb.c`.
- Windows ilk-boot PnP, registry, ETW ve konsol tanı orkestrasyonu: `windows/startnet.cmd`, özellikle 200-233 ve 240-452.
- Windows PHY/DWC3 hazırlığı: `windows/b2lusb/b2lusb_hw.c`, 422-665.
