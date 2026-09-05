#!/usr/bin/env python3
"""Exercise runtime preflight with scripted WIM reads, never an actual WIM edit."""
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
m = runpy.run_path(str(ROOT / 'windows/b2lusb/check-winpe-runtime.py'))
REQUIRED = m['REQUIRED']
INF = b'''[Version]
Class=USBFunctionController
ClassGuid={88BAE032-5A81-49F0-BC3D-A4FF138216D6}
[Manufacturer]
%ms%=Models,NTarm64
[Models.NTarm64]
%dev%=Synopsys,ACPI\\TEST0001
[Synopsys.NT]
CopyFiles=Core
[Synopsys.NT.Services]
AddService=ufxsynopsys,2,Service
'''


def pe(build=28000, machine=0xaa64):
    raw = bytearray(512)
    raw[:2] = b'MZ'
    struct.pack_into('<I', raw, 0x3c, 64)
    raw[64:68] = b'PE\0\0'
    struct.pack_into('<HH', raw, 68, machine, 1)
    raw[88:96] = b'.rsrc\0\0\0'
    struct.pack_into('<II', raw, 104, 128, 256)
    struct.pack_into('<IIII', raw, 256, 0xfeef04bd, 0x10000, 10 << 16, build << 16)
    return bytes(raw)


class Wim:
    def __init__(self):
        self.files = {path: pe() for path in REQUIRED[:-1]}
        self.files[REQUIRED[-1]] = INF
        self.attempts = []
        self.error = 0

    def run(self, args, **kwargs):
        assert kwargs['env']['WIMLIB_IMAGEX_IGNORE_CASE'] == '1'
        op = args[1]
        assert op in ('info', 'extract', 'dir')  # no servicing/mutation command
        if op == 'info':
            return subprocess.CompletedProcess(args, 0, 'Build: 28000\nArchitecture: ARM64', '')
        if op == 'dir':
            return subprocess.CompletedProcess(args, 0,
                '/Windows/WinSxS/arm64_usbfunction_22631/ufx01000.sys', '')
        path = args[4]
        self.attempts.append(path)
        if self.error or path not in self.files:
            return subprocess.CompletedProcess(args, self.error or 49, '', 'extract failed')
        dest = Path(args[5].split('=', 1)[1])
        (dest / Path(path).name.lower()).write_bytes(self.files[path])
        return subprocess.CompletedProcess(args, 0, '', '')


with tempfile.TemporaryDirectory() as temp:
    output = Path(temp) / 'function.inf'
    wim = Wim()
    logs = []
    assert m['check']('test.wim', 1, logs.append, output, wim.run) == 0
    assert 'Needs=synopsys.nt.services' in output.read_text()
    assert wim.attempts == list(REQUIRED)
    for kind in ('missing', 'io', 'foreign-build', 'x64', 'bad-inf'):
        wim = Wim()
        output.unlink(missing_ok=True)
        logs = []
        if kind == 'missing':
            for path in REQUIRED[3:]:
                del wim.files[path]
        elif kind == 'io':
            wim.error = 2
        elif kind == 'foreign-build':
            wim.files[REQUIRED[3]] = pe(22631)
        elif kind == 'x64':
            wim.files[REQUIRED[3]] = pe(machine=0x8664)
        else:
            wim.files[REQUIRED[-1]] = INF.replace(b'NTarm64', b'NTamd64')
        assert m['check']('test.wim', 1, logs.append, output, wim.run) == 1, kind
        assert not output.exists() and wim.attempts == list(REQUIRED)
        if kind == 'missing':
            assert sum(line.startswith('YOK:') for line in logs) == 3
            assert any('PE: ARM64 10.0.28000' in line for line in logs)
            assert any('WinSxS' in line for line in logs)  # inventory is not accepted as installed
        if kind == 'io':
            assert not any(line.startswith('YOK:') for line in logs)
            assert sum(line.startswith('OKUMA HATASI:') for line in logs) == 6

script = (ROOT / 'tools_b2lusb.sh').read_text()
assert script.index('check-winpe-runtime.py') < script.index('powershell.exe -NoProfile')
assert script.index('check-winpe-runtime.py') < script.index('osslsigncode sign')
print('WinPE runtime preflight: missing files, I/O errors, ARM64/build/INF gates: PASS')

with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)

    def iso_run(args, **kwargs):
        assert args[:2] in (['7z', 'l'], ['7z', 'e'])
        if args[1] == 'l':
            return subprocess.CompletedProcess(args, 0, 'Path = sources\\install.esd\n', '')
        assert args[4] == 'sources\\install.esd'
        (Path(args[2][2:]) / 'install.esd').write_bytes(b'fixture')
        return subprocess.CompletedProcess(args, 0, '', '')

    image = m['resolve_image'](Path('Windows 25H2 ARM64.iso'), work, lambda _: None, iso_run)
    assert image == work / 'install.esd'
    assert m['resolve_image'](Path('install.wim'), work, lambda _: None) == Path('install.wim')

    def compare_run(args, **kwargs):
        assert args[1] == 'extract' and args[4] == REQUIRED[0]
        dest = Path(args[5].split('=', 1)[1])
        (dest / 'ntoskrnl.exe').write_bytes(pe(22631 if args[2] == 'old.wim' else 28000))
        return subprocess.CompletedProcess(args, 0, '', '')

    logs = []
    assert m['compare_kernels']('old.wim', 1, 'boot.wim', logs.append, compare_run) == 1
    assert any('UYUMSUZ' in line for line in logs)
    assert m['compare_kernels']('matching.wim', 1, 'boot.wim', logs.append, compare_run) == 0
print('ISO input and source/target kernel comparison: PASS')

# Keep the exact inbox bytes when INF parsing fails, and print readable evidence.
with tempfile.TemporaryDirectory() as temp:
    wim = Wim()
    raw = INF.decode().replace('NTarm64', 'NTamd64').encode('utf-16')
    wim.files[REQUIRED[-1]] = raw
    evidence = Path(temp) / 'inbox.inf'
    output = Path(temp) / 'generated.inf'
    logs = []
    assert m['check']('test.wim', 1, logs.append, output, wim.run, evidence) == 1
    assert evidence.read_bytes() == raw and not output.exists()
    assert any('INF SHA256:' in line for line in logs)
    assert any('no ARM64 DDInstall' in line for line in logs)
    assert any('0001: [Version]' in line for line in logs)
print('Raw inbox INF evidence and precise model errors: PASS')
