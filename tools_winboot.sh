#!/usr/bin/env bash
#
# Windows Boot Manager'i ISO'dan cikarip ESP'ye hazirlar.
#
#   bash tools_winboot.sh [/yol/Windows_..._ARM64.iso]
#
# AMAC: Windows'un ilk denemesi, HICBIR SEY SILMEDEN.
#
# Bu adimda Windows kurmuyoruz. Sadece Boot Manager'i calistirip ne oldugunu
# goruyoruz. Isletim sistemi bulamayip hata ekrani verecek - ama o ekrani
# CIZEBILMESI bile cok sey kanitliyor:
#   - GOP'umuz Windows'a yetiyor (kendi grafik cikisini kuruyor)
#   - ACPI tablolarimiz kabul edildi (bootmgr ACPI'yi okuyor)
#   - Bellek haritamiz gecerli (aksi halde daha once cokerdi)
#   - EFI cagri arayuzumuz Windows'un bekledigi gibi
#
# Beklenen hata: 0xc0000225 "boot device bulunamadi" ya da benzeri.
# BU BASARIDIR. Hicbir sey acilmazsa ya da cihaz yeniden baslarsa,
# sorun daha temelde demektir.
#
# NOT: boot.wim (606 MB) ESP'ye SIGMIYOR (CACHE 600 MB). Yani WinPE'yi
# RAM diskten acmak bu bolumle mumkun degil. Sonraki adimda ya daha buyuk
# bir bolum (SYSTEM 5.57 GB) kullanilacak ya da imaj dogrudan USERDATA'ya
# uygulanacak.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESP="$HERE/esp"

#
# BAYRAKLARI ONCE AYIKLA. Onceden bayrak ayristirmasi ISO atamasindan SONRA
# geliyordu; "bash tools_winboot.sh --wim-yenile" calistirinca bayrak ISO
# yolu saniliyor ve "ISO bulunamadi" hatasi veriyordu.
#
FORCE_WIM=0
STOK_STARTNET=0
DISK_MODU=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --wim-yenile) FORCE_WIM=1 ;;
    --stok)       STOK_STARTNET=1 ;;
    --disk)       DISK_MODU=1 ;;
    *) ARGS+=("$a") ;;
  esac
done
if [ ${#ARGS[@]} -gt 0 ]; then set -- "${ARGS[@]}"; else set --; fi

ISO="${1:-}"
if [ -z "$ISO" ]; then
  for c in "$HOME"/*.iso /mnt/c/Users/*/Downloads/*Arm64*.iso /mnt/c/Users/*/Downloads/*.iso; do
    [ -f "$c" ] && { ISO="$c"; break; }
  done
fi
[ -f "$ISO" ] || { echo "ISO bulunamadi. Yolu argüman olarak ver."; exit 1; }

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
for tool in 7z readlink sha256sum stat; do
  command -v "$tool" >/dev/null || { echo "$tool yok; gerekli WSL araclarini kur."; exit 1; }
done

# Hazir boot.wim yalniz ayni ISO'dan geldiyse yeniden kullanilabilir. Aksi
# halde farkli bir ISO verildiginde onceki WIM sessizce paketlenir. Tam ISO
# hash'i cok pahali oldugu icin kanonik yol, boyut ve mtime birlikte kaynak
# kimligi olarak kullanilir.
ISO_REAL="$(readlink -f "$ISO")"
ISO_SOURCE="$(printf '%s\n%s\n%s\n' "$ISO_REAL" "$(stat -c%s "$ISO")" \
  "$(stat -c%Y "$ISO")" | sha256sum | awk '{print $1}')"
WIM_SOURCE_FILE="$HERE/out/boot-wim-source.txt"
CURRENT_WIM_SOURCE="$(sed -n 's/^SOURCE_ID=//p' "$WIM_SOURCE_FILE" 2>/dev/null || true)"

say "ISO: $ISO"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

say "ISO icindeki boot dosyalari araniyor"
#
# ILK DENEMEDE BURASI SESSIZCE BASARISIZ OLDU. Cikarma satirlari "|| true" ile
# korunuyordu, BCD kopyalanmadi ve cihaz "0xc000000f - File: \EFI\Microsoft\
# Boot\BCD" verdi. Simdi yollar ISO listesinden BULUNUYOR (buyuk/kucuk harf
# ISO'dan ISO'ya degisiyor) ve eksik dosya varsa script DURUYOR.
#
LIST="$W/list.txt"
7z l -slt "$ISO" > "$LIST"

find_in_iso() {   # $1 = aranan yol deseni (buyuk/kucuk harf duyarsiz)
  grep -i '^Path = ' "$LIST" | sed 's/^Path = //' | grep -i -m1 -E "$1" || true
}

BM_PATH="$(find_in_iso '(^|[/\\])efi[/\\]boot[/\\]bootaa64\.efi$')"
BCD_PATH="$(find_in_iso '(^|[/\\])efi[/\\]microsoft[/\\]boot[/\\]bcd$')"
echo "  Boot Manager : ${BM_PATH:-BULUNAMADI}"
echo "  BCD          : ${BCD_PATH:-BULUNAMADI}"

[ -n "$BM_PATH" ]  || { echo "HATA: bootaa64.efi ISO'da yok."; exit 1; }
[ -n "$BCD_PATH" ] || { echo "HATA: BCD ISO'da yok."; exit 1; }

say "cikariliyor"
7z e -o"$W" "$ISO" "$BM_PATH"  >/dev/null
7z e -o"$W" "$ISO" "$BCD_PATH" >/dev/null
BM="$W/$(basename "$BM_PATH")"
BCD="$W/$(basename "$BCD_PATH")"

# Gercekten geldiler mi? Sessiz basarisizlik bir tur kaybettirdi.
[ -s "$BM" ]  || { echo "HATA: bootaa64.efi cikarilamadi."; exit 1; }
[ -s "$BCD" ] || { echo "HATA: BCD cikarilamadi."; exit 1; }

# BCD bir registry hive'i; "regf" ile baslar. Yanlis dosyayi almadigimizi dogrula.
MAGIC="$(head -c4 "$BCD")"
[ "$MAGIC" = "regf" ] || { echo "HATA: BCD registry hive gibi gorunmuyor (basi: $MAGIC)"; exit 1; }
echo "  BCD dogrulandi: regf, $(stat -c%s "$BCD") bayt"

#
# boot.sdi - RAM DISKININ TANIMI. WinPE icin ZORUNLU.
#
# Bir tur bunsuz denendi: bootmgr "A required device isn't connected or can't
# be accessed / 0xc000000f" verdi. Sikayeti kelimesi kelimesine dogruymus -
# gereken aygit gercekten yoktu, cunku tanimi eksikti.
#
# BCD'deki {ramdiskoptions} girdisi "ramdisksdipath = \boot\boot.sdi" diyor.
# boot.wim icerigi tasiyor, boot.sdi ise diskin kendisini tarif ediyor;
# ikisi de ayni birimde olmak zorunda.
#
SDI_PATH="$(find_in_iso '(^|[/\\])boot[/\\]boot\.sdi$')"
echo "  boot.sdi     : ${SDI_PATH:-BULUNAMADI}"
[ -n "$SDI_PATH" ] || { echo "HATA: boot.sdi ISO'da yok."; exit 1; }
7z e -o"$W" "$ISO" "$SDI_PATH" >/dev/null
SDI="$W/$(basename "$SDI_PATH")"
[ -s "$SDI" ] || { echo "HATA: boot.sdi cikarilamadi."; exit 1; }
echo "  boot.sdi dogrulandi: $(stat -c%s "$SDI") bayt"

#
# Yazi tipleri - SADECE LATIN OLANLAR.
#
# ISO'da 20 font var ve toplami 83 MB; buyuk kismi CJK (chs/cht/jpn/kor,
# tek basina 11 MB'lik cht_boot.ttf gibi). Ingilizce hata ekrani icin
# gereksizler ve boot.wim'e yer birakmiyorlar - bir tur "Disk full" ile
# gecti. Asagidaki dordu ~300 KB tutuyor.
#
WANT_FONTS='wgl4_boot|segmono_boot|segoe_slboot|segoen_slboot'
FONTS="$(grep -i '^Path = ' "$LIST" | sed 's/^Path = //' \
         | grep -i -E 'efi[/\\]microsoft[/\\]boot[/\\]fonts[/\\].*\.ttf$' \
         | grep -i -E "$WANT_FONTS" || true)"
if [ -n "$FONTS" ]; then
  mkdir -p "$W/fonts"
  echo "$FONTS" | while read -r f; do 7z e -o"$W/fonts" "$ISO" "$f" >/dev/null 2>&1 || true; done
  echo "  yazi tipi: $(ls "$W/fonts" 2>/dev/null | wc -l) dosya, $(du -sh "$W/fonts" | cut -f1)"
fi

#
# boot.wim - WinPE'yi RAM diskten acmak icin.
#
# ISO'daki boot.wim 606 MB ve iki indeks iceriyor:
#   1 = Microsoft Windows PE      <- bize gereken
#   2 = Microsoft Windows Setup   <- kurulum sihirbazi, bizde klavye yok, gereksiz
#
# Sadece 1. indeksi disari aktarinca ciddi kuculuyor. LZX kullaniyoruz;
# bootmgr'in ramdisk yukleyicisi XPRESS ve LZX okuyabiliyor ama LZMS/solid
# OKUYAMIYOR - o yuzden "--solid" YOK, daha kucuk olurdu ama acilmazdi.
#
#
# WIM ZATEN HAZIRSA ATLA.
#
# 570 MB'lik WIM'i her BCD denemesinde yeniden isliyorduk; cikarma + delete +
# optimize + hive duzenleme dakikalar suruyor ve BCD degisikliginin WIM ile
# hicbir ilgisi yok. Hazirsa dokunmuyoruz.
#   Zorla yeniden uretmek icin:  bash tools_winboot.sh --wim-yenile
#
WIM_PATH="$(find_in_iso '(^|[/\\])sources[/\\]boot\.wim$')"
if [ "$FORCE_WIM" = 0 ] && [ "$CURRENT_WIM_SOURCE" = "$ISO_SOURCE" ] \
   && [ -s "$ESP/sources/boot.wim" ] \
   && wimlib-imagex info "$ESP/sources/boot.wim" 2>/dev/null | grep -qi '^Boot Index: *1'; then
  say "boot.wim hazir, atlaniyor ($(du -h "$ESP/sources/boot.wim" | cut -f1), Boot Index 1)"
  echo "  yeniden uretmek icin: bash tools_winboot.sh --wim-yenile"
elif [ -n "$WIM_PATH" ] && command -v wimlib-imagex >/dev/null; then
  if [ -s "$ESP/sources/boot.wim" ] && [ "$CURRENT_WIM_SOURCE" != "$ISO_SOURCE" ]; then
    say "ISO degisti; boot.wim onbellegi yenileniyor"
  fi
  say "boot.wim isleniyor (sadece WinPE indeksi)"
  7z e -o"$W" "$ISO" "$WIM_PATH" >/dev/null
  SRCWIM="$W/$(basename "$WIM_PATH")"
  echo "  kaynak: $(du -h "$SRCWIM" | cut -f1)"
  wimlib-imagex info "$SRCWIM" | grep -E '^(Index|Name|Description)' | sed 's/^/    /' || true

  echo "  kaynagin sikistirmasi:"
  wimlib-imagex info "$SRCWIM" 1 2>/dev/null | grep -E 'Compression|Chunk Size' | sed 's/^/    /' || true

  mkdir -p "$ESP/sources"
  rm -f "$ESP/sources/boot.wim"

  #
  # YENIDEN SIKISTIRMIYORUZ.
  #
  # Ilk denemede "export --compress=LZX" ile 570 MB'a indirdik ve cihaz
  # "winload.efi ... 0xc0000487" verdi. bootmgr'in kendi WIM okuyucusu
  # bizim urettigimiz bicimi yanlis cozuyorsa winload tam olarak boyle
  # "bozuk" gorunur. O yuzden simdi ORIJINAL sikistirilmis veriyi
  # oldugu gibi koruyoruz:
  #
  #   delete 2   -> Setup indeksini kaldir (bize gereksiz)
  #   optimize   -> dosyayi yeniden paketle, sahipsiz veriyi at
  #                 (--recompress YOK, yani hicbir sey yeniden sikistirilmiyor)
  #
  cp "$SRCWIM" "$ESP/sources/boot.wim"
  if wimlib-imagex info "$ESP/sources/boot.wim" 2>/dev/null | grep -q '^Image Count: *2'; then
    wimlib-imagex delete "$ESP/sources/boot.wim" 2
  fi
  wimlib-imagex optimize "$ESP/sources/boot.wim"

  #
  # SIGMIYORSA LZX ILE YENIDEN SIKISTIR.
  #
  # 23H2'nin boot.wim'i 745 MB cikti; CACHE bolumu 600 MB. Delete+optimize
  # yetmiyor cunku Setup ile WinPE dosyalarin cogunu PAYLASIYOR, 2. indeksi
  # silmek az yer kazandiriyor.
  #
  # YENIDEN SIKISTIRMADAN NEDEN KACINMISTIM: bir tur "export --compress=LZX"
  # yaptiktan sonra cihaz "winload.efi ... 0xc0000487" verdi ve sucu
  # sikistirmaya attim. YANLISTI - certutil o kodun STATUS_WIM_NOT_BOOTABLE
  # oldugunu soyledi, yani Boot Index isaretiydi. LZX'in hicbir suclulugu
  # yoktu ve bootmgr'in WIM okuyucusu XPRESS ile LZX'i sorunsuz aciyor.
  # (Acamadigi sey LZMS ve solid arsivler; onlari kullanmiyoruz.)
  #
  # "export 1" sadece 1. indeksin GERCEKTEN kullandigi dosyalari yaziyor,
  # yani silinen Setup indeksinden artakalan paylasilmayan veri de gidiyor.
  #
  # rm -f SART: wimlib export hedefe EKLER, uzerine yazmaz. Bir tur bu
  # yuzden iki indeksli bir dosya olustu.
  #
  SZM=$(( $(stat -c%s "$ESP/sources/boot.wim") / 1024 / 1024 ))
  if [ "$SZM" -gt 500 ]; then
    say "boot.wim ${SZM} MB, bolume sigmaz -> LZX ile yeniden sikistiriliyor"
    echo "  (bu birkac dakika surer)"
    rm -f "$W/small.wim"
    wimlib-imagex export "$ESP/sources/boot.wim" 1 "$W/small.wim" --compress=LZX
    rm -f "$ESP/sources/boot.wim"
    mv "$W/small.wim" "$ESP/sources/boot.wim"
    echo "  ${SZM} MB -> $(( $(stat -c%s "$ESP/sources/boot.wim") / 1024 / 1024 )) MB"
  fi

  #
  # ACILABILIR OLARAK ISARETLE. IKI TUR BUNUN YUZUNDEN GITTI.
  #
  # WIM basliginda "Boot Index" alani var; hangi imajin acilabilir oldugunu
  # o soyluyor. Windows ISO'sunda bu deger 2, yani SETUP imajini gosteriyor -
  # biz de tam olarak 2'yi sildik, geriye isaretli imaj kalmadi.
  #
  # Cihaz bunu birebir soyledi ama ben "bozuk dosya" diye okudum:
  #   winload.efi ... 0xc0000487 = STATUS_WIM_NOT_BOOTABLE
  #   "Belirtilen Windows Goruntusu (WIM) onyuklenebilir olarak isaretli degil."
  #
  # (certutil -error 0xc0000487 ile bulundu. Kod tahmin etmeye calismak yerine
  #  Windows'a sormak bir turda cozdu.)
  #
  wimlib-imagex info "$ESP/sources/boot.wim" 1 --boot >/dev/null
  BOOTIDX="$(wimlib-imagex info "$ESP/sources/boot.wim" 2>/dev/null | grep -i '^Boot Index' | tr -s ' ')"
  echo "  sonuc: $(du -h "$ESP/sources/boot.wim" | cut -f1)  (yeniden sikistirilmadi)"
  echo "  ${BOOTIDX:-Boot Index: OKUNAMADI}   <- 1 olmali"
  case "$BOOTIDX" in
    *1) : ;;
    *) echo "  UYARI: Boot Index 1 degil, WinPE yine acilmaz." ;;
  esac


  SZB=$(stat -c%s "$ESP/sources/boot.wim")
  LIMIT=$((600*1024*1024 - 12*1024*1024))
  echo "  boyut: $((SZB/1024/1024)) MB / sinir $((LIMIT/1024/1024)) MB"
  if [ "$SZB" -gt "$LIMIT" ]; then
    echo
    echo "  SIGMIYOR. Yeniden sikistirma kacinilmaz olabilir:"
    echo "    wimlib-imagex export <kaynak> 1 <hedef> --compress=LZX --rebuild"
    echo "  ya da daha buyuk bir bolum (SYSTEM 5.57 GB)."
  fi

  mkdir -p "$(dirname "$WIM_SOURCE_FILE")"
  {
    echo "SOURCE_ID=$ISO_SOURCE"
    echo "ISO=$ISO_REAL"
    echo "ISO_SIZE=$(stat -c%s "$ISO")"
    echo "ISO_MTIME=$(stat -c%Y "$ISO")"
    echo "WIM_PATH=$WIM_PATH"
  } >"$WIM_SOURCE_FILE"
else
  echo "  (boot.wim atlandi: ISO'da yok ya da wimlib-imagex kurulu degil)"
fi

#
# ---------------------------------------------------------------------------
# WIM ICERIK YAMALARI - WIM YENIDEN URETILSIN YA DA URETILMESIN CALISIR
# ---------------------------------------------------------------------------
#
# BU BLOK ONCEDEN 'WIM'i isle' DALININ ICINDEYDI VE GERCEK BIR HATAYDI.
#
# '--wim-yenile' verilmediginde script 'boot.wim hazir, atlaniyor' deyip o
# dalin TAMAMINI geciyordu. Yani:
#   - AutoReboot=0 hive yamasi hic uygulanmiyordu
#   - '--stok' bayragi hicbir is yapmiyordu (stock startnet.cmd yazilmiyordu)
#   - dogrulama satirlari hic basilmiyordu, ekranda 'hicbir sey yazmiyor'
#
# WIM'i YENIDEN URETMEK ile ICERIGINE DOKUNMAK bagimsiz islerdir; ikisini
# ayni kosula baglamak birkac turluk olcumu supheli hale getirdi.
#
# Artik boot.wim varsa her calistirmada uygulaniyor.
#
if [ -s "$ESP/sources/boot.wim" ] && command -v wimlib-imagex >/dev/null; then
  #
  # MAVI EKRANI EKRANDA TUT.
  #
  # Cihaz cekirdek cokunce ANINDA sifirliyor, kod gorunmuyor. Sebep WinPE'nin
  # registry'sindeki CrashControl\AutoReboot = 1. Bu ayar WIM'in ICINDEKI
  # SYSTEM hive'inda; disari cikarip degistirip geri koyabiliyoruz.
  #
  #   AutoReboot        = 0  -> cokunce sifirlama, ekranda kal
  #   DisplayParameters = 1  -> stop kodunun parametrelerini de goster
  #
  # Bunlar olmadan her tur "yeniden basladi" demekten oteye gidemiyoruz.
  #
  #
  # IKI ARAC, IKI PAKET - BU BIR TUR KAYBETTIRDI.
  #
  # "sudo apt install libhivex-bin" yaptik ama script yine "hivexregedit yok"
  # dedi. Cunku hivexregedit o pakette DEGIL: bir Perl scripti ve
  # "libwin-hivex-perl" paketiyle geliyor. libhivex-bin sadece hivexsh,
  # hivexget ve hivexml veriyor.
  #
  # Artik ikisiyle de calisiyor:
  #   hivexregedit varsa  -> .reg birlestirme (okunakli, tercih edilen)
  #   yoksa hivexsh       -> "setval" ile ayni degerleri elle yaziyoruz
  #
  # Dogrulama hivexget ile; o da libhivex-bin'de, yani her durumda var.
  #
  HIVEARAC=""
  command -v hivexregedit >/dev/null && HIVEARAC="regedit"
  [ -z "$HIVEARAC" ] && command -v hivexsh >/dev/null && HIVEARAC="sh"

  if [ -n "$HIVEARAC" ]; then
    say "WinPE registry: AutoReboot kapatiliyor  (arac: hivex$HIVEARAC)"
    HV="$W/hive"; mkdir -p "$HV"
    if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
         /Windows/System32/config/SYSTEM --dest-dir="$HV" --no-acls >/dev/null 2>&1; then

      YAZILDI=0
      if [ "$HIVEARAC" = "regedit" ]; then
        cat > "$W/crash.reg" <<'REG'
[HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\CrashControl]
"AutoReboot"=dword:00000000
"DisplayParameters"=dword:00000001
REG
        hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' \
          "$HV/SYSTEM" "$W/crash.reg" 2>/dev/null && YAZILDI=1
      else
        #
        # hivexsh: "setval N" komutundan sonra N tane (isim, deger) CIFTI
        # satir satir okunuyor. Yol ayiraci ters egik cizgi.
        #
        hivexsh -w "$HV/SYSTEM" <<'HSH' >/dev/null 2>&1 && YAZILDI=1
cd \ControlSet001\Control\CrashControl
setval 2
AutoReboot
dword:0
DisplayParameters
dword:1
commit
HSH
      fi

      if [ "$YAZILDI" = 1 ]; then
        wimlib-imagex update "$ESP/sources/boot.wim" 1 \
          --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
        wimlib-imagex optimize "$ESP/sources/boot.wim" >/dev/null
        echo "  yazildi, WIM guncellendi ($(du -h "$ESP/sources/boot.wim" | cut -f1))"

        #
        # GERI OKUYUP DOGRULA. "Mavi ekran hic gormuyoruz" gozleminin tamami
        # bu ayarin gercekten uygulanmis olmasina dayaniyor; uygulanmadiysa
        # bugcheck zaten cihazi yeniden baslatir ve gormememiz normaldir,
        # yani "cokus bugcheck degil" cikarimi temelsiz kalir.
        #
        DV="$W/dogrula"; mkdir -p "$DV"
        if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
             /Windows/System32/config/SYSTEM --dest-dir="$DV" --no-acls >/dev/null 2>&1; then
          AR="$(hivexget "$DV/SYSTEM" 'ControlSet001\Control\CrashControl' AutoReboot 2>/dev/null || true)"
          DP="$(hivexget "$DV/SYSTEM" 'ControlSet001\Control\CrashControl' DisplayParameters 2>/dev/null || true)"
          echo "  WIM'den geri okundu: AutoReboot=${AR:-<yok>}  DisplayParameters=${DP:-<yok>}"
          case "$AR" in
            0|0x0|0x00000000) echo "  DOGRULANDI: bugcheck olursa cihaz ekranda kalacak" ;;
            *) echo "  ! DOGRULANAMADI - bugcheck olsa bile cihaz yeniden baslar" ;;
          esac
        else
          echo "  ! UYARI: dogrulama icin hive geri okunamadi"
        fi
      else
        echo "  ! UYARI: hive duzenlenemedi, AutoReboot acik kaliyor"
      fi
    else
      echo "  ! UYARI: SYSTEM hive cikarilamadi"
    fi
  else
    echo
    echo "  ###############################################################"
    echo "  # HIC HIVEX ARACI YOK."
    echo "  #   sudo apt install -y libhivex-bin libwin-hivex-perl"
    echo "  #"
    echo "  # DIKKAT: hivexregedit libhivex-bin'de DEGIL, libwin-hivex-perl"
    echo "  # paketindedir. Ikisini de kur."
    echo "  #"
    echo "  # Bu atlanacak bir uyari degil: onsuz AutoReboot=0 yazilamiyor,"
    echo "  # bugcheck olsa bile cihaz aninda yeniden baslar ve mavi ekrani"
    echo "  # ASLA goremeyiz. 'Cokus bugcheck degil' cikarimi buna dayaniyor."
    echo "  ###############################################################"
    echo
  fi

  #
  # KENDI startnet.cmd'MIZ.
  #
  # WinPE acilista \Windows\System32\startnet.cmd calistirir. Varsayilani
  # wpeinit calistirip cmd.exe aciyor; cmd.exe cikinca WinPE MAKINEYI YENIDEN
  # BASLATIYOR. Cihazda gordugumuz "donen daire -> ekran sonme animasyonu ->
  # reset" tam olarak buydu: cokme degil, duzgun kapanma.
  #
  # Bizimki asla cikmiyor ve icerideki durumu hem ekrana hem ESP'ye yaziyor.
  #
  #
  # --stok : BIZIM startnet.cmd'MIZI HIC KOYMA, STOCK ICERIGI GERI YAZ.
  #
  # Stock WinPE'nin startnet.cmd'i tek satirdir: "wpeinit". Cihazda donen
  # dairenin EKRAN SONME ANIMASYONUYLA kayboldugu tek an, tam olarak o
  # haldeydik - henuz hicbir sey enjekte etmemistik. Animasyon cokme degil,
  # kabugun DUZGUN CIKISI: wpeinit biter, winpeshl cmd.exe'yi baslatir,
  # konsol girdisi olmadigi icin cmd hemen cikar ve WinPE tasarim geregi
  # yeniden baslatir. Yani o an her sey calisiyordu.
  #
  # ENJEKSIYONU ATLAMAK YETMEZ: esp/sources/boot.wim'in icinde bizim
  # dosyamiz zaten duruyor ve atlamak onu SILMEZ. O yuzden stock icerigi
  # geri YAZIYORUZ - WIM'i bastan uretmeye (LZX, dakikalar) gerek kalmiyor.
  #
  STOKTMP="$W/stok_startnet.cmd"
  if [ "$STOK_STARTNET" = 1 ]; then
    printf 'wpeinit\r\n' > "$STOKTMP"
    SN="$STOKTMP"
    say "STOK startnet.cmd geri yaziliyor (tek satir: wpeinit)"
  else
    SN="$(dirname "$ESP")/windows/startnet.cmd"
  fi
  if [ -f "$SN" ]; then
    say "startnet.cmd enjekte ediliyor"
    # CRLF ZORUNLU. Batch yorumlayicisi LF ile etiketleri ve "goto"yu
    # bozabiliyor; script kirilinca cmd.exe cikiyor ve WinPE yeniden
    # baslatiyor. Bir tur boyle gecti.
    SNTMP="$W/startnet.cmd"
    sed 's/\r*$/\r/' "$SN" > "$SNTMP"
    printf '  satir sonu: %s\n' "$(file -b "$SNTMP" | grep -o 'CRLF' || echo 'CRLF DEGIL!')"
    #
    # ONCE SIL, SONRA EKLE.
    #
    # "add" hedefte ayni isimde dosya varken onu degistirmiyor. b2lfb.sys'te
    # tam olarak bu oldu ve bir tur kaybettirdi. Burada AYNI TUZAK vardi ve
    # daha pahaliya mal olmus olabilir: startnet.cmd ile yaptigimiz butun
    # denemeler (goto, ping, pause, sonsuz dongu) cihaza HIC ULASMAMIS
    # olabilir. Hepsi ayni sonucu vermesinin en basit aciklamasi bu.
    #
    wimlib-imagex update "$ESP/sources/boot.wim" 1 \
      --command="delete --force /Windows/System32/startnet.cmd" >/dev/null 2>&1 || true
    wimlib-imagex update "$ESP/sources/boot.wim" 1 \
      --command="add '$SNTMP' /Windows/System32/startnet.cmd" >/dev/null
    #
    # UYUSMAZLIK ARTIK OLUMCUL.
    #
    # Onceki surum yalnizca "! UYARI" basiyordu ve elli satirlik ciktinin
    # icinde kayboluyordu. Bir dogrulamanin degeri, basarisiz oldugunda
    # ISI DURDURMASINDAN geliyor; yoksa sadece gurultu.
    #
    CK="$W/verify"; mkdir -p "$CK"
    if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
         /Windows/System32/startnet.cmd --dest-dir="$CK" --no-acls >/dev/null 2>&1; then
      A="$(sha1sum "$SNTMP" | cut -c1-12)"; B="$(sha1sum "$CK/startnet.cmd" | cut -c1-12)"
      if [ "$A" = "$B" ]; then
        echo "  kondu ve DOGRULANDI (sha1=$A)"
      else
        echo
        echo "  ###############################################################"
        echo "  # WIM'DEKI startnet.cmd FARKLI - eski surum kalmis."
        echo "  #   yazilan: $A"
        echo "  #   WIM'deki: $B"
        echo "  # Flaslamak bosuna; cihaza eski betik gider."
        echo "  ###############################################################"
        exit 1
      fi
    else
      echo "  ! startnet.cmd geri okunamadi - dogrulanamiyor"
      exit 1
    fi
  fi

  # Butunluk: wimlib her dosyanin SHA1'ini saklar, verify hepsini kontrol eder.
  if wimlib-imagex verify "$ESP/sources/boot.wim" >/dev/null 2>&1; then
    echo "  butunluk: TAMAM"
  else
    echo "  butunluk: BASARISIZ - WIM bozuk!"
    exit 1
  fi
fi


say "ESP agacina yerlestiriliyor"
rm -rf "$ESP/EFI/Microsoft/Boot/Fonts"   # onceki turdan kalan 83 MB CJK font
mkdir -p "$ESP/EFI/Microsoft/Boot"
cp "$BM"  "$ESP/EFI/Microsoft/Boot/bootmgfw.efi"
#
# Kendi BCD'miz varsa ISO'nunkine tercih et.
#
# ISO'nun BCD'si Setup icin kurulmus ve tani secenekleri kapali. Cihazda
# winload calisiyor ama sessizce yeniden basliyor - ne cokme ekrani ne kod.
# windows/bcd_olustur.cmd bunlari acik uretiyor (recoveryenabled No,
# bootstatuspolicy IgnoreAllFailures, quietboot No, sos On).
#
OWNBCD="$(dirname "$ESP")/windows/BCD"
if [ -s "$OWNBCD" ] && [ "$(head -c4 "$OWNBCD")" = "regf" ]; then
  cp "$OWNBCD" "$ESP/EFI/Microsoft/Boot/BCD"
  echo "  BCD: KENDI URETTIGIMIZ ($(stat -c%s "$OWNBCD") bayt, tani acik)"
else
  if [ "$DISK_MODU" = 1 ]; then
    echo "HATA: disk modu icin windows/BCD yok veya gecerli bir registry hive degil." >&2
    echo "Once: bash tools_ufsboot.sh /mnt/c/.../Windows_ARM64.iso" >&2
    exit 1
  fi
  cp "$BCD" "$ESP/EFI/Microsoft/Boot/BCD"
  echo "  BCD: ISO'nunki (tani kapali - windows/bcd_olustur.cmd calistir)"
fi
mkdir -p "$ESP/boot"
cp "$SDI" "$ESP/boot/boot.sdi"
if [ -d "$W/fonts" ] && [ -n "$(ls -A "$W/fonts" 2>/dev/null)" ]; then
  mkdir -p "$ESP/EFI/Microsoft/Boot/Fonts"
  cp "$W"/fonts/* "$ESP/EFI/Microsoft/Boot/Fonts/" 2>/dev/null || true
fi

#
# --disk : WinPE gercek bir NTFS bolumden aciliyor, RAM diski yok.
#
# boot.wim ve boot.sdi ESP'de GEREKSIZ: BCD "locate=" ile birimi kendisi
# tariyor. 600 MB'lik WIM'i 600 MiB'lik CACHE'e sokmaya calismak zaten
# imkansizdi, ESP'yi SYSTEM'e tasimamizin sebebi oydu. Disk modunda o
# zorunluluk kalkiyor ve ESP tekrar CACHE'e sigiyor.
#
# BU BLOK YERLESTIRMEDEN SONRA OLMAK ZORUNDA: onceki surumde ustteydi ve
# hemen ardindan gelen "cp $SDI $ESP/boot/boot.sdi" satiri boot.sdi'yi geri
# koyuyordu. Silme isleminin son soz olmasi lazim.
#
if [ "$DISK_MODU" = 1 ]; then
  say "DISK MODU: boot.wim ve boot.sdi ESP'den cikariliyor"
  rm -f "$ESP/sources/boot.wim" "$ESP/boot/boot.sdi"
  rmdir "$ESP/sources" "$ESP/boot" 2>/dev/null || true
  echo "  ESP artik yalnizca bootmgfw + BCD + fontlar tasiyor"
  #
  # startup.nsh Windows'u BCD'nin varligindan taniyor (boot.wim'den degil).
  # Bu satir onu dogruluyor: BCD yoksa cihaz yine "Acilacak bir sey
  # bulunamadi" der ve bir Odin flasi bosa gider.
  #
  if [ -s "$ESP/EFI/Microsoft/Boot/BCD" ]; then
    echo "  startup.nsh tetikleyicisi (BCD): VAR"
  else
    echo "  ! BCD YOK - startup.nsh Windows'u bulamaz!"
  fi
fi

#
# SIRA KORUMASI: b2lfb WIM'den dusmus mu?
#
# tools_winboot.sh WIM'e dokunuyor; --wim-yenile ile onu bastan uretiyor.
# tools_b2lfb.sh'ten SONRA calisirsa surucu de SYSTEM hive'indaki servis
# kaydi da ucar ve cihazda hicbir sey gorunmez - ki bu "surucu yuklenmedi"
# diye yanlis yorumlanir.
#
# Dogru sira: once tools_winboot.sh, EN SON tools_b2lfb.sh.
#
if [ "$DISK_MODU" = 0 ] && [ -s "$(dirname "$ESP")/out/b2lfb/b2lfb.sys" ]; then
  if ! wimlib-imagex dir "$ESP/sources/boot.wim" 1 2>/dev/null | grep -qi 'b2lfb\.sys'; then
    echo
    echo "  ###############################################################"
    echo "  # b2lfb.sys derlenmis ama BU WIM'DE YOK."
    echo "  #"
    echo "  # tools_b2lfb.sh HER ZAMAN EN SON calisir:"
    echo "  #     bash tools_b2lfb.sh"
    echo "  #     bash cache_esp.sh --win --system"
    echo "  ###############################################################"
  fi
fi

say "Sonuc"
SONUC_YOLLARI=()
for yol in "$ESP/EFI/Microsoft" "$ESP/boot" "$ESP/sources"; do
  [ -e "$yol" ] && SONUC_YOLLARI+=("$yol")
done
if [ ${#SONUC_YOLLARI[@]} -gt 0 ]; then
  find "${SONUC_YOLLARI[@]}" -type f -exec ls -lh {} \; | awk '{print "  "$5"\t"$9}'
fi
echo
echo "Toplam: $(du -sh "$ESP" | cut -f1)  (CACHE bolumu 600 MB, imaj 75 MB)"
echo
echo "Sonra:"
if [ "$DISK_MODU" = 1 ]; then
  echo "  bash cache_esp.sh --win --disk --baseline"
  echo "  SYSTEM + CACHE'i birlikte flasla; bootmgfw NTFS SYSTEM'deki Windows'u acar"
else
  echo "  bash cache_esp.sh --win"
  echo "  cache'i flasla, ac, startup.nsh Windows Boot Manager'i baslatacak"
fi
