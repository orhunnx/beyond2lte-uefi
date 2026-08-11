#!/usr/bin/env bash
#
# B2LFB surucusunu WSL'de derler, imzalar ve boot.wim'e acilis surucusu
# olarak yerlestirir.
#
#   bash tools_b2lfb.sh
#
# ---------------------------------------------------------------------------
# NEDEN VISUAL STUDIO / WDK YOK
# ---------------------------------------------------------------------------
# Bir Windows cekirdek surucusu, ozunde /SUBSYSTEM:NATIVE bir PE dosyasi.
# WDK'nin sagladigi sey basliklar ve ntoskrnl icin bir import kitapligi.
# Bizim surucumuz ntoskrnl'den TEK bir fonksiyon kullaniyor (MmMapIoSpace),
# o yuzden ikisini de elde uretmek birkac satir:
#
#   - basliklar  -> b2lfb.c icinde elle bildirildi
#   - import lib -> llvm-dlltool ile .def'ten uretiliyor
#
# Boylece butun zincir WSL'de kaliyor: Windows makinesine gecip proje acmak,
# derleme ayari kurcalamak yok. Her tur bir Odin flasina mal oldugu icin
# gelistirme dongusunu tek makinede tutmak onemli.
#
# ---------------------------------------------------------------------------
# IMZA
# ---------------------------------------------------------------------------
# ARM64 Windows cekirdek modu imza istiyor. Bunu bir kez gorduk:
#   ws2ifsl.sys / 0xc0000428 / STATUS_INVALID_IMAGE_HASH
#
# Kendi sertifikamizla imzaliyoruz (osslsigncode) ve BCD'de "testsigning on"
# aciyoruz. Secure Boot'un zorlamadigini OLCTUK: imzasiz EBSTEST.EFI cihazda
# calisti ve bootmgfw yuklenirken "[Security] 3rd party image can be loaded"
# satiri gecti.
#
# ACIK KALAN: test imzali bir ACILIS surucusunun sertifikasinin WinPE'nin
# guven deposunda da bulunmasi gerekebilir. Gerekirse cihaz bunu 0xc0000428
# ile SOYLEYECEK - yani basarisizlik da okunabilir bir sonuc. Sessizlikten
# iyi.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/windows/b2lfb"
WIM="$HERE/esp/sources/boot.wim"
OUT="$HERE/out/b2lfb"; mkdir -p "$OUT"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

for t in clang lld-link llvm-dlltool; do
  command -v $t >/dev/null || { echo "$t yok -> sudo apt install -y clang lld llvm"; exit 1; }
done
command -v osslsigncode >/dev/null || { echo "osslsigncode yok -> sudo apt install -y osslsigncode"; exit 1; }
command -v openssl      >/dev/null || { echo "openssl yok -> sudo apt install -y openssl"; exit 1; }

#
# 1) ntoskrnl import kitapligi
#
# lld-link "MmMapIoSpace nerede" diye soracak. Cevabi bir .def dosyasi:
# fonksiyon adlari ve hangi DLL'de olduklari. llvm-dlltool bundan gercek
# bir .lib uretiyor; ntoskrnl.exe'nin kendisine ihtiyac yok.
#
say "ntoskrnl import kitapligi uretiliyor"
cat > "$OUT/ntoskrnl.def" <<'DEF'
LIBRARY ntoskrnl.exe
EXPORTS
MmMapIoSpace
KeRegisterBugCheckCallback
KeInitializeDpc
KeInitializeTimer
KeSetTimerEx
DEF
llvm-dlltool -m arm64 -d "$OUT/ntoskrnl.def" -l "$OUT/ntoskrnl.lib"
echo "  $(basename "$OUT/ntoskrnl.lib") hazir"

#
# 2) Derleme
#
# -ffreestanding : C kutuphanesi yok, cekirdekte zaten olmaz
# -fno-stack-protector : __security_cookie sembolunu istemesin
# -mno-stack-arg-probe : __chkstk istemesin
# Ucu de "linkte cozulemeyen sembol" hatasini onlemek icin; WDK olmadan
# derlerken en sik carpilan uc tas bunlar.
#
say "Derleniyor (aarch64-windows-msvc)"
clang -target aarch64-windows-msvc \
      -ffreestanding -fno-stack-protector -mno-stack-arg-probe \
      -O1 -Wall -Wextra \
      -c "$SRC/b2lfb.c" -o "$OUT/b2lfb.obj"
echo "  b2lfb.obj: $(stat -c%s "$OUT/b2lfb.obj") bayt"

#
# 3) Baglama
#
# /driver      : cekirdek surucusu bayraklari
# /subsystem:native : cekirdek modu PE
# /entry:DriverEntry : varsayilan giris noktasi degil, bizimki
#
say "Baglaniyor"
lld-link /driver /subsystem:native /entry:DriverEntry /machine:arm64 \
         /nodefaultlib \
         /out:"$OUT/b2lfb.sys" "$OUT/b2lfb.obj" "$OUT/ntoskrnl.lib"
echo "  b2lfb.sys: $(stat -c%s "$OUT/b2lfb.sys") bayt"
file "$OUT/b2lfb.sys" | sed 's/^/  /'

#
# 4) Sertifika (bir kez uretiliyor, sonra tekrar kullaniliyor)
#
# Sertifika DEGISIRSE cihazdaki guven deposuna eklenen kopya gecersiz kalir,
# o yuzden var olani koruyoruz.
#
CERT="$HERE/windows/b2lfb/b2l-test.pem"
KEY="$HERE/windows/b2lfb/b2l-test.key"
if [ ! -s "$CERT" ] || [ ! -s "$KEY" ]; then
  say "Test sertifikasi uretiliyor (bir kereye mahsus)"
  openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$KEY" -out "$CERT" \
    -subj "/CN=beyond2lte test signing" 2>/dev/null
  echo "  uretildi: $(basename "$CERT")"
else
  echo
  echo "  mevcut sertifika kullaniliyor: $(basename "$CERT")"
fi

say "Imzalaniyor"
osslsigncode sign -certs "$CERT" -key "$KEY" \
  -n "beyond2lte framebuffer" -h sha256 \
  -in "$OUT/b2lfb.sys" -out "$OUT/b2lfb-signed.sys" 2>&1 | sed 's/^/  /'
mv -f "$OUT/b2lfb-signed.sys" "$OUT/b2lfb.sys"

#
# 5) WIM'e yerlestirme
#
[ -s "$WIM" ] || { echo; echo "boot.wim yok - once tools_winboot.sh calistir."; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok"; exit 1; }
command -v hivexregedit  >/dev/null || { echo "hivexregedit yok -> sudo apt install -y libwin-hivex-perl"; exit 1; }

#
# ONCE SIL, SONRA EKLE, SONRA GERI OKU
#
# BU BIR TURA MAL OLDU. Surucu guncellendi, "TIK=..." kaydi eklendi, ama
# cihazda hala yalnizca "B2LF" gorunuyordu - yani ESKI surucu calisiyordu.
#
# Sebep: "add" hedefte ayni isimde bir dosya varken onu degistirmeyebiliyor.
# Script ciktiyi /dev/null'a atiyordu, dolayisiyla hicbir uyari gorunmedi ve
# her sey basarili sanildi.
#
# Ayni sinifin ucuncu hatasi:
#   1. hivexsh setval dugumdeki diger degerleri siliyordu
#   2. .reg icindeki ters bolukler kacis olarak yutuluyordu
#   3. simdi bu
# Ucunde de ortak ders: YAZDIGINI GERI OKU.
#
say "boot.wim'e ekleniyor"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/drivers/b2lfb.sys" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$OUT/b2lfb.sys' /Windows/System32/drivers/b2lfb.sys" >/dev/null
echo "  /Windows/System32/drivers/b2lfb.sys"

CK="$OUT/dogrulama"; rm -rf "$CK"; mkdir -p "$CK"
if wimlib-imagex extract "$WIM" 1 /Windows/System32/drivers/b2lfb.sys \
     --dest-dir="$CK" --no-acls >/dev/null 2>&1; then
  A="$(sha1sum "$OUT/b2lfb.sys"  | cut -c1-16)"
  B="$(sha1sum "$CK/b2lfb.sys"   | cut -c1-16)"
  if [ "$A" = "$B" ]; then
    echo "  dogrulandi: WIM'deki surucu derledigimizle AYNI ($A)"
  else
    echo
    echo "  ###############################################################"
    echo "  # WIM'DEKI SURUCU FARKLI - eski surum kalmis."
    echo "  #   derlenen: $A"
    echo "  #   WIM'deki: $B"
    echo "  # Flaslamak bosuna; cihaza eski kod gider."
    echo "  ###############################################################"
    exit 1
  fi
else
  echo "  ! b2lfb.sys WIM'den geri okunamadi - dogrulanamiyor"
  exit 1
fi
rm -rf "$CK"

#
# 6) Servis kaydi - ACILIS SURUCUSU
#
# Start=0  BOOT   : winload yukluyor, cekirdekten once
# Type=1          : cekirdek surucusu
# Group           : BOS BIRAKILDI - surucu EN SONDA yuklensin.
#
#   Onceden "System Bus Extender" idi, yani en erken gruplardan biri. Orada
#   DriverEntry'yi sonsuza kadar bloke edince sifirlama HIC olmadi (25+
#   serit). Yani tetikleyici bizden SONRA.
#
#   Bos Group, gruplu butun acilis surucilerinden SONRA yuklenmek demek.
#   Ayni engelleme numarasi, cok daha ileri bir noktada - boylece "acilis
#   surucileri yuklenirken mi, yoksa daha sonra mi" sorusu ayriliyor.
#
# ErrorControl=3 (CRITICAL) - ILK SURUMDE 0 IDI VE BU BIR TASARIM HATASIYDI.
#
# "Surucu patlarsa acilis devam etsin, yeni ariza kaynagi yaratmayalim" diye
# 0 (IGNORE) yazmistim. Sonucu: imza reddedilirse winload surucuyu SESSIZCE
# atliyor. Yani elimizdeki EN BILGILENDIRICI arizayi - ws2ifsl denemesinde
# gordugumuz "0xc0000428 STATUS_INVALID_IMAGE_HASH" ekranini - kendi elimle
# sessizlige cevirmisim. Ilk denemede ne macenta ne RAM imzasi cikmasinin
# en olasi sebebi bu.
#
# 3 ile tam tersi oluyor: surucu yuklenemezse winload DURUYOR ve sebebi
# ekrana yaziyor. Acilis olmuyor ama bu turda acilis zaten hedef degil;
# hedef surucunun yuklenip yuklenmedigini OGRENMEK.
#
#   Macenta ekran   -> surucu yuklendi ve calisti
#   Hata ekrani     -> surucu yuklenemedi, SEBEBI YAZIYOR
#   Ikisi de yok    -> winload acilis surucilerine hic gelmiyor
#
# Uc okumanin ucu de bilgi. Onceki surumde ikisi ayni goruntuyu veriyordu.
#
# hivexsh'in setval'i KULLANILMIYOR: o komut dugumdeki diger butun degerleri
# siliyor ve bu daha once bes olcumu gecersiz kilmisti. hivexregedit --merge
# yalnizca verilen degerleri yaziyor.
#
say "SYSTEM hive'ina acilis servisi olarak yaziliyor"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$W" --no-acls >/dev/null

cat > "$W/b2lfb.reg" <<'REG'
[HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services\b2lfb]
"Type"=dword:00000001
"Start"=dword:00000000
"ErrorControl"=dword:00000003
"ImagePath"=str(2):"\\SystemRoot\\System32\\drivers\\b2lfb.sys"
"Group"=""
"DisplayName"="beyond2lte framebuffer"
REG
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$W/SYSTEM" "$W/b2lfb.reg"

#
# IMAGEPATH GERI OKUNUYOR - BU KONTROL BIR TURA MAL OLDU
#
# Ilk surumde .reg icinde "\SystemRoot\System32\drivers\b2lfb.sys" yaziyordu.
# .reg biciminde ters boluk KACIS KARAKTERI; hive'a
# "SystemRootSystem32driversb2lfb.sys" olarak girdi ve cihaz sunu bastı:
#
#   File: \windows\SystemRootSystem32driversb2lfb.sys
#   Status: 0xc000000f  (dosya bulunamadi)
#
# Yaziyi yazip geri okumamak, yazdigini varsaymaktir. Artik okuyoruz: ters
# boluk sayisi ucten azsa yol bozulmus demektir ve script oracikta duruyor -
# bir Odin flasi harcanmadan.
#
YOL="$(hivexget "$W/SYSTEM" 'ControlSet001\Services\b2lfb' ImagePath 2>/dev/null || true)"
echo "  ImagePath geri okundu: $YOL"
if [ "$(printf '%s' "$YOL" | tr -cd '\\' | wc -c)" -lt 3 ]; then
  echo
  echo "  ###############################################################"
  echo "  # ImagePath BOZUK - ters boluk kacislari kaybolmus."
  echo "  # Beklenen: \\SystemRoot\\System32\\drivers\\b2lfb.sys"
  echo "  # .reg dosyasinda ters bolukler ciftlenmeli."
  echo "  ###############################################################"
  exit 1
fi

#
# Hive de ayni tuzakta: once sil, sonra ekle. Yoksa duzenlenmis hive WIM'e
# hic girmez ve servis kaydi eski halinde kalir.
#
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$W/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
echo "  Services\\b2lfb  Start=0 Type=1 ErrorControl=3 (CRITICAL)"

#
# Hive de geri okunuyor: WIM'deki kopyada servis gercekten var mi?
#
HK="$W/geri"; mkdir -p "$HK"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$HK" --no-acls >/dev/null 2>&1
GERI="$(hivexget "$HK/SYSTEM" 'ControlSet001\Services\b2lfb' ImagePath 2>/dev/null || true)"
if [ -n "$GERI" ]; then
  echo "  dogrulandi: WIM'deki hive'da ImagePath = $GERI"
else
  echo
  echo "  ###############################################################"
  echo "  # WIM'DEKI HIVE'DA b2lfb SERVISI YOK - duzenleme geri yazilmamis."
  echo "  ###############################################################"
  exit 1
fi

say "Dogrulama"
wimlib-imagex dir "$WIM" 1 2>/dev/null | grep -i "b2lfb" | sed 's/^/  /' || echo "  ! b2lfb WIM'de gorunmuyor"

cat <<'SON'

SIRA:
  1) BCD'de test imzalamayi ac. Windows'ta:
        windows\bcd_olustur.cmd testimza
     (BCD'yi kopyala, sonra tools_winboot.sh calistir)

  2) bash cache_esp.sh --win --system      ve system-esp.tar'i flasla

BEKLENEN OKUMA  (ErrorControl=3 ile artik hepsi ayirt edilebiliyor):
  EKRAN MACENTA                -> surucu yuklendi VE calisti
  Hata ekrani + 0xc0000428     -> imza reddedildi. Sertifika WinPE'nin guven
                                  deposuna eklenecek - cozulebilir, ve artik
                                  sebebi EKRANDA yaziyor
  Baska bir hata kodu          -> surucu yuklenemedi, kod sebebi soyluyor
  Hicbir sey, eskisi gibi      -> winload acilis surucilerine HIC gelmiyor;
                                  ariza daha geride

  Sonraki acilista startup.nsh dokumunde 0xCF030000'da "42 32 4C 46" (B2LF)
  gorunuyorsa surucu calismis demektir - macenta cikmasa bile.

  DIKKAT: cokusten sonra cihazi ELLE KAPATMA. RAM kaydi yalnizca sicak
  sifirlamada kaliyor.
SON
