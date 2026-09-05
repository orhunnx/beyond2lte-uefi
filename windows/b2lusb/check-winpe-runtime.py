#!/usr/bin/env python3
"""Read-only WIM preflight; never substitute DriverStore files for installed ones."""
import argparse
import os
from pathlib import Path
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


def check(wim, index, emit, out_inf=None, run=subprocess.run):
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
        try:
            VALIDATOR['check_runtimes'](files[:-1])
            generated = VALIDATOR['make_inf'](files[-1].read_bytes())
        except (ValueError, UnicodeError, OSError) as error:
            emit(f'HATA: ARM64/build/INF dogrulamasi: {error}')
            return 1
        if out_inf is not None:
            Path(out_inf).write_text(generated, encoding='ascii')
        emit('ARM64 runtime build ve function INF kontrolleri gecti; servis/bagimlilik ve cihaz testi ayridir.')
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wim', type=Path)
    parser.add_argument('--index', type=int, default=1)
    parser.add_argument('--out-inf', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.index < 1:
        parser.error('--index en az 1 olmali')
    lines = []

    def emit(line):
        lines.append(line)
        print(line)

    try:
        result = check(args.wim, args.index, emit, args.out_inf)
    except OSError as error:
        emit(f'HATA: {error}')
        result = 1
    if args.report is not None:
        args.report.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    raise SystemExit(main())
