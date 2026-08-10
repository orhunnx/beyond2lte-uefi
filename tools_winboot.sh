#!/usr/bin/env bash
#
# Windows Boot Manager'i ISO'dan cikarip ESP'ye hazirlar.
#
#   bash tools_winboot.sh [/yol/Win11_..._Arm64.iso]
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
ARGS=()
for a in "$@"; do
  case "$a" in
    --wim-yenile) FORCE_WIM=1 ;;
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
command -v 7z >/dev/null || { echo "7z yok -> sudo apt install -y p7zip-full"; exit 1; }

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
if [ "$FORCE_WIM" = 0 ] && [ -s "$ESP/sources/boot.wim" ] \
   && wimlib-imagex info "$ESP/sources/boot.wim" 2>/dev/null | grep -qi '^Boot Index: *1'; then
  say "boot.wim hazir, atlaniyor ($(du -h "$ESP/sources/boot.wim" | cut -f1), Boot Index 1)"
  echo "  yeniden uretmek icin: bash tools_winboot.sh --wim-yenile"
elif [ -n "$WIM_PATH" ] && command -v wimlib-imagex >/dev/null; then
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
  if command -v hivexregedit >/dev/null; then
    say "WinPE registry: AutoReboot kapatiliyor"
    HV="$W/hive"; mkdir -p "$HV"
    if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
         /Windows/System32/config/SYSTEM --dest-dir="$HV" --no-acls >/dev/null 2>&1; then
      cat > "$W/crash.reg" <<'REG'
[HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\CrashControl]
"AutoReboot"=dword:00000000
"DisplayParameters"=dword:00000001
REG
      if hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' \
           "$HV/SYSTEM" "$W/crash.reg" 2>/dev/null; then
        wimlib-imagex update "$ESP/sources/boot.wim" 1 \
          --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
        wimlib-imagex optimize "$ESP/sources/boot.wim" >/dev/null
        echo "  AutoReboot=0 yazildi, WIM guncellendi ($(du -h "$ESP/sources/boot.wim" | cut -f1))"
      else
        echo "  UYARI: hive duzenlenemedi, AutoReboot acik kaliyor"
      fi
    else
      echo "  UYARI: SYSTEM hive cikarilamadi"
    fi
  else
    echo "  hivexregedit yok -> sudo apt install -y libhivex-bin"
    echo "  (AutoReboot acik kalir; cokunce kod gorulmez)"
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
  SN="$(dirname "$ESP")/windows/startnet.cmd"
  if [ -f "$SN" ]; then
    say "startnet.cmd enjekte ediliyor"
    # CRLF ZORUNLU. Batch yorumlayicisi LF ile etiketleri ve "goto"yu
    # bozabiliyor; script kirilinca cmd.exe cikiyor ve WinPE yeniden
    # baslatiyor. Bir tur boyle gecti.
    SNTMP="$W/startnet.cmd"
    sed 's/\r*$/\r/' "$SN" > "$SNTMP"
    printf '  satir sonu: %s\n' "$(file -b "$SNTMP" | grep -o 'CRLF' || echo 'CRLF DEGIL!')"
    wimlib-imagex update "$ESP/sources/boot.wim" 1 \
      --command="add '$SNTMP' /Windows/System32/startnet.cmd" >/dev/null
    #
    # GERCEKTEN GIRDI MI? "add" mevcut dosyanin uzerine yazmazsa sessizce
    # eski surum kalir ve cihazda hicbir sey degismez. Geri okuyup SHA1
    # karsilastiriyoruz - imaj uretirken bilmek, cihazda tahmin etmekten iyi.
    #
    CK="$W/verify"; mkdir -p "$CK"
    if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
         /Windows/System32/startnet.cmd --dest-dir="$CK" --no-acls >/dev/null 2>&1; then
      A="$(sha1sum "$SNTMP" | cut -c1-12)"; B="$(sha1sum "$CK/startnet.cmd" | cut -c1-12)"
      if [ "$A" = "$B" ]; then echo "  kondu ve DOGRULANDI (sha1=$A)"
      else echo "  ! UYARI: WIM'deki startnet.cmd farkli ($A vs $B)"; fi
    else
      echo "  ! UYARI: startnet.cmd geri okunamadi"
    fi
  fi

  # Butunluk: wimlib her dosyanin SHA1'ini saklar, verify hepsini kontrol eder.
  if wimlib-imagex verify "$ESP/sources/boot.wim" >/dev/null 2>&1; then
    echo "  butunluk: TAMAM"
  else
    echo "  butunluk: BASARISIZ - WIM bozuk!"
  fi

  SZB=$(stat -c%s "$ESP/sources/boot.wim")
  LIMIT=$((600*1024*1024 - 12*1024*1024))
  echo "  boyut: $((SZB/1024/1024)) MB / sinir $((LIMIT/1024/1024)) MB"
  if [ "$SZB" -gt "$LIMIT" ]; then
    echo
    echo "  SIGMIYOR. Yeniden sikistirma kacinilmaz olabilir:"
    echo "    wimlib-imagex export <kaynak> 1 <hedef> --compress=LZX --rebuild"
    echo "  ya da daha buyuk bir bolum (SYSTEM 5.57 GB)."
  fi
else
  echo "  (boot.wim atlandi: ISO'da yok ya da wimlib-imagex kurulu degil)"
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
  cp "$BCD" "$ESP/EFI/Microsoft/Boot/BCD"
  echo "  BCD: ISO'nunki (tani kapali - windows/bcd_olustur.cmd calistir)"
fi
mkdir -p "$ESP/boot"
cp "$SDI" "$ESP/boot/boot.sdi"
if [ -d "$W/fonts" ] && [ -n "$(ls -A "$W/fonts" 2>/dev/null)" ]; then
  mkdir -p "$ESP/EFI/Microsoft/Boot/Fonts"
  cp "$W"/fonts/* "$ESP/EFI/Microsoft/Boot/Fonts/" 2>/dev/null || true
fi

say "Sonuc"
find "$ESP/EFI/Microsoft" "$ESP/boot" "$ESP/sources" -type f 2>/dev/null -exec ls -lh {} \; | awk '{print "  "$5"\t"$9}'
echo
echo "Toplam: $(du -sh "$ESP" | cut -f1)  (CACHE bolumu 600 MB, imaj 75 MB)"
echo
echo "Sonra:"
echo "  bash cache_esp.sh"
echo "  cache'i flasla, ac, startup.nsh Windows Boot Manager'i baslatacak"
