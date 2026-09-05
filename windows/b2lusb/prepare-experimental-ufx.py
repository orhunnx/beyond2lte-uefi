#!/usr/bin/env python3
"""Explicit WSL-only UFX overlay experiment; not a CBS package installation.

Keep the target kernel/UCM. Copy an unmodified source URS/UFX/Synopsys cohort, its
original INF/DriverStore directory and service subtrees. Work on a WIM copy;
retain a rollback image and publish only after read-back verification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import struct
import subprocess
import tempfile

AUDIT = runpy.run_path(str(Path(__file__).with_name('check-winpe-runtime.py')))
REQUIRED = AUDIT['REQUIRED']
SYSTEM = '/Windows/System32/config/SYSTEM'
MARKER = '/Windows/B2LUSB/experimental-ufx.json'
SERVICES = ('UrsCx01000', 'Ufx01000', 'ufxsynopsys')
PROFILE = 'experimental-ufx-overlay-v2'
PREFIX = 'HKEY_LOCAL_MACHINE\\SYSTEM'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def command(args, **kwargs):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True,
                            env=dict(os.environ, WIMLIB_IMAGEX_IGNORE_CASE='1', LC_ALL='C'),
                            **kwargs)
    if result.returncode:
        raise ValueError(f'{args[0]} basarisiz ({result.returncode}): {result.stderr.strip()}')
    return result.stdout


def extract(image, index, path, work, optional=False):
    dest = Path(tempfile.mkdtemp(dir=work))
    args = ['wimlib-imagex', 'extract', str(image), str(index), path,
            f'--dest-dir={dest}', '--no-acls']
    result = subprocess.run(args, capture_output=True, text=True,
                            env=dict(os.environ, WIMLIB_IMAGEX_IGNORE_CASE='1', LC_ALL='C'))
    if optional and result.returncode == 49:
        return None
    if result.returncode:
        raise ValueError(f'{path}: WIM okuma hatasi {result.returncode}: {result.stderr.strip()}')
    matches = [p for p in dest.rglob('*') if p.name.lower() == Path(path).name.lower()]
    if len(matches) != 1:
        raise ValueError(f'{path}: belirsiz cikarma sonucu')
    return matches[0]


def control_set(hive):
    number = int(command(['hivexget', hive, 'Select', 'Current']).strip(), 0)
    if not 1 <= number <= 999:
        raise ValueError('Gecersiz SYSTEM Select/Current')
    return f'ControlSet{number:03d}'


def remap_service(raw, source_cs, target_cs, service):
    """Relocate only key headers; never rewrite source registry value data."""
    root = f'{PREFIX}\\{source_cs}\\Services\\{service}'
    found = False
    output = []
    skip = False
    for line in raw.splitlines():
        if line.startswith('['):
            if not line.endswith(']'):
                raise ValueError('Bozuk servis REG basligi')
            key = line[1:-1]
            if key.lower() != root.lower() and not key.lower().startswith(root.lower() + '\\'):
                raise ValueError('Servis disinda REG anahtari')
            # Device instance enumeration belongs to the source machine, not this phone.
            suffix = key[len(root):]
            skip = suffix.lower() == '\\enum' or suffix.lower().startswith('\\enum\\')
            if not skip:
                found = found or not suffix
                line = f'[{PREFIX}\\{target_cs}\\Services\\{service}{suffix}]'
        if not skip:
            output.append(line)
    if not found:
        raise ValueError(f'Kaynak servis REG bos: {service}')
    return '\n'.join(output) + '\n'


def imports(raw):
    """Read PE32+ normal import DLL names; reject unsupported delay imports.

    File availability is checked below. This is not a symbol/ABI or WDF class
    version compatibility test; the Windows loader remains authoritative.
    """
    try:
        pe = struct.unpack_from('<I', raw, 0x3c)[0]
        if raw[pe:pe + 4] != b'PE\0\0':
            raise ValueError('PE imzasi yok')
        machine, count = struct.unpack_from('<HH', raw, pe + 4)
        size = struct.unpack_from('<H', raw, pe + 20)[0]
        opt = pe + 24
        if machine != 0xaa64 or struct.unpack_from('<H', raw, opt)[0] != 0x20b or size < 240:
            raise ValueError('ARM64 PE32+ import tablosu gerekli')
        def offset(rva):
            for n in range(count):
                base = opt + size + 40 * n
                va = struct.unpack_from('<I', raw, base + 12)[0]
                length, ptr = struct.unpack_from('<II', raw, base + 16)
                if va <= rva < va + length and ptr + rva - va < len(raw):
                    return ptr + rva - va
            raise ValueError('PE RVA dosya disinda')
        delay, delay_size = struct.unpack_from('<II', raw, opt + 112 + 13 * 8)
        if delay or delay_size:
            raise ValueError('Delay-import bagimliligi otomatik aktarimda desteklenmiyor')
        rva, length = struct.unpack_from('<II', raw, opt + 120)
        names = set()
        if not rva:
            return names
        terminated = False
        for n in range(length // 20):
            descriptor = struct.unpack_from('<IIIII', raw, offset(rva + 20 * n))
            if not any(descriptor):
                terminated = True
                break
            pos = offset(descriptor[3])
            name = raw[pos:raw.index(b'\0', pos)].decode('ascii').lower()
            if not re.fullmatch(r'[a-z0-9_.-]+\.(sys|dll|exe)', name):
                raise ValueError(f'Beklenmeyen import modulu: {name}')
            names.add(name)
        if not terminated:
            raise ValueError('Sonlandirilmamis import tablosu')
        return names
    except (struct.error, IndexError, UnicodeError) as error:
        raise ValueError(f'Bozuk PE import tablosu: {error}') from error


def quote_wim(path):
    value = str(path)
    if any(c in value for c in "'\r\n"):
        raise ValueError('WIM komut yolunda desteklenmeyen tirnak/yeni satir')
    return "'" + value + "'"


def known_servicing_drift(installed, actual_hashes):
    """Only recover the recorded native Synopsys SYS/INF, never arbitrary bytes.
    All other runtime hashes must still match the original prepared manifest.
    """
    changed = []
    if installed.get('profile') != PROFILE:
        raise ValueError('Eski profil icin otomatik servisleme kurtarmasi yok')
    for path in REQUIRED:
        actual = actual_hashes[path]
        if actual == installed['files'][path]:
            continue
        if path not in REQUIRED[4:] or actual != installed.get('replaced', {}).get(path):
            raise ValueError(f'Kayitli native Synopsys disinda runtime degisikligi: {path}')
        changed.append(path)
    if not changed:
        raise ValueError('Hash degisikligi olmayan runtime kontrol hatasi; otomatik kurtarma yok')
    return changed


def prepare(source, index, target, manifest, emit=print):
    source, target = source.resolve(), target.resolve()
    if source == target:
        raise ValueError('Kaynak ile hedef ayni olamaz')
    for tool in ('wimlib-imagex', 'hivexget', 'hivexregedit'):
        if not shutil.which(tool):
            raise ValueError(f'{tool} yok; WSL icin libhivex-bin/libwin-hivex-perl gerekli')
    record = {'profile': PROFILE, 'source': str(source),
              'source_index': index, 'source_sha256': sha(source), 'files': {},
              'overlay': {}, 'services': {}, 'import_files': {}, 'replaced': {}}
    # Same directory permits atomic publication; a failed experiment never edits target.
    with tempfile.TemporaryDirectory(prefix='.b2l-ufx-', dir=target.parent) as temp:
        work = Path(temp)
        repair_servicing = False
        installed_record = extract(target, 1, MARKER, work, optional=True)
        if installed_record is not None:
            installed = json.loads(installed_record.read_text(encoding='utf-8'))
            if installed.get('profile') not in (PROFILE, 'experimental-ufx-overlay-v1'):
                raise ValueError('Bilinmeyen mevcut runtime profili')
            if (installed.get('source_sha256') != record['source_sha256'] or
                    installed.get('source_index') != index):
                raise ValueError('Mevcut UFX denemesi baska kaynaga ait; once yedek WIM geri yuklenmeli')
            if AUDIT['check'](target, 1, emit, experimental_ufx_manifest=installed_record):
                actual = {path: sha(extract(target, 1, path, work)) for path in REQUIRED}
                changed = known_servicing_drift(installed, actual)
                repair_servicing = True
                emit('Kayitli native Synopsys geri gelmis; kaynak katmani yeniden hazirlanacak: ' + ', '.join(changed))
        if installed_record is not None and installed['profile'] == PROFILE and not repair_servicing:
            hive = extract(target, 1, SYSTEM, work)
            cs = control_set(hive)
            for service in SERVICES:
                evidence = extract(target, 1, f'/Windows/B2LUSB/runtime-source/{service}.reg', work)
                if sha(evidence) != installed['services'][service]['reg_sha256']:
                    raise ValueError(f'{service}: kaynak servis kaydi degismis')
                current = command(['hivexregedit', '--export', '--prefix', PREFIX,
                                   hive, f'{cs}\\Services\\{service}'])
                if remap_service(current, cs, cs, service) != evidence.read_text(encoding='utf-8'):
                    raise ValueError(f'{service}: hazirlanmis servis agaci degismis')
            manifest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(installed_record, manifest)
            emit('Ayni kaynak UFX denemesi zaten hazir; ISO yeniden cikarilmadi.')
            return
        image = AUDIT['resolve_image'](source, work, emit)
        if AUDIT['check'](image, index, emit):
            raise ValueError('Kaynak imajin kendi runtime/INF dogrulamasi basarisiz')
        target_files = [extract(target, 1, p, work) for p in REQUIRED[:2]]
        AUDIT['VALIDATOR']['check_runtimes'](target_files)
        source_files = [extract(image, index, p, work) for p in REQUIRED[2:]]
        overlay = dict(zip(REQUIRED[2:], source_files))
        record['target_build'] = list(AUDIT['VALIDATOR']['arm64_build'](target_files[0].read_bytes()))
        record['source_build'] = list(AUDIT['VALIDATOR']['arm64_build'](source_files[0].read_bytes()))
        if record['target_build'][:2] != record['source_build'][:2]:
            raise ValueError('Major/minor Windows surumu farkli')
        # The explicit experiment replaces the whole source cohort, including
        # native Synopsys on partially populated WinPE. Record all previous bytes.
        for path, file in overlay.items():
            existing = extract(target, 1, path, work, optional=True)
            record['replaced'][path] = sha(existing) if existing is not None else None
            emit(f'RUNTIME: {path}: {record["replaced"][path] or "YOK"} -> {sha(file)}')
        if repair_servicing:
            # Keep the original pre-overlay identity across repeated repairs.
            for path in REQUIRED[4:]:
                record['replaced'][path] = installed['replaced'].get(path)
        for path, file in zip(REQUIRED, target_files + source_files):
            record['files'][path] = sha(file)

        # The original generic inbox INF has no CatalogFile directive. Keep its
        # source store directory as payload; do not pretend it is registered by
        # copying it. DISM still installs our real platform INF in the next step.
        listing = command(['wimlib-imagex', 'dir', image, index])
        stores = [line.strip() for line in listing.splitlines() if re.fullmatch(
            r'/Windows/System32/DriverStore/FileRepository/ufxsynopsys\.inf_arm64_[^/]+/ufxsynopsys\.inf',
            line.strip(), re.I)]
        matching_stores = []
        for path in stores:
            if sha(extract(image, index, path, work)) == sha(source_files[3]):
                matching_stores.append(str(Path(path).parent))
        if len(matching_stores) != 1:
            raise ValueError('Kurulu INF ile eslesen tek kaynak ARM64 Synopsys DriverStore paketi bulunamadi')
        store_path = matching_stores[0]
        store = extract(image, index, store_path, work)
        store_files = {p.name.lower(): p for p in store.rglob('*') if p.is_file()}
        for path, file in ((REQUIRED[4], source_files[2]), (REQUIRED[5], source_files[3])):
            if Path(path).name.lower() not in store_files or sha(store_files[Path(path).name.lower()]) != sha(file):
                raise ValueError(f'Kaynak DriverStore/kurulu dosya farkli: {path}')
        for file in store.rglob('*'):
            if file.is_file():
                path = store_path + '/' + file.relative_to(store).as_posix()
                existing = extract(target, 1, path, work, optional=True)
                if existing is not None and sha(existing) != sha(file):
                    raise ValueError(f'Hedef DriverStore cakismasi: {path}')
                overlay[path] = file

        modules = {p.name.lower(): p for p in target_files + source_files[:-1]}
        for driver in source_files[:3]:
            for module in sorted(imports(driver.read_bytes())):
                if module not in modules:
                    candidates = []
                    for directory in ('/Windows/System32', '/Windows/System32/drivers'):
                        file = extract(target, 1, directory + '/' + module, work, optional=True)
                        if file:
                            candidates.append(file)
                    if len(candidates) != 1:
                        raise ValueError(f'{driver.name}: tek hedef import dosyasi bulunamadi: {module}')
                    modules[module] = candidates[0]
                # Runtime PE version parser also verifies ARM64 machine type.
                version = AUDIT['VALIDATOR']['arm64_build'](modules[module].read_bytes())
                record['import_files'][module] = {'sha256': sha(modules[module]), 'build': list(version)}
        emit('Import modulu dosyalari var; sembol/ABI/WDF baglama uyumu Windows yukleyicisinde sinanacak.')

        source_hive = extract(image, index, SYSTEM, work)
        target_hive = extract(target, 1, SYSTEM, work)
        src_cs, dst_cs = control_set(source_hive), control_set(target_hive)
        target_services = command(['hivexregedit', '--export', '--prefix', PREFIX,
                                   target_hive, f'{dst_cs}\\Services'])
        target_keys = {line[1:-1].lower() for line in target_services.splitlines()
                       if line.startswith('[') and line.endswith(']')}
        for service in SERVICES:
            key = f'{src_cs}\\Services\\{service}'
            raw = command(['hivexregedit', '--export', '--prefix', PREFIX, source_hive, key])
            for value in ('Type', 'Start', 'ErrorControl', 'ImagePath'):
                value_text = command(['hivexget', source_hive, key, value])
                if value == 'ImagePath' and service.lower() + '.sys' not in value_text.lower():
                    raise ValueError(f'Beklenmeyen servis ImagePath: {service}')
            dependencies = (command(['hivexget', source_hive, key, 'DependOnService']).splitlines()
                            if re.search(r'^"DependOnService"=', raw, re.M | re.I) else [])
            for dependency in dependencies:
                if not dependency:
                    continue
                if dependency.lower() not in [s.lower() for s in SERVICES]:
                    command(['hivexget', target_hive, f'{dst_cs}\\Services\\{dependency}', 'ImagePath'])
            if re.search(r'^"DependOnGroup"=', raw, re.M | re.I):
                groups = command(['hivexget', source_hive, key, 'DependOnGroup']).splitlines()
                target_groups = command(['hivexget', target_hive,
                                         f'{dst_cs}\\Control\\ServiceGroupOrder', 'List']).lower().splitlines()
                if any(group and group.lower() not in target_groups for group in groups):
                    raise ValueError(f'{service}: hedef servis grubu eksik')
            relocated = remap_service(raw, src_cs, dst_cs, service)
            reg = work / f'{service}.reg'
            reg.write_text(relocated, encoding='utf-8')
            # Replace only this named service subtree in the temporary hive.
            # A plain merge would leave parameters belonging to the old runtime.
            if f'{PREFIX}\\{dst_cs}\\Services\\{service}'.lower() in target_keys:
                existing = command(['hivexregedit', '--export', '--prefix', PREFIX,
                                    target_hive, f'{dst_cs}\\Services\\{service}'])
                previous = work / f'{service}-before.reg'
                previous.write_text(existing, encoding='utf-8')
                overlay[f'/Windows/B2LUSB/runtime-source/{service}-before.reg'] = previous
                record['replaced'][f'Services/{service}'] = sha(previous)
                deletion = work / f'{service}-delete.reg'
                deletion.write_text(f'Windows Registry Editor Version 5.00\n\n'
                                    f'[-{PREFIX}\\{dst_cs}\\Services\\{service}]\n\n', encoding='utf-8')
                command(['hivexregedit', '--merge', '--prefix', PREFIX, target_hive, deletion])
                emit(f'SERVIS: {service} kaynak agaciyla degistiriliyor; onceki kayit saklandi.')
            command(['hivexregedit', '--merge', '--prefix', PREFIX, target_hive, reg])
            verified = command(['hivexregedit', '--export', '--prefix', PREFIX, target_hive,
                                f'{dst_cs}\\Services\\{service}'])
            if remap_service(verified, dst_cs, dst_cs, service) != relocated:
                raise ValueError(f'{service}: servis geri okuma farkli')
            record['services'][service] = {'reg_sha256': sha(reg), 'control_set': dst_cs}
            overlay[f'/Windows/B2LUSB/runtime-source/{service}.reg'] = reg
        overlay[SYSTEM] = target_hive
        for path, file in overlay.items():
            record['overlay'][path] = sha(file)
        local_record = work / 'experimental-ufx.json'
        local_record.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        overlay[MARKER] = local_record
        candidate = work / 'boot.wim'
        shutil.copy2(target, candidate)
        script = ''.join(f'add {quote_wim(file)} {quote_wim(path)}\n' for path, file in overlay.items())
        command(['wimlib-imagex', 'update', candidate, '1'], input=script)
        for path, file in overlay.items():
            actual = extract(candidate, 1, path, work)
            if sha(actual) != sha(file):
                raise ValueError(f'WIM geri okuma hash farki: {path}')
        if AUDIT['check'](candidate, 1, emit, experimental_ufx_manifest=local_record):
            raise ValueError('Hazirlanan WIM on kontrolu gecmedi')
        # Keep the pre-experiment image exactly once; never overwrite a rollback.
        backup = target.with_name(target.name + '.before-experimental-ufx')
        if not backup.exists():
            shutil.copy2(target, backup)
        emit(f'Geri donus imaji: {backup}')
        os.replace(candidate, target)
        manifest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(local_record, manifest)
    emit('Deneysel UFX dosya/servis katmani hazir. Sonraki adim: MSVC ve DISM platform INF kurulumu.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--index', type=int, default=1)
    parser.add_argument('--target-wim', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--allow-mixed-ufx', action='store_true', required=True)
    args = parser.parse_args()
    if args.index < 1:
        parser.error('--index en az 1 olmali')
    try:
        prepare(args.source, args.index, args.target_wim, args.manifest)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'HATA: {error}')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
