#!/usr/bin/env python3
"""Host tests for UFX preparation, including rollback after failed read-back.

External WIM/hive commands are scripted here; real WSL servicing is separate.
"""
from pathlib import Path
import runpy
import shlex
import struct
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
m = runpy.run_path(str(ROOT/'windows/b2lusb/prepare-experimental-ufx.py'))
prefix = m['PREFIX']
remap = m['remap_service']
reg = (f'Windows Registry Editor Version 5.00\n\n[{prefix}\\ControlSet002\\Services\\Ufx01000]\n'
       '"Literal"="ControlSet002"\n'
       f'[{prefix}\\ControlSet002\\Services\\Ufx01000\\Parameters]\n"Class"=dword:00000001\n'
       f'[{prefix}\\ControlSet002\\Services\\Ufx01000\\Enum]\n"0"="source-instance"\n')
mapped = remap(reg, 'ControlSet002', 'ControlSet001', 'Ufx01000')
assert '\\ControlSet001\\Services\\Ufx01000\\Parameters]' in mapped
assert '"Literal"="ControlSet002"' in mapped
assert 'source-instance' not in mapped
try:
    remap(reg + '\n[HKEY_LOCAL_MACHINE\\SYSTEM\\Select]\n', 'ControlSet002', 'ControlSet001', 'Ufx01000')
    raise AssertionError('unrelated key accepted')
except ValueError:
    pass


def import_pe():
    raw = bytearray(1024)
    struct.pack_into('<I', raw, 0x3c, 64)
    raw[64:68] = b'PE\0\0'
    struct.pack_into('<HH', raw, 68, 0xaa64, 1)
    struct.pack_into('<H', raw, 84, 240)
    struct.pack_into('<H', raw, 88, 0x20b)
    struct.pack_into('<II', raw, 208, 0x1000, 40)
    struct.pack_into('<III', raw, 340, 0x1000, 512, 512)
    struct.pack_into('<IIIII', raw, 512, 0x1100, 0, 0, 0x1080, 0x1100)
    raw[640:653] = b'ntoskrnl.exe\0'
    return raw


assert m['imports'](import_pe()) == {'ntoskrnl.exe'}
for kind in ('x64', 'truncated', 'rva', 'delay'):
    raw = import_pe()
    if kind == 'x64':
        struct.pack_into('<H', raw, 68, 0x8664)
    elif kind == 'truncated':
        raw = raw[:40]
    elif kind == 'rva':
        struct.pack_into('<I', raw, 524, 0x8000)
    else:
        struct.pack_into('<II', raw, 304, 0x1000, 32)
    try:
        m['imports'](raw)
        raise AssertionError(kind)
    except ValueError:
        pass
print('UFX service relocation and bounded ARM64 import parsing: PASS')

# Script a complete preparation. Fail during candidate read-back, then verify
# that the original WIM and any earlier rollback image remain byte-identical.
for fail_readback in (True, False):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp).resolve()
        source, target = root/'install.wim', root/'boot.wim'
        source.write_bytes(b'source-image')
        target.write_bytes(b'original-target')
        backup = target.with_name(target.name + '.before-experimental-ufx')
        backup.write_bytes(b'previous-rollback')
        store_path = '/Windows/System32/DriverStore/FileRepository/ufxsynopsys.inf_arm64_test'
        files = {p: ('target-' + p).encode() for p in m['REQUIRED'][:3]}
        src_files = {p: ('source-' + p).encode() for p in m['REQUIRED'][3:]}
        payload = {}
        service_data = {}

        def fake_extract(image, index, path, work, optional=False):
            if path == m['MARKER'] and Path(image) == target:
                return None
            if Path(image) == target and path in src_files:
                return None
            if Path(image) == target and path.startswith(store_path):
                return None
            if Path(image).name == 'boot.wim' and Path(image) != target:
                if fail_readback:
                    raise ValueError('injected read-back failure')
                content = payload[path]
            elif path == m['SYSTEM']:
                content = b'source-hive' if Path(image) == source else b'target-hive'
            elif path == store_path:
                directory = Path(tempfile.mkdtemp(dir=work))/Path(path).name
                directory.mkdir()
                for p in m['REQUIRED'][4:]:
                    (directory/Path(p).name).write_bytes(src_files[p])
                return directory
            elif path == store_path + '/ufxsynopsys.inf':
                content = src_files[m['REQUIRED'][5]]
            else:
                content = src_files[path] if Path(image) == source else files[path]
            dest = Path(tempfile.mkdtemp(dir=work))/Path(path).name
            dest.write_bytes(content)
            return dest

        def service_reg(service, cs):
            return (f'Windows Registry Editor Version 5.00\n\n[{prefix}\\{cs}\\Services\\{service}]\n'
                    '"Type"=dword:00000001\n')

        def fake_command(args, **kwargs):
            if args[:2] == ['wimlib-imagex', 'dir']:
                return store_path + '/ufxsynopsys.inf\n'
            if args[:2] == ['wimlib-imagex', 'update']:
                for line in kwargs['input'].splitlines():
                    _, file, path = shlex.split(line)
                    payload[path] = Path(file).read_bytes()
                Path(args[2]).write_bytes(b'updated-target')
                return ''
            if args[0] == 'hivexget':
                return args[2].split('\\')[-1] + '.sys' if args[3] == 'ImagePath' else '1'
            if args[0] == 'hivexregedit':
                if '--merge' in args:
                    service_data[Path(args[-1]).stem.lower()] = Path(args[-1]).read_text()
                    return ''
                if args[-1].endswith('\\Services'):
                    return ''
                service = args[-1].split('\\')[-1]
                is_source = Path(args[-2]).read_bytes() == b'source-hive'
                return service_reg(service, 'ControlSet002') if is_source else service_data[service.lower()]
            raise AssertionError(args)

        audit = {'resolve_image': lambda image, *args: image, 'check': lambda *args, **kwargs: 0,
                 'VALIDATOR': {'check_runtimes': lambda files: None,
                               'arm64_build': lambda raw: (10, 0, 26100 if raw.startswith(b'source-') else 28000)}}
        with patch.dict(m['prepare'].__globals__, {
            'extract': fake_extract, 'command': fake_command, 'AUDIT': audit,
            'imports': lambda raw: set(),
            'control_set': lambda hive: 'ControlSet002' if hive.read_bytes() == b'source-hive' else 'ControlSet001',
        }), patch('shutil.which', return_value='/fake/tool'):
            try:
                m['prepare'](source, 1, target, root/'manifest.json', lambda _: None)
                assert not fail_readback
            except ValueError as error:
                assert fail_readback and 'injected read-back failure' in str(error), error
        assert target.read_bytes() == (b'original-target' if fail_readback else b'updated-target')
        assert backup.read_bytes() == b'previous-rollback'
        assert source.read_bytes() == b'source-image'
        assert (root/'manifest.json').exists() == (not fail_readback)
print('UFX WIM staging: publish after read-back; failed read-back preserves original and rollback: PASS')
