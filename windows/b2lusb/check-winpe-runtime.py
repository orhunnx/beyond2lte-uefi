#!/usr/bin/env python3
"""Read-only WIM preflight; never substitute DriverStore files for installed ones."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

VALIDATOR = runpy.run_path(str(Path(__file__).with_name('make-function-inf.py')))
REQUIRED = (
    '/Windows/System32/ntoskrnl.exe',
    '/Windows/System32/drivers/UcmCx.sys',
    '/Windows/System32/drivers/Urscx01000.sys',
    '/Windows/System32/drivers/Ufx01000.sys',
    '/Windows/System32/drivers/ufxsynopsys.sys',
    '/Windows/INF/ufxsynopsys.inf',
)


def resolve_image(source, work, emit, run=subprocess.run):
    if source.suffix.lower() != '.iso':
        return source
    listing = run(['7z', 'l', '-slt', str(source)], capture_output=True, text=True)
    if listing.returncode:
        raise ValueError(f'ISO listelenemedi: {listing.stderr.strip()}')
    paths = [line[7:] for line in listing.stdout.splitlines() if line.startswith('Path = ')]
    installs = [path for path in paths
                if re.fullmatch(r'sources[/\\]install\.(wim|esd)', path, re.I)]
    if len(installs) != 1:
        raise ValueError('ISO icinde tek sources/install.wim veya install.esd bulunamadi')
    emit('ISO kurulum imaji gecici dizine cikariliyor; birkac GB bos alan gerekebilir.')
    extraction = run(['7z', 'e', f'-o{work}', str(source), installs[0]],
                     capture_output=True, text=True)
    image = work / installs[0].replace('\\', '/').split('/')[-1]
    if extraction.returncode or not image.is_file():
        raise ValueError(f'ISO kurulum imaji cikarilamadi: {extraction.stderr.strip()}')
    return image


def compare_kernels(source, index, target, emit, run=subprocess.run):
    versions = []
    with tempfile.TemporaryDirectory(prefix='b2l-kernel-compare-') as work:
        for number, (image, image_index) in enumerate(((source, index), (target, 1))):
            dest = Path(work) / str(number)
            dest.mkdir()
            result = run(['wimlib-imagex', 'extract', str(image), str(image_index),
                          REQUIRED[0], f'--dest-dir={dest}', '--no-acls'],
                         env=dict(os.environ, WIMLIB_IMAGEX_IGNORE_CASE='1', LC_ALL='C'),
                         capture_output=True, text=True)
            files = [p for p in dest.rglob('*') if p.is_file() and p.name.lower() == 'ntoskrnl.exe']
            if result.returncode or len(files) != 1:
                raise ValueError(f'Karsilastirma cekirdegi okunamadi: {image}: {result.stderr.strip()}')
            version = VALIDATOR['arm64_build'](files[0].read_bytes())
            versions.append(version)
            emit(('KAYNAK' if number == 0 else 'HEDEF') + ' PE: ARM64 ' +
                 '.'.join(str(part) for part in version))
    if versions[0] != versions[1]:
        emit('UYUMSUZ: kaynak ve hedef major/minor/build farkli; runtime aktarimi yapilmaz.')
        return 1
    emit('Kaynak/hedef ARM64 build eslesiyor; paket/servisleme uygunlugu ayrica dogrulanmali.')
    return 0


def validate_experimental_ufx(files, manifest):
    """Accept only the recorded runtime cohort; kernel and UCM stay native."""
    record = json.loads(Path(manifest).read_text(encoding='utf-8'))
    profile = record.get('profile')
    if profile not in ('experimental-ufx-overlay-v1', 'experimental-ufx-overlay-v2'):
        raise ValueError('Bilinmeyen UFX deneme profili')
    versions = [VALIDATOR['arm64_build'](p.read_bytes()) for p in files[:-1]]
    split = 3 if profile.endswith('-v1') else 2
    if any(version != versions[0] for version in versions[1:split]):
        raise ValueError('Deneme profili yerli runtime/cekirdek farkina izin vermez')
    if any(version != versions[3] for version in versions[split:]) or versions[3][:2] != versions[0][:2]:
        raise ValueError('Kaynak runtime grubu ayni major/minor/build olmali')
    if list(versions[0]) != record['target_build'] or list(versions[3]) != record['source_build']:
        raise ValueError('UFX manifest build bilgisi dosyalarla eslesmiyor')
    for path, file in zip(REQUIRED, files):
        if hashlib.sha256(file.read_bytes()).hexdigest() != record['files'][path]:
            raise ValueError(f'UFX manifest hash farki: {path}')


def check(wim, index, emit, out_inf=None, run=subprocess.run, evidence_inf=None,
          experimental_ufx_manifest=None):
    env = dict(os.environ, WIMLIB_IMAGEX_IGNORE_CASE='1', LC_ALL='C')

    def command(*args):
        return run(['wimlib-imagex', *args], env=env, capture_output=True, text=True)

    emit(f'USB runtime on kontrolu: {wim} (indeks {index})')
    info = command('info', str(wim), str(index))
    if info.returncode:
        emit(f'HATA: WIM bilgisi okunamadi (kod {info.returncode}): {info.stderr.strip()}')
        return 1
    emit(info.stdout.strip())
    errors = []
    with tempfile.TemporaryDirectory(prefix='b2l-usb-runtime-') as work:
        files = []
        for i, path in enumerate(REQUIRED):
            dest = Path(work) / str(i)
            dest.mkdir()
            result = command('extract', str(wim), str(index), path,
                             f'--dest-dir={dest}', '--no-acls')
            if result.returncode:
                label = 'YOK' if result.returncode == 49 else 'OKUMA HATASI'
                errors.append(path)
                emit(f'{label}: {path} (kod {result.returncode})')
                if result.returncode != 49:
                    emit(result.stderr.strip())
                continue
            matches = [p for p in dest.rglob('*') if p.is_file()
                       and p.name.lower() == Path(path).name.lower()]
            if len(matches) != 1 or not matches[0].stat().st_size:
                errors.append(path)
                emit(f'HATA: bos veya belirsiz cikarma sonucu: {path}')
                continue
            files.append(matches[0])
            emit(f'VAR: {path}')
            if i == len(REQUIRED) - 1 and evidence_inf is not None:
                raw_inf = matches[0].read_bytes()
                Path(evidence_inf).write_bytes(raw_inf)
                emit(f'Ham inbox INF kaydedildi: {evidence_inf}')
                emit('INF SHA256: ' + hashlib.sha256(raw_inf).hexdigest())
            if i < len(REQUIRED) - 1:
                try:
                    version = VALIDATOR['arm64_build'](matches[0].read_bytes())
                    emit('  PE: ARM64 ' + '.'.join(str(part) for part in version))
                except ValueError as error:
                    errors.append(path)
                    emit(f'HATA: {path}: {error}')
        if errors:
            emit(f'HATA: {len(errors)} gerekli dosya dogrulanamadi; derleme/paketleme baslatilmaz.')
            listing = command('dir', str(wim), str(index))
            if listing.returncode:
                emit(f'WIM envanteri okunamadi (kod {listing.returncode}).')
            else:
                hits = [line.strip() for line in listing.stdout.splitlines()
                        if any(name in line.lower() for name in
                               ('ufx', 'usbfunction', 'ucmcx', 'urscx'))]
                emit('Ilgili WIM yolları (yalniz envanter; kurulu bileşen kaniti degil):')
                for line in hits:
                    emit(line)
                if not hits:
                    emit('Eslesen yol yok.')
            emit('Ayni hedef Windows build\'inin ARM64 USB Function paketi ve bagimliliklari gerekli.')
            emit('SYS dosyasini tek basina kopyalamak veya WDK stub kullanmak runtime kurulumu degildir.')
            return 1
        failed = False
        try:
            if experimental_ufx_manifest is None:
                VALIDATOR['check_runtimes'](files[:-1])
            else:
                validate_experimental_ufx(files, experimental_ufx_manifest)
                emit('DENEYSEL UFX: kayitli kaynak runtime grubu kabul edildi; yukleyici/ABI uyumu kanitlanmadi.')
        except (ValueError, OSError, KeyError, TypeError) as error:
            emit(f'HATA: ARM64/build dogrulamasi: {error}')
            failed = True
        try:
            generated = VALIDATOR['make_inf'](files[-1].read_bytes())
        except (ValueError, UnicodeError, OSError) as error:
            emit(f'HATA: INF dogrulamasi: {error}')
            emit('--- Ham ufxsynopsys.inf (yalniz inceleme; kurulum yapilmaz) ---')
            raw_inf = files[-1].read_bytes()
            encoding = 'utf-16' if raw_inf.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
            for number, line in enumerate(raw_inf.decode(encoding, errors='replace').splitlines(), 1):
                emit(f'{number:04d}: {line}')
            emit('--- INF sonu ---')
            failed = True
        if failed:
            return 1
        if out_inf is not None:
            Path(out_inf).write_text(generated, encoding='ascii')
        emit('Incelenen imajin kendi ARM64 runtime/build/INF kontrolleri gecti.')
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wim', type=Path)
    parser.add_argument('--index', type=int, default=1)
    parser.add_argument('--out-inf', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--experimental-ufx-manifest', type=Path,
                        help='Yalniz hazirlanan, hash kayitli UFX deneme WIM\'ini kabul et')
    parser.add_argument('--reference-wim', type=Path,
                        help='Kaynak ISO/WIM cekirdegini hedef boot.wim indeks 1 ile karsilastir')
    args = parser.parse_args()
    if args.index < 1:
        parser.error('--index en az 1 olmali')
    if args.experimental_ufx_manifest and (args.reference_wim or args.wim.suffix.lower() == '.iso'):
        parser.error('UFX deneme manifesti yalniz hazirlanan hedef WIM icindir')
    if args.reference_wim is not None and args.out_inf is not None:
        parser.error('Kaynak incelemesinde --out-inf kullanilamaz; INF hedef WIM\'den uretilir')
    if args.wim.suffix.lower() == '.iso' and args.out_inf is not None:
        parser.error('ISO incelemesinde --out-inf kullanilamaz; INF hedef WIM\'den uretilir')
    lines = []
    comparison = None

    def emit(line):
        lines.append(line)
        print(line)

    try:
        emit('USB runtime denetleyici v2')
        evidence_inf = None
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            evidence_inf = args.report.with_suffix('.ufxsynopsys.inf')
            # A missing INF in this run must not leave last run's evidence.
            evidence_inf.unlink(missing_ok=True)
        with tempfile.TemporaryDirectory(prefix='b2l-iso-audit-') as work:
            emit(f'Girdi: {args.wim}')
            image = resolve_image(args.wim, Path(work), emit)
            if args.reference_wim is not None:
                comparison = compare_kernels(image, args.index, args.reference_wim, emit)
            extra = ({'experimental_ufx_manifest': args.experimental_ufx_manifest}
                     if args.experimental_ufx_manifest else {})
            result = check(image, args.index, emit, args.out_inf, evidence_inf=evidence_inf, **extra)
            result = max(result, comparison or 0)
    except (OSError, ValueError) as error:
        emit(f'HATA: {error}')
        result = 1
    if comparison == 1:
        emit('HEDEF ESLESMESI: BASARISIZ; kaynak ve hedef PE build farkli.')
    if result:
        emit('GENEL SONUC: BASARISIZ (cikis kodu 1). Paketleme icin on kontrol gecmedi.')
    else:
        emit('GENEL SONUC: BASARILI (cikis kodu 0). Yalniz dosya/build/INF kontrolleri gecti.')
    emit('Bu kontrol runtime kurmaz; servis/bagimlilik ve cihaz dogrulamasi ayridir.')
    if args.report is not None:
        args.report.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    raise SystemExit(main())
