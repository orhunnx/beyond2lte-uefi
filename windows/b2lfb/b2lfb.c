/*
 * B2LFB - Windows cekirdeginin icinden konusan en kucuk surucu.
 *
 * ---------------------------------------------------------------------------
 * NEDEN
 * ---------------------------------------------------------------------------
 * Otuz turdur Windows'un icinden tek bir bayt duyamiyoruz. Her arizada
 * elimizde yalnizca "kac saniye sonra sifirladi" var, ve bu yuzden gerileme
 * avlari tur tur uzuyor.
 *
 * Bu surucu iki kanal aciyor ve ikisi de bagimsiz:
 *
 *   1. FRAMEBUFFER  - canli. Ekranin ustune renkli bant ciziyor.
 *   2. AYRILMIS RAM - olum sonrasi. 0xCF030000'a imza + sayac yaziyor;
 *                     cihaz cokup sifirlandiktan sonra startup.nsh zaten
 *                     "dmem 0xCF030000 60" ile o adresi dokuyor. Yani
 *                     STARTUP.NSH'E HIC DOKUNMADAN calisiyor.
 *
 * Sicak sifirlamada DRAM icerigi kaliyor - olculdu (B2LResetHookDxe sayaci
 * 0x403 -> 0x404). Kural: cokusten sonra cihazi elle kapatma.
 *
 * ---------------------------------------------------------------------------
 * NEDEN ACILIS SURUCUSU (Start=0)
 * ---------------------------------------------------------------------------
 * winload cekirdegi baslatirken acilis surucilerini yukluyor - yani su an
 * oldugumuz noktadan ONCE. Mevcut ariza duzelmeden de bu surucu calisir ve
 * "buraya kadar geldik" der. Gerileme avinin beklemesine gerek yok.
 *
 * ---------------------------------------------------------------------------
 * NEDEN WDK YOK
 * ---------------------------------------------------------------------------
 * Ihtiyacimiz olan cekirdek API'si iki fonksiyon. WDK basliklarini
 * kullanmak yerine ikisini elle bildiriyoruz; boylece surucu WSL'de clang
 * ile derleniyor, Visual Studio gerekmiyor. Ayrintili gerekce ve derleme
 * komutlari tools_b2lfb.sh icinde.
 */

typedef long               NTSTATUS;
typedef unsigned char      UINT8;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
/* KeQueryPerformanceCounter isaretli doner; WDK yok, tipi elle tanimliyoruz. */
typedef signed long long   INT64;
typedef void              *PVOID;

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef union _PHYSICAL_ADDRESS {
  struct {
    UINT32 LowPart;
    int    HighPart;
  } u;
  long long QuadPart;
} PHYSICAL_ADDRESS;

/*
 * MEMORY_CACHING_TYPE. MmNonCached seciliyor: yazma dogrudan gidiyor,
 * onbellekte bekleyip cokuste kaybolmuyor. Bir hata ayiklama kanalinda
 * hizdan cok GORUNURLUK onemli.
 */
#define MmNonCached     0
#define MmCached        1
#define MmWriteCombined 2

__declspec(dllimport) PVOID MmMapIoSpace (
  PHYSICAL_ADDRESS PhysicalAddress,
  UINT64           NumberOfBytes,
  UINT32           CacheType);

/*
 * BUGCHECK GERI CAGRISI - FAZ 2'NIN TEK AMACI
 *
 * Otuz turdur cevaplayamadigimiz soru: 10 saniyelik olum bir bugcheck mi,
 * yoksa SoC'yi sifirlayan baska bir sey mi?
 *
 * Ikisi ekranda AYNI gorunuyor cunku Windows'un kendi mavi ekrani bu
 * platformda cizilmiyor. Ama cekirdek, bugcheck sirasinda kayitli geri
 * cagrilari CAGIRIYOR - ve biz o an framebuffer'a yazabiliyoruz.
 *
 * Ayrim kesin:
 *   Ekran YESILE donuyor -> bugcheck. Windows cokuyor ve bunu biliyor.
 *   Macenta kaliyor      -> bugcheck DEGIL. Bir sey SoC'yi disaridan
 *                           sifirliyor; cekirdegin haberi bile yok.
 *
 * Ikisi tamamen farkli yerlere baktiriyor ve bu ayrimi ilk kez yapabiliyoruz.
 *
 * KBUGCHECK_CALLBACK_RECORD'un ic yapisini bildirmiyoruz: cekirdek icine
 * yaziyor, bize sadece yeterince buyuk ve SIFIRLANMIS, sayfalanamaz bir
 * alan lazim. Surucunun .data bolumu zaten sayfalanamaz. Sifir olmasi
 * onemli - KeInitializeCallbackRecord'un yaptigi tek is State alanini
 * BufferEmpty (0) yapmak.
 */
__declspec(dllimport) UINT8 KeRegisterBugCheckCallback (
  PVOID  CallbackRecord,
  PVOID  CallbackRoutine,
  PVOID  Buffer,
  UINT32 Length,
  UINT8 *Component);

/*
 * KALP ATISI - cekirdek kac saniye yasadi?
 *
 * Bugcheck olmadigini ogrendik ama "ne kadar sure ayakta kaldi" sorusu hala
 * kronometreyle olculuyor. Saniyede bir kare cizen bir zamanlayici bunu
 * ekranda sayilabilir hale getiriyor - EbsTest'te ayni fikir ise yaramisti.
 *
 * Ayrica cok daha onemli bir sey soyluyor: kareler SON ANA KADAR artiyorsa
 * cekirdek olene dek zamanlayici isletiyordu, yani saglikliydi. Belli bir
 * noktada donuyorsa is orada bitiyor.
 *
 * KTIMER ve KDPC yapilarinin ic duzenini bildirmiyoruz; cekirdek iceri
 * yaziyor, bize yeterince buyuk ve hizali alan lazim. Ikisi de 64 bitte
 * 0x40 bayt; 128 bayt fazlasiyla yeterli.
 */
/*
 * BOSTA ISLEMCIYI ENGELLEME - EN GUCLU HIPOTEZ
 *
 * Iki "sifirlama olmadi" olcumunun ORTAK NOKTASI:
 *   - EbsTest mesgul dongude 60+ saniye yasadi
 *   - DriverEntry blokajinda sonsuz dongu ceviriyorduk, 25+ serit
 * Ikisinde de ISLEMCI HIC BOSA DUSMEDI.
 *
 * Exynos'un PMU'su, butun cekirdekler WFI'ye girdiginde sistemi dusuk guc
 * durumuna sokacak sekilde yapilandirilabiliyor. Uyandirma kaynaklari dogru
 * kurulmamissa oradan donus olmaz. Linux cpuidle surucusuyle bunu dogru
 * yapiyor; bizim DSDT'de ne _LPI var ne _CST, yani Windows HAL'in
 * varsayilan WFI'sini kullaniyor.
 *
 * Zamanlama da oturuyor: Windows ancak acilis yeterince ilerleyince gercek
 * bosta zamana sahip oluyor - yaklasik on saniye.
 *
 * TEST: bosta is parcaciginin yerine gecen, EN DUSUK oncelikte donen bir
 * is parcacigi. Bosta is parcacigi oncelik 0'da; 1'deki bir is parcacigi
 * her zaman ondan once secilir. Acilis normal ilerler cunku diger her sey
 * daha yuksek oncelikte, ama CPU hic WFI'ye girmez.
 */
__declspec(dllimport) long PsCreateSystemThread (
  PVOID *ThreadHandle, UINT32 DesiredAccess, PVOID ObjectAttributes,
  PVOID ProcessHandle, PVOID ClientId, PVOID StartRoutine, PVOID StartContext);
__declspec(dllimport) PVOID KeGetCurrentThread (void);
__declspec(dllimport) long  KeSetPriorityThread (PVOID Thread, long Priority);
__declspec(dllimport) INT64 KeQueryPerformanceCounter (INT64 *Frequency);

__declspec(dllimport) void KeInitializeDpc (PVOID Dpc, PVOID Routine, PVOID Context);
__declspec(dllimport) void KeInitializeTimer (PVOID Timer);
__declspec(dllimport) UINT8 KeSetTimerEx (PVOID Timer, long long DueTime, int Period, PVOID Dpc);

/*
 * BOOT YASAM DONGUSU ISARETI
 *
 * I/O yoneticisi bu callback'i butun aygitlari enumerate edip baslattiktan
 * sonra, PASSIVE_LEVEL'de bir sistem thread'inde cagirir. Image/process
 * bildirim kancalari burada BILEREK yok: ikisi de onceki denemelerde
 * b2lfb'yi 0x1E ile dusurdu. Bu ise boot-suruculeri icin tasarlanmis,
 * kayda deger bir sinirdir.
 */
typedef void (*PDRIVER_REINITIALIZE) (PVOID DriverObject, PVOID Context, UINT32 Count);
__declspec(dllimport) void IoRegisterBootDriverReinitialization (
  PVOID DriverObject,
  PDRIVER_REINITIALIZE DriverReinitializationRoutine,
  PVOID Context);

/* ------------------------------------------------------------------------ */

/*
 * Framebuffer. Adres bellek haritamizdaki "Display Reserved" bolgesi:
 *   {"Display Reserved", 0xCA000000, 0x01400000, ..., Reserv, WRITE_THROUGH}
 *
 * COZUNURLUK VE STRIDE BILEREK VARSAYILMIYOR. Ekranin genisligini, satir
 * uzunlugunu, piksel bicimini bilmiyoruz ve yanlis tahmin edersek hicbir
 * sey gormeyiz - ki bu, surucunun calismadigi sonucunu cikarttirir.
 *
 * Onun yerine bolgenin BASINDAN itibaren duz bir blok dolduruyoruz. Stride
 * ne olursa olsun ekranin ustunde bir bant belirir. Renk her 32 bitte bir
 * tekrar ettigi icin piksel bicimi (BGRA/RGBA) yalnizca rengi degistirir,
 * bandin gorunurlugunu degistirmez.
 */
/*
 * ILK SURUM SADECE BOLGENIN BASINA ~120 SATIRLIK BANT YAZIYORDU VE HICBIR
 * SEY GORUNMEDI. Hata: framebuffer'in bolgenin BASINDA oldugunu varsaydim.
 * "Display Reserved" 20 MB'lik bir alan ve GOP'un gercek tabani icinde
 * herhangi bir yerde olabilir.
 *
 * Tahmini tamamen kaldiriyoruz: BOLGENIN TAMAMINI dolduruyoruz. Framebuffer
 * icinde nerede olursa olsun ekran maceneta oluyor. 20 MB'lik dword yazma
 * bu cekirdekte goz acip kapayincaya kadar bitiyor.
 */
#define FB_PHYS   0xCA000000ULL
#define FB_BYTES  0x01400000ULL   /* bolgenin TAMAMI - 20 MB */

/*
 * ARTIK EKRANIN TAMAMINI BOYAMIYORUZ.
 *
 * Macenta ekran isini gordu: surucunun calistigini kanitladi. Ama Windows'un
 * KENDI gorsellerini de orturttu - ozellikle SONME ANIMASYONUNU. O animasyon
 * bu projede zaten bir olcuydu: "kabuk duzgun cikti, cokme yok" demekti
 * (commit 43ef06c).
 *
 * Simdi bugcheck OLMADIGINI biliyoruz ve en guclu aciklama "cokme degil,
 * WinPE isini bitirip kendini yeniden baslatiyor". O aciklamayi sinamanin
 * yolu tam da sonme animasyonunu gorebilmek.
 *
 * Bu yuzden yalnizca ince bir serit boyuyoruz; gerisi Windows'a kaliyor.
 */
#define BANT_BYTES  (FB_BYTES / 32)

/*
 * Kalp atisi kareleri seridin hemen ardindan basliyor ve her tik bir
 * parca ilerliyor. Ekran geometrisini bilmeye gerek yok: buyuyen bir alan
 * zaten sayilabilir bir ilerleme cubugu.
 */
#define TIK_BYTES   (64ULL * 1024ULL)
#define TIK_AZAMI   ((FB_BYTES - BANT_BYTES) / TIK_BYTES)

/*
 * BOYAMA KAC TIK SURUYOR.
 *
 * Serit boyamak framebuffer'a yaziyor. WinPE komut istemini ekrana
 * cizmeye basladiginda ikisi birbirinin ustune yazar ve ekran okunmaz
 * hale gelir. Seritler yalnizca rahatlik; asil kayit 0xCF030000'daki
 * ASCII metin ve o sicak sifirlamayi asiyor.
 *
 * 30 tik = 3 saniye: erken ilerlemeyi gormeye yetiyor, WinPE'nin ekrani
 * devralmasindan once bitiyor.
 */
#define BOYA_AZAMI  30

/*
 * Olum sonrasi kayit. startup.nsh su an tam olarak burayi dokuyor:
 *     dmem 0xCF030000 60
 * Yani bu adresi secmek, Shell tarafinda sifir degisiklik demek.
 */
#define LOG_PHYS  0xCF030000ULL
#define LOG_BYTES 0x100ULL

static void FillDword (PVOID Base, UINT64 Bytes, UINT32 Value)
{
  UINT32 *P = (UINT32 *)Base;
  UINT64  N = Bytes / 4;
  UINT64  I;

  for (I = 0; I < N; I++) {
    P[I] = Value;
  }
}

/*
 * Haritalanmis adresler global tutuluyor.
 *
 * SEBEBI KRITIK: bugcheck aninda IRQL cok yuksek ve cekirdek API'lerinin
 * neredeyse tamami yasak - MmMapIoSpace dahil. O an haritalama YAPAMAYIZ.
 * Bu yuzden DriverEntry'de bir kez haritalayip isaretcileri sakliyoruz;
 * geri cagrida yalnizca hafizaya yaziyoruz, ki o serbest.
 */
static PVOID gFb  = 0;
static PVOID gLog = 0;

/*
 * TIK, zamanlayici DPC'sinin kac kez calistigini soyler; DPC yogunlukta
 * gecikirse bunun duvar saatiyle bire bir oldugunu varsayamayiz. QPC ayni
 * araligi bagimsiz sayar. Her ikili degeri 32 bite kisitliyoruz: bu deneyde
 * saniyeler olculuyor, 19.2 MHz gibi tipik bir QPC'de 32 bit dakikalarca
 * yeterlidir ve 64-bit bolme / CRT yardimcisi gerektirmez.
 */
static INT64 gQpcBaslangic = 0;
static INT64 gQpcFrekans   = 0;

/* Cekirdek bu alanin icine yaziyor; sifir olmasi ve sayfalanamaz olmasi yeter */
static UINT64 gCallbackRecord[16];

static UINT8 gComponent[8] = { 'b','2','l','f','b',0,0,0 };

/* Cekirdek bunlarin icine yaziyor - 128 bayt, 16 bayt hizali */
__declspec(align(16)) static UINT64 gTimer[16];
__declspec(align(16)) static UINT64 gDpc[16];

static volatile UINT64 gTik = 0;

static void B2LYazHex8 (volatile UINT8 *P, UINT32 V)
{
  int K;

  for (K = 7; K >= 0; K--) {
    UINT8 D = (UINT8)(V & 0xF);
    P[K] = (UINT8)((D < 10) ? ('0' + D) : ('A' + D - 10));
    V >>= 4;
  }
}

/*
 * +0xA0, Service Control Manager ayrim damgasi:
 *
 *   AUT=REG C=000  -> boot surucusu sonraki asama icin damgayi kurdu
 *   AUT=SCM C=001  -> Start=2 b2lauto.sys, SCM tarafindan baslatildi
 *
 * Ikinci durum sadece ayri, cok kucuk bir auto-start surucusu tarafindan
 * yazilir. Bu ilk deger her DriverEntry'de tekrar yazilir: onceki sicak
 * sifirlamadan kalan AUT=SCM sonucu yeni acilista yanlis pozitif olmaz.
 */
static void B2LAutoStartRecord (void)
{
  volatile UINT8 *P;

  if (gLog == 0) {
    return;
  }

  P = (volatile UINT8 *)gLog + 160;
  P[0]  = 'A'; P[1]  = 'U'; P[2]  = 'T'; P[3]  = '=';
  P[4]  = 'R'; P[5]  = 'E'; P[6]  = 'G'; P[7]  = ' ';
  P[8]  = 'C'; P[9]  = '='; P[10] = '0'; P[11] = '0'; P[12] = '0';
  P[13] = ' '; P[14] = ' '; P[15] = ' ';
}

/*
 * +0x70, 16 baytlik boot-reinit kaydi:
 *
 *   BRI=REG C=000   -> callback kaydedildi, henuz cagrilmadi
 *   BRI=0010 C=001  -> tum aygitlar basladi; o anda TIK=10 (1 saniye)
 *
 * Bu alan HeartbeatDpc'nin (+0x10/+0x20/+0x60) yazdigi alanlardan ayridir;
 * kilit ya da yuksek IRQL'de bekleme gerekmez.
 */
static void B2LBootReinitRecord (UINT32 Tick, UINT32 Count, UINT8 Called)
{
  volatile UINT8 *P;

  if (gLog == 0) {
    return;
  }

  P = (volatile UINT8 *)gLog + 112;
  P[0] = 'B'; P[1] = 'R'; P[2] = 'I'; P[3] = '=';
  if (Called == 0) {
    P[4] = 'R'; P[5] = 'E'; P[6] = 'G'; P[7] = ' ';
  } else {
    P[4] = (UINT8)('0' + ((Tick / 1000) % 10));
    P[5] = (UINT8)('0' + ((Tick /  100) % 10));
    P[6] = (UINT8)('0' + ((Tick /   10) % 10));
    P[7] = (UINT8)('0' + ( Tick         % 10));
  }
  P[8] = ' '; P[9] = 'C'; P[10] = '=';
  P[11] = (UINT8)('0' + ((Count / 100) % 10));
  P[12] = (UINT8)('0' + ((Count /  10) % 10));
  P[13] = (UINT8)('0' + ( Count         % 10));
  P[14] = ' '; P[15] = ' ';
}

/* I/O Manager: tum aygitlar enumerate edilip baslatildi. */
static void B2LBootReinitialize (PVOID DriverObject, PVOID Context, UINT32 Count)
{
  (void)DriverObject;
  (void)Context;

  /* gTik DPC'de artar; anlik bir tik farki anlami degistirmez. */
  B2LBootReinitRecord ((UINT32)gTik, Count, 1);
}

/*
 * Bosta donguisunun tur sayaci.
 *
 * NEDEN AYRI BIR SAYAC: turkuaz bant ve gLog[10]='T' yalnizca is
 * parcaciginin BASLADIGINI kanitliyor. Iddia ise baska: "CPU hic bosa
 * dusmedi". Bunun icin dongunun sifirlama anina KADAR dondugunu gostermek
 * gerekiyor. Baslayip sonra hic zamanlanmamis bir is parcacigi da ayni
 * bandi birakirdi.
 *
 * Bu ayrimi es gecmenin bedelini bu projede zaten odedik: sanal SEH
 * kaydini "calismadi" diye okuduk, oysa olay calismisti ve gordugumuz sey
 * EL3'un cevabiydi.
 */
static volatile UINT64 gSpin = 0;

/*
 * DONGUNUN SERBEST HIZI.
 *
 * NEDEN: iki acilis ayni goruntuyle TIK=0102/SPN=11M ve TIK=0258/SPN=1.73G
 * verdi. Ikisi de "sabit sure" ve "sabit tur sayisi" modellerini cürütüyor,
 * ama yerine koyacagimiz model - "Windows belli miktarda IS yapinca oluyor"
 * - hesaplanamiyor: 1.73 milyar tur 25 saniyenin tamami da olabilir, ucte
 * biri de. Donguinin CPU'yu tek basina aldigi andaki hizini bilmeden toplam
 * tur sayisi hicbir seye cevrilemez.
 *
 * Cozum: her tikte o tikteki tur sayisina bak ve gorulen EN BUYUGUNU sakla.
 * En buyuk delta, dongunun rakipsiz kaldigi bir 100 ms dilimidir; yani
 * serbest hiz. Ondan sonra
 *
 *     Windows'un CPU suresi = toplam sure - (toplam tur / serbest hiz)
 *
 * dogrudan hesaplanabiliyor ve iki acilis karsilastirilabilir hale geliyor.
 */
/*
 * GIC dagiticisi - WINDOWS'IN ICINDEN.
 *
 * NEDEN: cikista butun SPI'lari kapattik ve bekleyen bitlerini temizledik,
 * ama bitler ANINDA geri geldi (SUS ONCE=8, SUS SONRA=8). Seviye
 * tetiklemeli bir kesmede bu, kaynagin hala sinyali basili tuttugu anlamina
 * geliyor - DECON kareyi taramaya devam ediyor.
 *
 * Ama kapatilmis bir kesme, bekleyen biti kalksa da CPU'ya ILETILMEZ.
 * Yani firtina ancak Windows o SPI'lari kendisi ACARSA mumkun. Bunu
 * tahmin etmek yerine oluyoruz: Windows kosarken dagiticiyi okuyup kac
 * SPI'in ACIK ve kac tanesinin BEKLIYOR oldugunu her tikte kaydediyoruz.
 *
 *   GE buyuk  -> Windows SPI'lari aciyor, firtina mumkun
 *   GE sifir  -> aciklari yok, firtina hipotezi tamamen duser
 */
static UINT64 gSpinOnceki = 0;
static UINT32 gSpinAzami  = 0;

/*
 * IMAJ YUKLEME KANCASI KALDIRILDI - HEM COKTU HEM SINYAL VERMIYORDU.
 *
 * PsSetLoadImageNotifyRoutine ile her surucu imajinin adini kaydediyorduk.
 * Iki ayri sebeple cikti:
 *
 * 1) SINYAL YOKTU. Ayni goruntuyle uc acilista CNT 16, 3 ve 21 cikti, imaj
 *    adi her seferinde baska. Olum ani ise kararli (TIK ~10, CPU yariya
 *    inince 24). Yani son yuklenen imajin adi sucluyu degil, olumun
 *    yukleme sirasinin neresine denk geldigini soyluyordu.
 *
 * 2) COKUYORDU. Cihaz KMODE_EXCEPTION_NOT_HANDLED (0x1E) / b2lfb.sys verdi.
 *    PsSetCreateProcessNotifyRoutine denemesi de ayni sonu bulmustu.
 *
 * Olcum aracimizi dusuren ve karsiliginda bilgi vermeyen bir kanca
 * tutulmaz. Ustelik ayni isi BCD'deki "sos" cekirdegin kendi tarafindan,
 * hicbir seye kanca takmadan yapiyor: yuklenen surucileri ve baslatma
 * iletilerini dogrudan ekrana yaziyor.
 */

/*
 * Bosta is parcaciginin yerine gecen dongu. Oncelik 1: bosta is parcacigi
 * (0) disinda HER SEY bundan once calisir, dolayisiyla acilis yavaslamaz.
 */
static void BostaDongu (PVOID Ctx)
{
  volatile UINT64 I = 0;

  (void)Ctx;

  KeSetPriorityThread (KeGetCurrentThread (), 1);

  /*
   * CALISTIGINI KANITLA.
   *
   * Ilk denemede sonuc degismedi, ama is parcaciginin gercekten
   * yaratildigini ve onceliginin tuttugunu DOGRULAMADIK. Bu projede
   * dogrulanmamis bir aletin sessizligine guvenmek dort kez yanilttı.
   *
   * Bant hemen ardindan gelen alani turkuaza boyuyoruz: gorunuyorsa
   * is parcacigi calisti, gorunmuyorsa bu deney hicbir sey kanitlamiyor.
   */
  if (gFb != 0) {
    FillDword ((PVOID)((UINT8 *)gFb + BANT_BYTES), TIK_BYTES / 2, 0x0000FFFF);
  }
  if (gLog != 0) {
    ((UINT8 *)gLog)[10] = 'T';
  }

  for (;;) {
    I++;
    gSpin++;
  }
}

/*
 * Saniyede bir. Her tik bir sonraki parcayi boyuyor ve rengi degistiriyor,
 * boylece tikler tek tek sayilabiliyor.
 */
static void HeartbeatDpc (PVOID Dpc, PVOID Ctx, PVOID A1, PVOID A2)
{
  UINT8 *P;

  (void)Dpc; (void)Ctx; (void)A1; (void)A2;

  /*
   * BOYAMA DURUR, SAYAC DURMAZ.
   *
   * Onceki surum burada "gTik >= TIK_AZAMI ise geri don" diyordu ve bu
   * SESSIZ BIR HATAYDI: seritler ekrani doldurdugunda sayac ve kayit da
   * duruyordu. Uzun bir acilista TIK olduugu yerde donar, biz de o sayiyi
   * "olum ani" diye okurduk. 25.8 saniyelik acilista tam olarak bu riske
   * girmisiz.
   *
   * Artik boyama kosullu, sayac ve kayit kosulsuz.
   */
  if ((gFb != 0) && (gTik < BOYA_AZAMI) && (gTik < TIK_AZAMI)) {
    P = (UINT8 *)gFb + BANT_BYTES + (gTik * TIK_BYTES);

    /* Tek/cift tikler farkli renk: kareler birbirinden ayirt ediliyor */
    FillDword ((PVOID)P, TIK_BYTES, (gTik & 1) ? 0x0000FFFF : 0x00FFFF00);
  }

  gTik++;

  /*
   * Olum sonrasi kayit ASCII yaziliyor.
   *
   * NEDEN: onceki surumde sayaclar ikili yaziliyordu ve cihazdaki "dmem"
   * dokumunu bulanik bir fotograftan hex olarak cozmek zorunda kaliyorduk -
   * hangi baytin hangi alan oldugu tartisma konusu oldu. dmem'in sag
   * sutunundaki ASCII gorunumu bedava; ondan yararlanmamak sacma.
   *
   * +0x10'dan itibaren tam olarak sunu okuyacagiz:  TIK=0010 BUG=N
   */
  if (gLog != 0) {
    UINT8 *S = (UINT8 *)gLog + 16;
    UINT32 T = (UINT32)gTik;

    S[0] = 'T'; S[1] = 'I'; S[2] = 'K'; S[3] = '=';
    S[4] = (UINT8)('0' + ((T / 1000) % 10));
    S[5] = (UINT8)('0' + ((T /  100) % 10));
    S[6] = (UINT8)('0' + ((T /   10) % 10));
    S[7] = (UINT8)('0' + ( T         % 10));
    S[8] = ' ';
    S[9] = 'B'; S[10] = 'U'; S[11] = 'G'; S[12] = '=';
    /* Bugcheck geri cagrisi bunu 'E' yapiyor; dokunmuyorsak 'H' kaliyor */
    if (S[13] != 'E') {
      S[13] = 'H';
    }
    S[14] = ' '; S[15] = ' ';

    /*
     * +0x20:  SPN=xxxxxxxx
     *
     * Her tikte bosta dongusunun o ana kadarki tur sayisi. Sifirlamadan
     * sonra okundugunda:
     *
     *   sayi buyuk ve tikler boyunca ARTIYOR -> CPU hic bosa dusmedi,
     *       yani sifirlama bosta kalmayla (ve dolayisiyla PSCI
     *       CPU_SUSPEND ile) ilgili DEGIL
     *   sifir ya da sabit -> dongu zamanlanmamis, bu deney hicbir sey
     *       kanitlamiyor ve once onu duzeltmek gerekir
     */
    /*
     * FREN KALDIRILDI - SORUSUNU CEVAPLADI.
     *
     * Her tikte yarim saniye DISPATCH_LEVEL'da beklenmisti, yani CPU'nun
     * tam yarisi alinmisti. Sonuc: TIK 0010'dan 0018'e cikti. Sifirlama
     * SURE temelli degil IS temelli - Windows acilista belli bir noktaya
     * variyor ve orada oluyor. Sabit sure sayan bir seyin (watchdog,
     * zamanlayici) boyle davranmasi mumkun degil.
     *
     * Fren artik sadece olcumu yavaslatirdi, o yuzden cikti.
     */

    {
      UINT8 *P = (UINT8 *)gLog + 32;
      UINT32 V = (UINT32)gSpin;
      int    K;

      P[0] = 'S'; P[1] = 'P'; P[2] = 'N'; P[3] = '=';

      for (K = 7; K >= 0; K--) {
        UINT8 D = (UINT8)(V & 0xF);
        P[4 + K] = (UINT8)((D < 10) ? ('0' + D) : ('A' + D - 10));
        V >>= 4;
      }

      P[12] = ' '; P[13] = ' '; P[14] = ' '; P[15] = ' ';
    }

    /*
     * +0x60:  MAX=xxxxxxxx   bir tikte gorulen EN BUYUK tur sayisi
     *
     * NOT: bu blok bir kez yazilmis, sonra baska bir duzenleme sirasinda
     * indeksle kesip yapistirirken KAZARA SILINMISTI. gSpinAzami tanimli
     * kalmis ama hic kullanilmamis, dolayisiyla ekranda MAX satiri da hic
     * cikmamisti ve bunu fark etmedim. Silinen bir olcumun sessizligi,
     * olculup sifir cikmasindan ayirt edilemiyor.
     *
     * En buyuk delta, dongunun rakipsiz kaldigi bir 100 ms dilimidir; yani
     * serbest hiz. Ondan sonra:
     *     Windows'un CPU suresi = TIK/10 - SPN/MAX/10
     */
    {
      UINT64 Simdi = gSpin;
      UINT64 Fark  = Simdi - gSpinOnceki;

      gSpinOnceki = Simdi;

      if (Fark > 0xFFFFFFFFULL) {
        Fark = 0xFFFFFFFFULL;
      }

      if ((UINT32)Fark > gSpinAzami) {
        gSpinAzami = (UINT32)Fark;
      }

      {
        UINT8 *Q = (UINT8 *)gLog + 96;
        UINT32 V = gSpinAzami;
        int    K;

        Q[0] = 'M'; Q[1] = 'A'; Q[2] = 'X'; Q[3] = '=';
        for (K = 7; K >= 0; K--) {
          UINT8 D = (UINT8)(V & 0xF);
          Q[4 + K] = (UINT8)((D < 10) ? ('0' + D) : ('A' + D - 10));
          V >>= 4;
        }
        Q[12] = ' '; Q[13] = ' '; Q[14] = ' '; Q[15] = ' ';
      }
    }

    /*
     * +0x80: QPC duvar-zaman dogrulamasi.
     *
     * QPD = DriverEntry'den bu son DPC'ye sayac farki
     * F   = saniyedeki sayac sayisi
     *
     * Sonraki UEFI ekraninda oran QPD/F dogrudan saniyedir. Bu alan
     * sadece DPC tarafindan yazilir ve BRI (+0x70) ile cakisma yoktur.
     */
    if (gQpcFrekans > 0) {
      INT64 Simdi = KeQueryPerformanceCounter (0);
      volatile UINT8 *Q = (volatile UINT8 *)gLog + 128;

      Q[0] = 'Q'; Q[1] = 'P'; Q[2] = 'D'; Q[3] = '=';
      B2LYazHex8 (Q + 4,  (UINT32)(Simdi - gQpcBaslangic));
      Q[12] = ' '; Q[13] = 'F'; Q[14] = '=';
      B2LYazHex8 (Q + 15, (UINT32)gQpcFrekans);
      Q[23] = ' '; Q[24] = ' '; Q[25] = ' '; Q[26] = ' ';
      Q[27] = ' '; Q[28] = ' '; Q[29] = ' '; Q[30] = ' ';
      Q[31] = ' ';
    }

    /*
     * GIC OKUMASI KALDIRILDI - ARACIMIZI DUSURDU.
     *
     * Windows'un icinden dagiticiyi okumak KMODE_EXCEPTION_NOT_HANDLED
     * (0x1E) uretti. Sebebi mimari: ARM'de ayni fiziksel adresi FARKLI
     * bellek nitelikleriyle iki kez haritalamak tanimsiz davranis. HAL
     * GIC'i zaten Device olarak haritalamis durumda; MmMapIoSpace ile
     * ikinci bir haritalama acmak tam olarak bu yasaga giriyor.
     *
     * Bu, olcum aracini ikinci kez dusurusum oldu - ilki
     * PsSetLoadImageNotifyRoutine idi. Ikisi de ZAYIF bir hipotez icindi.
     * Firtina hipotezi zaten sallantida: kapatilmis kesme CPU'ya
     * iletilmiyor ve butun SPI'lari susturdugumuz denemede TIK hic
     * degismedi (0097). Bunun icin b2lfb'yi riske atmaya degmez.
     *
     * Aracin saglamligi, ondan alinacak tek bir olcumden daha degerli.
     */
  }
}

/*
 * Bugcheck oldu. Tek isi haber vermek.
 */
static void BugCheckCallback (PVOID Buffer, UINT32 Length)
{
  (void)Buffer;
  (void)Length;

  /*
   * Yesil. Macenta 0x00FF00FF idi (B=FF, G=00, R=FF) ve ekranda macenta
   * cikti - yani bicim BGRA. 0x0000FF00 her iki yorumda da yesil veriyor,
   * dolayisiyla piksel bicimini yeniden tartismaya gerek yok.
   */
  if (gFb != 0) {
    /*
     * YALNIZCA UST BANT YESILE BOYANIYOR - TAMAMI DEGIL.
     *
     * Onceki surum butun ekrani yesile boyuyor, sonra TIK sayisi kadar
     * beyaz serit ciziyordu. Ama kalp atisinin kendi mavi/sari seritleri
     * zaten sayilabilir bir ilerleme cubugu; ustune yesil basmak once
     * bilgiyi siliyor, sonra geri cizmeye calisiyordu.
     *
     * Simdi bugcheck yalnizca ust bandi yesile ceviriyor. Kalp atisi
     * seritleri OLDUGU GIBI kaliyor. Tek bakista iki bilgi:
     *   ust bant yesil  -> bugcheck oldu
     *   serit sayisi    -> cekirdek kac saniye yasadi
     */
    FillDword (gFb, BANT_BYTES, 0x0000FF00);

    /*
     * TIK'I EKRANA CIZ - SANIYE BASINA BIR KARE.
     *
     * NEDEN: AutoReboot kapali oldugu icin bugcheck ekranda kaliyor ve
     * cihaz yeniden baslamiyor. Elle kapatinca DRAM gidiyor, yani bir
     * sonraki acilista B2LGoster'in okuyacagi kayit da gidiyor. Stop
     * code'u goruyoruz ama TIK'i goremiyoruz.
     *
     * AutoReboot'u acmak ikilemi tersine cevirirdi: bu sefer stop code
     * ekranda kalmazdi. Ikisini birden istiyoruz.
     *
     * Cozum ekrana yazmak. Yazi tipi yok, ama saymaya gerek de yok:
     * her saniye icin bir kare, yesil bandin hemen altinda. Fotograftan
     * sayilabiliyor ve 1 saniye hassasiyetinde.
     *
     * Kalp atisi seritleri ilk 3 saniyeden sonra duruyor (BOYA_AZAMI),
     * o yuzden bu satir onlarla cakismiyor.
     */
    {
      UINT64 Saniye = gTik / 10;
      UINT64 K;

      /* Serit alaninin basi temizlensin ki eski boya sayimi bozmasin */
      FillDword ((PVOID)((UINT8 *)gFb + BANT_BYTES), BANT_BYTES, 0x00101010);

      for (K = 0; K < Saniye && K < 60; K++) {
        /*
         * Her kare 1/64 bant: 60 tanesi yan yana sigiyor. Onarli
         * gruplari ayirt etmek icin her onuncu kare farkli renk -
         * elli kareyi tek tek saymak yerine "bes grup arti uc" diye
         * okunuyor.
         */
        FillDword ((PVOID)((UINT8 *)gFb + BANT_BYTES + K * (BANT_BYTES / 64)),
                   BANT_BYTES / 128,
                   ((K + 1) % 10 == 0) ? 0x00FF00FF : 0x00FFFFFF);
      }
    }
  }

  /*
   * Olum sonrasi kayit: sicak sifirlamayi asan kanit.
   * +0x0C'ye "BUGC", ve ASCII satirindaki BUG= alanini 'E'ye cekiyoruz.
   */
  if (gLog != 0) {
    UINT8 *B = (UINT8 *)gLog;
    B[12] = 'B'; B[13] = 'U'; B[14] = 'G'; B[15] = 'C';
    B[16 + 13] = 'E';
  }
}

NTSTATUS DriverEntry (PVOID DriverObject, PVOID RegistryPath)
{
  PHYSICAL_ADDRESS Pa;
  PVOID            Fb;
  PVOID            Log;

  (void)DriverObject;
  (void)RegistryPath;


  /*
   * 1) OLUM SONRASI KAYIT ONCE.
   *
   * Sirasi onemli: framebuffer haritalama herhangi bir sebeple patlarsa
   * (bolge Windows tarafindan baska turlu haritalanmis olabilir) en azindan
   * "surucu calisti" kaydi RAM'de kalir ve sonraki acilista okunur.
   */
  Pa.QuadPart = (long long)LOG_PHYS;
  Log = MmMapIoSpace (Pa, LOG_BYTES, MmNonCached);
  if (Log != 0) {
    UINT8  *B = (UINT8 *)Log;
    UINT32 *C = (UINT32 *)(B + 4);

    /*
     * SAYAC IMZADAN ONCE OKUNUYOR. Imzayi once yazip sonra "imza var mi"
     * diye bakmak anlamsiz olurdu - her zaman var cikar. Onceki acilistan
     * kalan imza gecerliyse sayac devam ediyor, degilse sifirdan basliyor.
     */
    UINT32 Onceki = 0;
    if ((B[0] == 'B') && (B[1] == '2') && (B[2] == 'L') && (B[3] == 'F')) {
      Onceki = *C;
    }

    /* "B2LF" imzasi - bu dort bayt yoksa surucu hic calismamis demektir */
    B[0] = 'B'; B[1] = '2'; B[2] = 'L'; B[3] = 'F';

    /*
     * +0x04: acilis sayaci. DRAM sicak sifirlamada kaldigi icin her
     * calismada bir artiyor - "surucu tekrar tekrar yukleniyor mu, yoksa
     * bir kere mi" sorusunu cevapliyor.
     */
    *C = Onceki + 1;

    /* +0x08: asama isareti. Framebuffer denenmeden once "1". */
    B[8] = 1;
  }

  /*
   * 2) FRAMEBUFFER - canli kanal.
  */
  gLog = Log;
  gQpcBaslangic = KeQueryPerformanceCounter (&gQpcFrekans);
  B2LAutoStartRecord ();


  Pa.QuadPart = (long long)FB_PHYS;
  Fb = MmMapIoSpace (Pa, FB_BYTES, MmNonCached);
  gFb = Fb;
  if (Fb != 0) {
    /*
     * Parlak macenta. Bilerek UEFI'nin ya da Windows'un kullandigi hicbir
     * renge benzemiyor - ekranda gorursek kaynagi tartisilmaz.
     */
    /* Yalnizca ince serit - gerisi Windows'a kaliyor ki animasyonu gorelim */
    FillDword (Fb, BANT_BYTES, 0x00FF00FF);

    if (Log != 0) {
      ((UINT8 *)Log)[8] = 2;   /* asama: framebuffer'a yazildi */
    }
  }

  /*
   * 3) BUGCHECK GERI CAGRISINI KAYDET.
   *
   * En sona birakildi: kayit basarisiz olsa bile yukaridaki iki kanal
   * calismis olur. Bu turun asil sorusu geri cagrida degil, ama onun
   * ugruna calisan bir seyi riske atmiyoruz.
   */
  /*
   * KAYIT SONUCU EKRANA YANSITILIYOR - BU ADIM ATLANAMAZ.
   *
   * Ilk denemede ekran macenta kaldi, yesil hic gelmedi. Bunun iki okumasi
   * var ve ayirmadan devam etmek hatali olur:
   *
   *   (a) Gercekten bugcheck degil
   *   (b) Kayit hic tutmadi, dolayisiyla sessizligin bir anlami yok
   *
   * Bu proje ayni dersi defalarca aldi: bir aracin SESSIZLIGINE guvenmeden
   * once aracin CALISTIGINI dogrulamak gerekiyor. B2LEl2Dxe "cikti
   * uretmedi" diye zararsiz sanildi ve turlarca olcum kirletti.
   *
   * KeRegisterBugCheckCallback basarili olursa TRUE donuyor. Donuse gore
   * bolgenin ilk sekizde birini BEYAZA boyuyoruz:
   *
   *   Macenta + ustte BEYAZ SERIT -> kayit TUTTU. Yesil gelmiyorsa cokme
   *                                  gercekten bugcheck degil.
   *   Serit YOK                   -> kayit tutmadi; onceki turun sessizligi
   *                                  hicbir sey kanitlamiyor.
   */
  {
    UINT8 Ok = KeRegisterBugCheckCallback (
                 (PVOID)gCallbackRecord,
                 (PVOID)BugCheckCallback,
                 gLog,
                 16,
                 gComponent);

    if (gLog != 0) {
      ((UINT8 *)gLog)[9]  = Ok;   /* +0x09: kayit donusu */
      ((UINT8 *)gLog)[8]  = 3;    /* asama: kayit denendi */
    }

    /* Kayit tuttuysa seridin ilk ceyregi beyaz - kanit yerinde kalsin */
    if (Ok && (gFb != 0)) {
      FillDword (gFb, BANT_BYTES / 4, 0x00FFFFFF);
    }
  }

  /*
   * 4) KALP ATISI - saniyede on kare.
   *
   * DueTime negatif = GORELI, 100 ns birimi. -1000000 = 100 ms.
   */
  /*
   * Bosta engelleyici. En sonda ve donusu YOK SAYILIYOR: basarisiz olsa
   * bile digher kanallar calismaya devam etsin.
   */
  {
    PVOID Th = 0;
    PsCreateSystemThread (&Th, 0x1F03FF, 0, 0, 0, (PVOID)BostaDongu, 0);
  }

  KeInitializeDpc ((PVOID)gDpc, (PVOID)HeartbeatDpc, 0);
  KeInitializeTimer ((PVOID)gTimer);
  /*
   * COZUNURLUK 1 SANIYEDEN 100 MS'YE INDIRILDI.
   *
   * NEDEN: tek bir Start=1 servisini kapatmak CNT'yi 16'dan 3'e dusurdu ama
   * TIK 9-10'da kaldi. Tek bir servis yuklenen imaj sayisini besde bire
   * indiremez; makul tek aciklama imajlarin SONDA BIR SALKIM halinde
   * yuklendigi ve olumun o salkimin neresine denk geldigine gore CNT'nin
   * ziplamasi. Bir saniyelik cozunurlukle bu ayirt edilemiyor.
   *
   * 100 ms ile TIK artik onda bir saniye: TIK=0100 = 10 saniye.
   */
  KeSetTimerEx ((PVOID)gTimer, -1000000LL, 100, (PVOID)gDpc);

  /*
   * PnP/aygit baslatma cizgisini yakala. Kayit hemen once yazilir ki
   * callback hic gelmezse "kayit denenmedi" ile ayirt edilebilsin.
   * Bu rutin yalnizca DriverEntry basariyla donecekse cagrilabilir; asagida
   * STATUS_SUCCESS donuyoruz.
   */
  B2LBootReinitRecord (0, 0, 0);
  IoRegisterBootDriverReinitialization (
    DriverObject,
    B2LBootReinitialize,
    0);

  /*
   * BLOKAJ KALDIRILDI - MERDIVEN GOREVINI TAMAMLADI.
   *
   *   Start=0, en erken grup      -> 25+ serit, sifirlama YOK
   *   Start=0, en son (gruppsuz)  -> 20+ serit, sifirlama YOK
   *   Start=1                     -> bant HIC CIKMADI, 10 saniyede sifirlama
   *
   * Ucuncu satir tetikleyiciyi iki nokta arasina sikistirdi: acilis
   * surucileri bittikten sonra, Start=1 surucileri yuklenmeden once. Orada
   * olan tek sey PnP yoneticisinin ACPI ad alanini sayip aygitlari
   * baslatmasi.
   *
   * Simdi acilisin ilerlemesi gerekiyor, o yuzden blokaj yok ve surucu
   * tekrar en erken acilis surucusu.
   */

  /*
   * SURELI BLOKAJ KALDIRILDI - SAYIYI VERDI.
   *
   *     REL=0053   TIK=0063   ->  birakildiktan sonra TAM 10 SANIYE
   *
   * Yani tetikleyiciye ulasmak icin acilis surucileri bittikten sonra on
   * saniyelik GERCEK IS gerekiyor. Gecikmesiz durumdaki 10 ile birebir
   * tutarli.
   *
   * Artik gecikmeye gerek yok; olcum normal acilista da ayni sayiyi veriyor.
   */

  /*
   * STATUS_SUCCESS SART. Bir acilis surucusu basarisiz donerse ve
   * ErrorControl uygunsa winload butun acilisi iptal eder. Bu surucu
   * hicbir seyi sahiplenmiyor; her kosulda basarili donuyor.
   */
  return STATUS_SUCCESS;
}
