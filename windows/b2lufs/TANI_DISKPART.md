# DiskPart tanısı: Location Path ve volume görünmemesi (rev 3 — ÇÖZÜLDÜ)

Tarih: 2026-08-16. Rev 2: saha görüntüleriyle (GPT `*` yok, tek
`Primary 119 GB, Offset 4096 B`) uyumsuz olan volume teşhisi düzeltildi.

## 0. Sonuç (rev 3)

Enstrümantasyon kök nedeni tek boot'ta yakaladı:
`MediaWindowsGptSnapshotReason=3` — Samsung'un 4Kn GPT başlığı
`HeaderSize=4096` taşıyor; prob (92..BlockLength + kaynak CRC) kabul
ederken snapshot kurucudaki eski `HeaderSize==92` tam-eşitlik kapısı
reddediyordu. Snapshot kapalı kalınca `EnumerationReady=0` keşif kapısı
diski bilinçli olarak gizledi (bkz. §3.4'teki "sessiz snapshot kapısı"
öngörüsü). Düzeltme: kaynak başlık boyutu proba bırakıldı; Windows
görünümündeki birincil ve yedek başlıklar `HeaderSize=92`ye
kanonikleştirilir, `92..4095` rezerv alan sıfırlanır, entries- ve
header-CRC 92 bayt üzerinden yeniden hesaplanır; self-check aynı kanonik
değerleri doğrular (b2lufs_transport.c: 2332–2346, 2431, 2494/2530/2574/2601).
Bu revizyondan sonraki disk-boot paketi `DriverVer 0.0.6.0`,
`SchemaVersion 25` taşır. CACHE'e ek olarak SYSTEM girdisi de salt Windows
görünümünde Basic Data yapılır ve `MediaWindowsSystemEntryFound/Index` ile
raporlanır. Aşağıdaki bölümler önceki GPT tanı sürecinin kaydıdır.

## 1. `Location Path : UNAVAILABLE` — kapatıldı, düzeltilmeyecek

DiskPart bu satırı `<adapter LocationPaths>#<BusType>(PxxTyyLzz)` olarak
kendisi sentezler ve biçim yalnız ATA/SATA, SCSI, SAS, RAID için tanımlıdır;
UFS jetonu yoktur. `DEVPKEY_Device_LocationPaths` değerini Windows üretir,
INF/miniport yazamaz. Miniport tarafında yapılacak bir şey yok; gerçek konum
`pnputil /properties` çıktısındaki ACPI `LocationPaths` değerinden izlenir.

## 2. Volume sorununun GERÇEK aşaması: partmgr GPT'yi kabul etmiyor

Son görüntüler `list disk`te GPT `*` işareti olmadığını ve `list partition`ın
yalnız diski kaplayan `Primary 119 GB, Offset 4096 B` gösterdiğini kanıtlıyor.
Bu, koruyucu MBR'ın 0xEE girdisinin MBR bölümü olarak yorumlanması demektir:
Windows LBA 0'ı okumuş, LBA 1'deki GPT'yi **reddedip** MBR yorumuna düşmüştür.
Dolayısıyla "Android bölümleri listeleniyor ama volume çıkmıyor" aşamasına
henüz gelinmedi; volume/Basic-Data/CACHE tartışması ancak GPT kabulünden
sonra anlamlıdır.

Düzeltmeler (önceki revizyona göre):

- CACHE adı bulunamazsa snapshot yine geçerli kalır; yalnız
  `MediaWindowsCacheEntryFound=0` olur (b2lufs_transport.c:2210, 2229).
  "Ad eşleşmezse snapshot geçersiz" ifadesi yanlıştı.
- MSR kullanıcıya volume olarak sunulmaz; sürücü harfi/volume GUID yolu alan
  standart GPT türü Basic Data'dır.
- `list partition`ın Type sütunu ham GUID göstermez; tip doğrulaması CACHE
  seçilip `detail partition` ile yapılır.

## 3. Kod incelemesinden ek bulgular

1. **Kanonik görünüm iç tutarlı görünüyor.** Normalize başlıkta entries-CRC
   (offset 88) `EntryBytes` üzerinden, header-CRC (offset 16) CRC alanı
   sıfırlanarak `HeaderSize` üzerinden yeniden hesaplanıyor (2215–2224);
   backup başlık MyLBA/AlternateLBA/EntryLBA düzeltilip CRC'si yeniden
   yazılıyor (2301–2312). İçerik tarafında bilinen bir red nedeni yok.
2. **Tanı kör noktası A:** `B2LUfsTryCompleteCachedGptRead` SRB'yi BLS2 izi
   BIRAKMADAN tamamlıyor (2808–2821'de `B2LUfsLogScsiTrace` yok). ETW'de
   LBA0/LBA1 READ kaydının yokluğu "partmgr okumadı" demek değildir.
   Öneri: cached yola da BLS2 (veya `GptCachedReadCount` registry sayacı)
   eklenmeli.
3. **Ham yoldaki BLS2 örneği güvenilirdir (rev 2'deki iddia yanlıştı):**
   örnek `SrbExtension->SystemAddress`'ten alınır (547–553) ve bu, ham
   yolun kopya hedefiyle aynı tampondur (3063–3065, iz 3072'de sonra).
   Yani başarılı bir BLS2 READ örneği, Windows'a teslim edilen tampondaki
   baytları gösterir. Tek kalan sınır: okuma, sürücünün yazdığı sanal
   adres üzerinden yapıldığı için olası bir eşleme/önbellek takma-adı
   sorununu ayırt edemez; onu yalnız kullanıcı-kipi ham okuma sınar.
4. **Sessiz snapshot kapısı:** `B2LUfsBuildWindowsGptSnapshot` (2177–2184)
   `MediaProbeGptHeaderSize == 92` gibi TAM eşitlikler ister; prob ise
   92..4096 aralığını kabul eder (2466). Ör. başlık boyutu 96 olsaydı
   `MediaProbeGptHeaderValid=1` kalır ama snapshot sessizce kapanırdı.
   Registry'de "snapshot neden kapalı" değeri yok; `WindowsGptSnapshotReason`
   benzeri bir neden kodu eklenmeli.
5. **Self-check eksik:** Üretilen dört bölgeyi (PMBR, primary header/entries,
   backup entries/header) oluşturulduktan sonra partmgr'ın uygulayacağı
   kurallarla yeniden doğrulayan bağımsız bir denetim yok. Snapshot
   kurulduktan sonra aynı doğrulayıcıdan geçirilip sonucu registry'ye
   yazılmalı; böylece "içerik mi, teslim mi" sorusu boot anında yanıtlanır.

## 4. Karar ağacı (kabul edilen sıra)

1. `list disk` → GPT sütununda `*` aranır.
2. `list partition` → tek 119 GB `Primary` değil, gerçek Samsung bölümleri
   görünmeli.
3. Aynı boot'un registry değerleri: `MediaWindowsGptSnapshotValid=1` ve
   `MediaWindowsCacheEntryFound=1`; 0 ise §3.4'teki ön koşullar
   (`CapacityBlockBytes=4096`, `MediaProbeGptHeaderSize=92`,
   `MediaProbeGptEntrySize=128`, `MediaProbeGptEntryCount≤128`) tek tek
   karşılaştırılır.
4. CACHE seçilip `detail partition` ile
   `EBD0A0A2-B9E5-4433-87C0-68B6B72699C7` doğrulanır.
5. Ancak bundan sonra volume/FAT32 bağlama aşaması.

## 5. Ayırt edici deney

WinPE'de `\\.\PhysicalDrive0` offset 4096'dan 4096 bayt kullanıcı-kipi ham
okuma (WinPE-PowerShell eklendiyse `FileStream`; yoksa b2lcmd'ye küçük bir
raw-read adımı). Bu deney özellikle izsiz cached yolu da sınadığı için
değerlidir. Yorumu:

- `EFI PART` + beklenen alanlar görünüyorsa üst yığına doğru baytlar teslim
  ediliyor demektir; sorun partmgr'ın başka bir doğrulama koşulundadır.
- Görünmüyorsa tek başına "son kopya bozuk" sonucu ÇIKMAZ; snapshot'ın hiç
  kullanılmaması, cached yol eşlemesi veya farklı bir okuma aralığının ham
  yola düşmesi ihtimalleri açık kalır.

En güçlü sonraki adım, cached GPT yoluna özel bir iz eklemektir: sayaç,
StartLBA/blok sayısı, mapping kaynağı (SrbExtension / GetSystemAddress /
DataBuffer fallback) ve kopyalama SONRASI `SystemAddress`'ten okunan ilk
64 bayt. Bu iz, izsiz cached yolu görünür kılar ve teslim/içerik ayrımını
tek boot'ta kapatır.

## Kaynaklar

- https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/hard-disk-location-path-format
- https://learn.microsoft.com/en-us/windows-hardware/drivers/install/devpkey-device-locationpaths
- https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/createpartition-msft-disk
