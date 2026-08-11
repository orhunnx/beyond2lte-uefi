#!/usr/bin/env bash
#
# ADK'nin ciplak WinPE'sini esp/ agacina yerlestirir.
#
#   bash tools_winpe_adk.sh /mnt/c/WinPE_arm64/media
#
# ---------------------------------------------------------------------------
# NEDEN AYRI BIR SCRIPT
# ---------------------------------------------------------------------------
# tools_winboot.sh bir Windows ISO'sundan cikarim yapiyor: dosyalari ariyor,
# iki indeksli boot.wim'i buduyor, Setup imajini siliyor. ADK'nin ciktisinda
# bunlarin hicbiri gerekmiyor - "copype arm64" zaten dogru agaci uretiyor.
# Ayri script tutmak ikisini de sade birakiyor ve calisan ISO yolunu
# bozmuyor.
#
# ---------------------------------------------------------------------------
# NEDEN ADK WinPE
# ---------------------------------------------------------------------------
# ISO'nun boot.wim'i 570 MB ve icinde Setup sihirbazi, surucu deposu, dil
# kaynaklari var. ADK'nin WinPE'si ~250 MB ve gercekten ciplak: cekirdek,
# WinPE kabugu, bitti. Cihaz ~12 saniyede sessizce sifirlaniyor ve bu sure
# ACPI icerigine gore degisiyor (storufs kapatilinca 5-6'dan 12'ye ciktu),
# yani sorun cekirdek yuklendikten SONRA, aygit/surucu tarafinda.
#
# Ciplak WinPE bu ekseni ikiye boluyor:
#   AYNI 12 saniyede olurse -> sorun platformda, Windows surumunde degil.
#                              Buyuk bir eleme; Windows tarafini kapatiriz.
#   daha uzun dayanirsa     -> ustune insa edilecek CALISAN bir taban.
#
# ---------------------------------------------------------------------------
# WINDOWS TARAFINDA HAZIRLIK
# ---------------------------------------------------------------------------
#   1) Windows ADK + "Windows PE add-on for the Windows ADK" kur (ayri indirme)
#   2) Yonetici olarak "Deployment and Imaging Tools Environment" ac
#   3) copype arm64 C:\WinPE_arm64
#
# Cikan agac: C:\WinPE_arm64\media\{EFI,boot,sources}
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESP="$HERE/esp"

MEDIA="${1:-}"
if [ -z "$MEDIA" ]; then
  for c in /mnt/c/WinPE_arm64/media /mnt/c/WinPE_amd64/media "$HOME"/WinPE_arm64/media; do
    [ -d "$c" ] && { MEDIA="$c"; break; }
  done
fi
[ -d "$MEDIA" ] || {
  echo "Media klasoru bulunamadi. Yolu argüman olarak ver:"
  echo "  bash tools_winpe_adk.sh /mnt/c/WinPE_arm64/media"
  exit 1
}

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok -> sudo apt install -y wimtools"; exit 1; }

say "Media: $MEDIA"

#
# Buyuk/kucuk harf ISO'dan ISO'ya, ADK'dan ADK'ya degisiyor. Aramak
# varsaymaktan ucuz - bir tur "BCD bulunamadi" ile gecmisti.
#
find_ci() {  # $1 = media'ya gore yol deseni
  find "$MEDIA" -ipath "$MEDIA/$1" -type f 2>/dev/null | head -1
}

BOOTAA="$(find_ci 'EFI/Boot/bootaa64.efi')"
SDI="$(find_ci    'boot/boot.sdi')"
WIM="$(find_ci    'sources/boot.wim')"

echo "  bootaa64.efi : ${BOOTAA:-BULUNAMADI}"
echo "  boot.sdi     : ${SDI:-BULUNAMADI}"
echo "  boot.wim     : ${WIM:-BULUNAMADI}"

[ -n "$BOOTAA" ] || { echo "HATA: bootaa64.efi yok. copype arm64 calistirdin mi?"; exit 1; }
[ -n "$SDI" ]    || { echo "HATA: boot.sdi yok."; exit 1; }
[ -n "$WIM" ]    || { echo "HATA: boot.wim yok."; exit 1; }

#
# ADK'nin WinPE'si ARM64 mi? amd64 icin copype calistirmak kolay bir hata ve
# cihazda "bu imaj bu makine icin degil" diye sessiz bir hataya donusuyor.
#
ARCH="$(wimlib-imagex info "$WIM" 1 2>/dev/null | grep -i '^Architecture' | tr -s ' ' | cut -d' ' -f2- || true)"
echo "  mimari       : ${ARCH:-bilinmiyor}"
case "${ARCH,,}" in
  *arm64*|*aarch64*) ;;
  "") echo "  UYARI: mimari okunamadi, devam ediliyor" ;;
  *) echo "HATA: WIM ARM64 degil ($ARCH). 'copype arm64' ile uret."; exit 1 ;;
esac

say "esp/ agacina yerlestiriliyor"
rm -rf "$ESP/EFI/Microsoft/Boot/Fonts"     # ADK'da da CJK fontlar var, 80 MB+
mkdir -p "$ESP/EFI/Microsoft/Boot" "$ESP/boot" "$ESP/sources"

# bootaa64.efi -> bootmgfw.efi. BCD'yi \EFI\Microsoft\Boot\ altinda aradigi
# icin startup.nsh onu oradan cagiriyor.
cp "$BOOTAA" "$ESP/EFI/Microsoft/Boot/bootmgfw.efi"
cp "$SDI"    "$ESP/boot/boot.sdi"

#
# BCD: kendi urettigimiz varsa o. bcd_olustur.cmd tani secenekleriyle
# uretiyor (numproc 1 dahil) ve ADK'nin BCD'si bizim yerlesimimizi bilmiyor.
#
OWNBCD="$HERE/windows/BCD"
ADKBCD="$(find_ci 'EFI/Microsoft/Boot/BCD')"
if [ -s "$OWNBCD" ]; then
  cp "$OWNBCD" "$ESP/EFI/Microsoft/Boot/BCD"
  echo "  BCD: windows/BCD (bizimki)"
elif [ -n "$ADKBCD" ]; then
  cp "$ADKBCD" "$ESP/EFI/Microsoft/Boot/BCD"
  echo "  BCD: ADK'ninki - numproc 1 YOK, bcd_olustur.cmd tekcpu calistir"
else
  echo "HATA: BCD yok."; exit 1
fi
[ "$(head -c4 "$ESP/EFI/Microsoft/Boot/BCD")" = "regf" ] || {
  echo "HATA: BCD registry hive gibi gorunmuyor."; exit 1; }

# Latin fontlar. CJK olanlar 80 MB tutuyor ve bir tur "Disk full" ile gecti.
FDIR="$(dirname "$BOOTAA")/../Microsoft/Boot/Fonts"
if [ -d "$FDIR" ]; then
  mkdir -p "$ESP/EFI/Microsoft/Boot/Fonts"
  for f in wgl4_boot segmono_boot segoe_slboot segoen_slboot; do
    cp "$FDIR/$f.ttf" "$ESP/EFI/Microsoft/Boot/Fonts/" 2>/dev/null || true
  done
  echo "  font: $(ls "$ESP/EFI/Microsoft/Boot/Fonts" 2>/dev/null | wc -l) dosya"
fi

say "boot.wim hazirlaniyor"
rm -f "$ESP/sources/boot.wim"
cp "$WIM" "$ESP/sources/boot.wim"
echo "  boyut: $(du -h "$ESP/sources/boot.wim" | cut -f1)"

#
# ACILABILIR OLARAK ISARETLE. Iki tur bunun yuzunden gitti:
#   winload.efi ... 0xc0000487 = STATUS_WIM_NOT_BOOTABLE
# ADK'nin WinPE'si tek indeksli ama Boot Index'i 0 olabiliyor.
#
wimlib-imagex info "$ESP/sources/boot.wim" 1 --boot >/dev/null
wimlib-imagex info "$ESP/sources/boot.wim" | grep -i '^Boot Index' | sed 's/^/  /'

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

#
# AutoReboot=0. Bugcheck olursa cihaz yeniden baslamak yerine mavi ekranda
# kalsin - su anki cokuste ekranda hicbir sey gorunmuyor ve bu ayarin
# gercekten uygulandigini bilmek onemli.
#
if command -v hivexregedit >/dev/null; then
  say "WinPE SYSTEM hive: AutoReboot=0"
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
      echo "  yazildi"
    else
      echo "  ! UYARI: hive birlestirilemedi"
    fi
  else
    echo "  ! UYARI: SYSTEM hive cikarilamadi"
  fi
else
  echo "  hivexregedit yok -> sudo apt install -y libhivex-bin"
fi

#
# startnet.cmd. WinPE acilista \Windows\System32\startnet.cmd calistirir.
# Bizimki wpeinit cagirmiyor ve sonsuz donguye giriyor - ekranda kalirsa
# WinPE'nin ayakta oldugunu kanitlar.
#
# CRLF ZORUNLU: bir tur LF ile gitti ve batch yorumlayici satirlari
# birlestirdi.
#
SN="$HERE/windows/startnet.cmd"
if [ -s "$SN" ]; then
  say "startnet.cmd enjekte ediliyor"
  sed 's/\r$//; s/$/\r/' "$SN" > "$W/startnet.cmd"
  wimlib-imagex update "$ESP/sources/boot.wim" 1 \
    --command="add '$W/startnet.cmd' /Windows/System32/startnet.cmd" >/dev/null
  CK="$W/ck"; mkdir -p "$CK"
  if wimlib-imagex extract "$ESP/sources/boot.wim" 1 \
       /Windows/System32/startnet.cmd --dest-dir="$CK" --no-acls >/dev/null 2>&1; then
    A="$(sha1sum "$W/startnet.cmd" | cut -c1-12)"
    B="$(sha1sum "$CK/startnet.cmd" | cut -c1-12)"
    if [ "$A" = "$B" ]; then echo "  dogrulandi ($A)"; else echo "  ! UYARI: farkli ($A vs $B)"; fi
  fi
fi

wimlib-imagex optimize "$ESP/sources/boot.wim" >/dev/null

say "Sonuc"
find "$ESP/EFI/Microsoft" "$ESP/boot" "$ESP/sources" -type f 2>/dev/null \
  -exec ls -lh {} \; | awk '{print "  "$5"\t"$9}'
TOT=$(du -sm "$ESP" | cut -f1)
echo
echo "  esp/ toplam: ${TOT} MB   (CACHE bolumu 600 MB)"
[ "$TOT" -gt 560 ] && echo "  ! UYARI: bolume sigmayabilir"
echo
echo "Simdi: bash cache_esp.sh --win"
