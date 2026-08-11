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
__declspec(dllimport) void KeInitializeDpc (PVOID Dpc, PVOID Routine, PVOID Context);
__declspec(dllimport) void KeInitializeTimer (PVOID Timer);
__declspec(dllimport) UINT8 KeSetTimerEx (PVOID Timer, long long DueTime, int Period, PVOID Dpc);

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

/* Cekirdek bu alanin icine yaziyor; sifir olmasi ve sayfalanamaz olmasi yeter */
static UINT64 gCallbackRecord[16];

static UINT8 gComponent[8] = { 'b','2','l','f','b',0,0,0 };

/* Cekirdek bunlarin icine yaziyor - 128 bayt, 16 bayt hizali */
__declspec(align(16)) static UINT64 gTimer[16];
__declspec(align(16)) static UINT64 gDpc[16];

static UINT64 gTik = 0;

/*
 * Saniyede bir. Her tik bir sonraki parcayi boyuyor ve rengi degistiriyor,
 * boylece tikler tek tek sayilabiliyor.
 */
static void HeartbeatDpc (PVOID Dpc, PVOID Ctx, PVOID A1, PVOID A2)
{
  UINT8 *P;

  (void)Dpc; (void)Ctx; (void)A1; (void)A2;

  if ((gFb == 0) || (gTik >= TIK_AZAMI)) {
    return;
  }

  P = (UINT8 *)gFb + BANT_BYTES + (gTik * TIK_BYTES);

  /* Tek/cift tikler farkli renk: kareler birbirinden ayirt ediliyor */
  FillDword ((PVOID)P, TIK_BYTES, (gTik & 1) ? 0x0000FFFF : 0x00FFFF00);

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
   * 4) KALP ATISI - saniyede bir kare.
   *
   * DueTime negatif = GORELI, 100 ns birimi. -10000000 = 1 saniye.
   * Period = 1000 ms, yani saniyede bir tekrar.
   */
  KeInitializeDpc ((PVOID)gDpc, (PVOID)HeartbeatDpc, 0);
  KeInitializeTimer ((PVOID)gTimer);
  KeSetTimerEx ((PVOID)gTimer, -10000000LL, 1000, (PVOID)gDpc);

  /*
   * DONMEYEN DONGU - IKINCI TUR, BU SEFER EN SONDAN
   *
   * Ilk turda surucu EN ERKEN gruptaydi ("System Bus Extender") ve burada
   * durunca sifirlama HIC olmadi. Yani tetikleyici bizden SONRA.
   *
   * Simdi servis kaydindaki Group bos birakildi: surucu butun gruplu acilis
   * surucilerinden SONRA yukleniyor. Ayni yerde tekrar duruyoruz ama bu kez
   * cok daha ilerideyiz.
   *
   *   Macenta bant cikip sifirlama OLMUYORSA
   *       -> Butun acilis surucileri sorunsuz yuklendi. Tetikleyici daha
   *          ileride: PnP aygit sayimi ya da cekirdegin Faz 1 devami.
   *
   *   Macenta bant HIC CIKMIYOR ve ~10 saniyede sifirliyorsa
   *       -> Sifirlama acilis surucileri yuklenirken, BIZDEN ONCE oluyor.
   *          O zaman suclu o listede ve tek tek kapatarak daraltilir.
   *
   * Bant cikip yine de sifirliyorsa tetikleyici tam bizim yuklenme
   * anımızda demektir - o da dar bir pencere.
   */
  {
    volatile UINT64 I = 0;
    for (;;) {
      I++;
    }
  }

  /*
   * STATUS_SUCCESS SART. Bir acilis surucusu basarisiz donerse ve
   * ErrorControl uygunsa winload butun acilisi iptal eder. Bu surucu
   * hicbir seyi sahiplenmiyor; her kosulda basarili donuyor.
   */
  return STATUS_SUCCESS;
}
