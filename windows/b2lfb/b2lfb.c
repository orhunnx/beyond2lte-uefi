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
  Pa.QuadPart = (long long)FB_PHYS;
  Fb = MmMapIoSpace (Pa, FB_BYTES, MmNonCached);
  if (Fb != 0) {
    /*
     * Parlak macenta. Bilerek UEFI'nin ya da Windows'un kullandigi hicbir
     * renge benzemiyor - ekranda gorursek kaynagi tartisilmaz.
     */
    FillDword (Fb, FB_BYTES, 0x00FF00FF);

    if (Log != 0) {
      ((UINT8 *)Log)[8] = 2;   /* asama: framebuffer'a yazildi */
    }
  }

  /*
   * STATUS_SUCCESS SART. Bir acilis surucusu basarisiz donerse ve
   * ErrorControl uygunsa winload butun acilisi iptal eder. Bu surucu
   * hicbir seyi sahiplenmiyor; her kosulda basarili donuyor.
   */
  return STATUS_SUCCESS;
}
