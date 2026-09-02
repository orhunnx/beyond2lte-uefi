# Beyond2LTE Windows ARM64 USB bring-up

Bu dizin Exynos9820 `DWC_usb31` denetleyicisini Windows'un USB Role Switch
(URS) ve inbox xHCI yığınına bağlayan ilk güvenli bring-up paketidir.

## Bu sürümün kesin kapsamı

- `b2lusb.sys`, `ACPI\BYN0002` üzerinde KMDF 1.15 URS client'tır.
- ACPI üst aygıtı DWC3 (`0x10C00000`), ana DRD PHY (`0x10B00000`) ve yalnız
  USB2 PHY izolasyon biti (`0x1586072C`) kaynaklarını bildirir.
- URS host çocuğu `URS\BYN0002&HOST`, `b2lusb-host.inf` aracılığıyla Microsoft
  inbox `USBXHCI.SYS` yığınına bağlanır. USB klavye/fare için özel HID sürücüsü
  yazılmaz; başarılı xHCI/root-hub numaralandırmasından sonra Windows'un
  `USBHUB3` ve HID class sürücüleri kullanılır.
- `b2lusbctl.exe status|host|device|none`, yöneticiye açık bring-up rol
  denetimidir. İstek URS'e bildirilir; eski role ait çocuk yığını kaldırıldıktan
  sonra `EvtUrsSetRole` Exynos PHY ve DWC3 rolünü değiştirir.
- Samsung vendor USB2 PHY sırası ve device/host tune sütunları uygulanır.
  Kaynak revizyonu:
  `Android-Artisan/android_kernel_samsung_exynos9820@9a0a792d227c8ae6cddac0c9d68a8bb9782c44cb`.

Bu sürüm bilerek **USB2-only** çalışır. `LINK_PORT.U3_DISABLE` set edilir;
`0x10AE0000` combo PCS ve `0x10AF0000` combo PMA ne ACPI'ye verilir ne de
sürücüden okunur. Güncel cihaz ölçümünde bu pencereler synchronous external
abort ürettiği için onları açan bir paket güvenli değildir.

## WSL derleme ve paketleme

Normal tam tur:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

`--touch` mevcut dokunmatik profilini korur; hem `--touch` hem de `--usb-test`
ACPI tablolarını aynı turda yeniden derler. `--usb-test`, UEFI `B2LUSB.EFI`
çıktısına ek olarak `tools_b2lusb.sh` çağırır.
Bu betik ayrı `build-b2lusb.ps1` ile Windows MSVC/WDK ARM64 derlemesini yapar,
SYS/EXE ve iki INF'yi ortak katalogla test-imzalar, `boot.wim`e ekler ve her
dosyayı hash ile geri okur. `--baseline`, UEFI tanı uygulamalarını SYSTEM
ESP'den çıkarır fakat WIM içindeki Windows sürücülerini korur.

Yalnız Windows paketini tekrar üretmek için:

```bash
bash tools_b2lusb.sh
bash cache_esp.sh --win --system --baseline
```

Önceden derlenmiş, kaynaklardan yeni olmayan ARM64 çıktıları yeniden
paketlenecekse mevcut proje sözleşmesi kullanılır:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lusb.sh
```

Betik `USBXHCI.SYS` ve `USBHUB3.SYS` dosyalarının hedef WinPE WIM'inde
bulunduğunu doğrulamadan paket üretmez. Çıplak ADK WinPE'de
`UrsCx01000.sys` yoksa `tools_winpe_urs_runtime.sh` otomatik çağrılır. Bu
betik Microsoft binary'sini ve `Services\\UrsCx01000` alt ağacını aynı
build'deki ARM64 Windows `install.wim`/`install.esd` kaynağından alır; servis
değerlerini tahmin etmez. UUP ISO otomatik bulunamazsa kaynak açıkça verilir:

```bash
B2L_WINDOWS_ARM64_SOURCE="/mnt/c/.../Windows11_ARM64.iso" \
  bash tools_b2lusb.sh
```

Tam eşleşen Windows build'i tercih edilir. WDK ve binary adıyla sürümlenmiş
`UrsCx 1.0` için aynı Windows ailesindeki daha eski runtime'ın daha yeni WinPE
üzerinde kullanılması deneysel uyarıyla kabul edilir; daha yeni runtime'ın eski
WinPE'ye eklenmesi reddedilir. Birden fazla edition içeren imajlarda gerekirse
`B2L_WINDOWS_ARM64_INDEX=<indeks>` kullanılır. Offline SYSTEM hive aktarımı için
`libhivex-bin` ve `libwin-hivex-perl` gerekir.

Build tarafında URS SDK başlığı WDK'nın sürümlü alt dizininden
`Include/<sürüm>/km/urs/1.0/UrsCx.h`, import library ise
`Lib/<sürüm>/km/arm64/urs/1.0/UrsCxStub.lib` olarak alınır. Bunlar Windows kurulum
ISO'sundan değil, eşleşen SDK/WDK kurulumundan gelir. Farklı bir WDK kökü
gerekiyorsa `B2L_WDK_ROOT='/mnt/.../Windows Kits/10'` verilebilir.

## İlk cihaz testi

`startnet.cmd` iki INF'yi yükler ve ilk durumu basar. Rol otomatik olarak host
yapılmaz; ölçüm kabuğunda açıkça seçilir:

```text
X:\Windows\B2LUSB\b2lusbctl.exe status
X:\Windows\B2LUSB\b2lusbctl.exe host
pnputil /enum-devices /deviceid "URS\BYN0002&HOST"
```

İlk host testi, MAX77705 henüz VBUS source/CC state yönetmediği için haricen
beslenen bir USB2 hub üzerinden yapılmalıdır. Başarı sırası:

1. `current=HOST`, `GCTL.PRTCAP=HOST`, `GSTS.CURMOD=HOST`;
2. `URS\BYN0002&HOST` için `USBXHCI` servisinin başlaması;
3. root hub ve haricen beslenen hub'ın numaralandırılması;
4. USB klavyenin Windows'ta HID Keyboard olarak görünmesi ve tuş üretmesi.

Geri dönüş:

```text
X:\Windows\B2LUSB\b2lusbctl.exe none
```

Sürücü çalışan bir device controller'ı durduramazsa veya çalışan xHCI yığını
halt olmadan rol değiştirmeye kalkarsa geçişi `STATUS_DEVICE_BUSY` ile reddeder.

## Henüz tamamlanmayan kapılar

1. **USB 3.x PHY:** FSYS0 ownership/clock düzeltildikten sonra combo PCS/PMA
   salt-okuma preflight, PLL/CDR lock ve ancak sonra ACPI kaynak genişletmesi.
2. **Gerçek Type-C rolü:** HSI2C45 ve MAX77705 için KMDF/UcmCx connector
   client; CC orientation, data mux, VBUS/VCONN ve PD state machine. Mevcut
   `IdGround/IdFloat` yolu yalnız bring-up override'ıdır.
3. **Function stack:** `URS\BYN0002&FUNCTION` için hedef WIM'deki
   `UfxSynopsys.inf` bölümünün doğrulanması ve function policy.
4. **KDNET-EEM-USB:** USB3 device yolu kararlı olduktan sonra `DBG2` içinde
   Network debug port type `0x8003`, subtype/vendor `0x5143`, DWC3 tabanı ve
   `\_SB.URS0` namespace yolu; ardından hedefte `kdnet.exe` ile destek ve
   transport doğrulaması. Bu tablo, çalışmayan bir debug portu Windows'a
   ilan etmemek için henüz eklenmemiştir.

Microsoft mimari referansları:

- <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table>
