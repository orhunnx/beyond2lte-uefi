# EL3 Smart Exception Handler — sıfırlamayı doğrudan yakalama planı

Kırk turluk elemeden sonra geriye tek aktör kaldı: **EL3.** Bu belge, onu
gözlemlemenin somut yolunu tarif ediyor.

## Bulgu

Stok kernel (`boot.img`, `strings`) ve `ianmacd/d2s` kaynağı — d2s tam olarak
bizim cihaz, Galaxy S10+ Exynos.

`drivers/soc/samsung/exynos-el3_mon.c`:

```c
static void exynos_smart_exception_handler(unsigned int id,
        unsigned long elr, unsigned long esr, unsigned long sctlr,
        unsigned long ttbr, unsigned long tcr, unsigned long x6,
        unsigned int offset);

ret = exynos_smc(SMC_CMD_SET_SEH_ADDRESS, addr, 0, 0);
arch_initcall(exynos_set_seh_address);
```

**EL3 monitörü beklenmedik bir istisna aldığında, çekirdeğin kaydettiği bu
adrese atlıyor** ve EL3 bağlamını argüman olarak veriyor. Samsung bunu
`arch_initcall`'da, açılışın çok erken bir noktasında kaydediyor.

Kaydedilmemişse EL3'ün atlayacak yeri yok. Bir güvenlik monitörünün başka
seçeneği de yok — sıfırlar. Sessizce, işletim sistemine haber vermeden.

Bu, bütün gözlemlerimize oturuyor:

| Gözlem | Açıklama |
|---|---|
| Bugcheck yok | Kararı Windows vermiyor |
| Çekirdek son ana kadar sağlıklı | Ölüm anlık |
| Blokaj tamamen önlüyor | Tetikleyen iş yapılmıyor |
| `ResetSystem` çağrılmıyor | Sıfırlayan Windows değil |
| PSCI kapalıyken de aynı | Windows'un elinde yol yok, yine de oluyor |
| Linux çalışıyor | Hem SEH kaydediyor hem istisnayı doğurmuyor |

## SMC numaraları (`include/linux/smc.h`)

```c
#define SMC_CMD_SET_DEBUG_MEM       (-120)   // EL3'e GPR dokumu icin bellek
#define SMC_CMD_GET_LOCKUP_REASON   (-121)
#define SMC_CMD_KERNEL_PANIC_NOTICE (-122)
#define SMC_CMD_SET_SEH_ADDRESS     (-123)
#define SMC_CMD_PROTECT_KERNEL_TEXT (-125)
```

Çağrı biçimi: `exynos_smc(cmd, arg1, arg2, arg3)` → `smc #0` ile
`x0=cmd, x1=arg1, x2=arg2, x3=arg3`. Negatif değerler 64 bite işaret
genişletiliyor (`-123` → `0xFFFFFFFFFFFFFF85`).

## Neden bu, EL2 arka kapısından iyi

`B2LEl2Dxe` TrustZone'un **belgelenmemiş** bir arka kapısını kullanmaya
çalışıyordu ve hiç çalışmadı. Bu ise Samsung'un **kendi çekirdeğinin
kullandığı, kaynağı elimizde olan** resmi arayüzü. Aynı SMC'yi biz de
yapabiliriz.

## Tasarım

### 1. `SMC_CMD_SET_DEBUG_MEM` önce — çünkü FİZİKSEL adres alıyor

Linux bunu `late_initcall`'da fiziksel adresle çağırıyor
(`virt_to_phys(smc_debug_mem)`, 2 sayfa). EL3 oraya bütün genel amaçlı
yazmaçları döküyor ve `offset`'i işleyiciye veriyor.

Fiziksel olması bizim için büyük kolaylık: sanal adres çevirme derdi yok.
`B2L RT Log` bölgesinden 8 KB ayırıp adresini veriyoruz.

### 2. `SMC_CMD_SET_SEH_ADDRESS` — burada sanal adres sorunu var

Linux **sanal** adres kaydediyor; EL3 çekirdeğin çeviri düzeninde `eret`
yapıyor. Yani adres, o an geçerli olan EL1 sanal adres uzayında anlamlı
olmalı.

- UEFI içindeyken: kimlik haritalama, fiziksel adres yeterli.
- Windows devraldıktan sonra: `SetVirtualAddressMap` sonrası bizim çalışma
  zamanı kodumuz Windows tarafından haritalanıyor. `EfiConvertPointer` ile
  işleyicinin yeni adresini öğrenip **SEH'i yeniden kaydediyoruz.**

Bu, `B2LRtHookDxe`'de zaten kurduğumuz düzenek — `gEfiEventVirtualAddressChange`
olayında çeviri yapıyoruz ve çalıştığı kanıtlandı (`GetVariable` 372/372).

### 3. İşleyici ne yapacak

Assembly olmalı: sekiz argüman `x0`-`x7`'de geliyor ve C ABI'siyle uyumlu
olsa da, çağrıldığı bağlam güvenilmez.

```
1. x0..x7'yi kayit bolgesine yaz  (fiziksel degil, o an gecerli olan
   haritalama uzerinden - RtData bolgesi Windows tarafindan haritali)
2. "SEH!" imzasi yaz
3. Sonsuz dongu
```

Sonsuz döngü kasıtlı: cihaz **sıfırlanmasın** ki ekranda donup kalsın. O anda
`b2lfb`'nin çizdiği şeritler ekranda duruyor olacak ve kaç saniyede olduğunu
göreceğiz. Kaydı okumak için sıcak sıfırlama gerekiyorsa, döngü yerine PMU'ya
yazarak sıcak sıfırlama da yapılabilir — ama o PMU'yu çalışma zamanı olarak
haritalamayı gerektirir.

### 4. Okuma

`startup.nsh` zaten `0xCF040000` civarını döküyor. `SEH!` imzası + `esr_el3`
görünürse **sıfırlamanın sebebi elimizde** demektir.

`esr_el3`'ün üst altı biti (`EC`) istisna sınıfını veriyor:

| EC | Anlam |
|---|---|
| `0x24`/`0x25` | Veri erişim hatası — EL3 korumalı bir adrese dokundu |
| `0x16` | `HVC` |
| `0x17` | `SMC` — geçersiz bir SMC geldi |
| `0x0E` | Illegal execution state |

`0x17` çıkarsa Windows'un yaptığı bir SMC'yi EL3 anlamamış demektir.
`0x24` çıkarsa EL3 bir bellek erişiminde patlamış.

## Riskler

- SEH kaydı `EXYNOS_ERROR_ALREADY_INITIALIZED (1)` dönebilir; S-Boot zaten
  kaydetmiş olabilir. Dönüş değeri kayda yazılmalı.
- Bir kez kaydedildikten sonra değiştirilemeyebilir.
- İşleyici yanlış bağlamda çalışırsa cihaz kilitlenir — ama zaten sıfırlanıyor,
  kaybedecek bir şey yok.

## Kaynaklar

- `ianmacd/d2s` — Galaxy S10+ Exynos çekirdeği,
  `drivers/soc/samsung/exynos-el3_mon.c` ve `include/linux/smc.h`
- Stok `boot.img`'den `strings`: `exynos_set_seh_address`,
  `"There has been an unexpected exception from the EL3 monitor"`,
  `elr_el3/esr_el3/sctlr_el3/ttbr_el3/tcr_el3/scr_el3`
