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
%dev%=Synopsys,,URS\\PNP0CA1&FUNCTION
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

# Full preflight accepts the observed generic+Qualcomm source INF without
# removing the separate 26100/28000 runtime build check.
with tempfile.TemporaryDirectory() as temp:
    wim = Wim()
    for path in REQUIRED[:-1]:
        wim.files[path] = pe(26100)
    wim.files[REQUIRED[-1]] = (ROOT/'tests/fixtures/ufxsynopsys-26100.inf').read_bytes()
    output = Path(temp)/'generated.inf'
    logs = []
    assert m['check']('source.wim', 1, logs.append, output, wim.run) == 0
    assert 'Needs=ufxsynopsys.install.nt.services' in output.read_text()
    assert 'qualcomm' not in output.read_text().lower()
    output.unlink()
    wim.files[REQUIRED[0]] = pe(28000)
    assert m['check']('mixed.wim', 1, logs.append, output, wim.run) == 1
    assert not output.exists()
print('Observed Synopsys/Qualcomm INF preflight and unchanged build gate: PASS')

# A source-local success must never hide failed target matching in the final
# console/report summary or process result.
import contextlib
import io
import sys
from unittest.mock import patch
with tempfile.TemporaryDirectory() as temp:
    report = Path(temp)/'report.txt'
    wim = Wim()
    actual_check = m['check']
    for comparison in (0, 1):
        console = io.StringIO()
        with patch.dict(m['main'].__globals__, {
            'compare_kernels': lambda *args: comparison,
            'check': lambda *args, **kwargs: actual_check(*args, run=wim.run, **kwargs),
        }), patch.object(sys, 'argv', ['check', 'source.wim', '--reference-wim',
                                      'boot.wim', '--report', str(report)]), contextlib.redirect_stdout(console):
            result = m['main']()
        assert result == comparison
        summary = 'GENEL SONUC: ' + ('BASARISIZ' if comparison else 'BASARILI')
        assert summary in console.getvalue() and summary in report.read_text()
        assert ('HEDEF ESLESMESI: BASARISIZ' in report.read_text()) == bool(comparison)
print('Source-local success versus overall target comparison summary: PASS')

# Only an explicit, hash-bound UFX pair may differ from the target. The source
# comparison remains strict; arbitrary UCM/URS/architecture changes are rejected.
import hashlib
import json
with tempfile.TemporaryDirectory() as temp:
    manifest = Path(temp)/'experimental.json'
    for bad in (None, 'hash', 'ucm', 'urs', 'pair', 'machine', 'kernel', 'profile'):
        wim = Wim()
        for path in REQUIRED[3:5]:
            wim.files[path] = pe(26100)
        record = {'profile': 'experimental-ufx-overlay-v1', 'target_build': [10, 0, 28000],
                  'source_build': [10, 0, 26100],
                  'files': {p: hashlib.sha256(b).hexdigest() for p, b in wim.files.items()}}
        if bad == 'hash':
            record['files'][REQUIRED[5]] = '0'*64
        elif bad in ('ucm', 'urs', 'pair', 'machine', 'kernel'):
            i = {'ucm': 1, 'urs': 2, 'pair': 4, 'machine': 3, 'kernel': 0}[bad]
            wim.files[REQUIRED[i]] = pe(22631, 0x8664 if bad == 'machine' else 0xaa64)
            record['files'][REQUIRED[i]] = hashlib.sha256(wim.files[REQUIRED[i]]).hexdigest()
        elif bad == 'profile':
            record['profile'] = 'anything-goes'
        manifest.write_text(json.dumps(record))
        assert m['check']('mixed.wim', 1, lambda _: None, run=wim.run,
                          experimental_ufx_manifest=manifest) == (1 if bad else 0), bad
        if bad is None:
            assert m['check']('mixed.wim', 1, lambda _: None, run=wim.run) == 1
print('Explicit UFX exception: exact hashes, pair, target UCM/URS and architecture: PASS')

# Observed WSL: native kernel/UCM/Synopsys=28000, URS=26100, UFX absent.
# v2 takes the entire URS/UFX/Synopsys cohort from one recorded source.
with tempfile.TemporaryDirectory() as temp:
    manifest = Path(temp)/'v2.json'
    wim = Wim()
    wim.files[REQUIRED[2]] = pe(26100)
    del wim.files[REQUIRED[3]]
    logs = []
    assert m['check']('observed.wim', 1, logs.append, run=wim.run) == 1
    assert any('YOK:' in line and 'Ufx01000.sys' in line for line in logs)
    for bad in (None, 'native-synopsys', 'urs', 'ucm', 'hash', 'machine'):
        wim = Wim()
        for path in REQUIRED[2:5]:
            wim.files[path] = pe(26100)
        if bad == 'native-synopsys':
            wim.files[REQUIRED[4]] = pe(28000)
        elif bad == 'urs':
            wim.files[REQUIRED[2]] = pe(22631)
        elif bad == 'ucm':
            wim.files[REQUIRED[1]] = pe(26100)
        elif bad == 'machine':
            wim.files[REQUIRED[2]] = pe(26100, 0x8664)
        record = {'profile': 'experimental-ufx-overlay-v2', 'target_build': [10, 0, 28000],
                  'source_build': [10, 0, 26100],
                  'files': {p: hashlib.sha256(b).hexdigest() for p, b in wim.files.items()}}
        if bad == 'hash':
            record['files'][REQUIRED[2]] = '0'*64
        manifest.write_text(json.dumps(record))
        assert m['check']('v2.wim', 1, lambda _: None, run=wim.run,
                          experimental_ufx_manifest=manifest) == (1 if bad else 0), bad
print('Observed partial WIM and v2 source URS/UFX/Synopsys cohort: PASS')

# Experimental output must actually own its source SYS, preserving bytes and
# source generic service/HW settings. Strict mode keeps the inbox wrapper.
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    manifest, output = root/'runtime.json', root/'b2lusb-function.inf'
    wim = Wim()
    original = (ROOT/'tests/fixtures/ufxsynopsys-26100.inf').read_bytes()
    wim.files[REQUIRED[-1]] = original
    for path in REQUIRED[2:5]:
        wim.files[path] = pe(26100)
    record = {'profile': 'experimental-ufx-overlay-v2', 'target_build': [10, 0, 28000],
              'source_build': [10, 0, 26100],
              'files': {p: hashlib.sha256(b).hexdigest() for p, b in wim.files.items()}}
    manifest.write_text(json.dumps(record))
    assert m['check']('source.wim', 1, print, output, wim.run,
                      experimental_ufx_manifest=manifest) == 0
    generated = output.read_text().lower()
    assert 'include=ufxsynopsys.inf' not in generated
    assert 'qcusbfnssfilter' not in generated
    assert 'copyfiles = ufxsynopsys.copyfiles' in generated
    assert 'dependencies    = ufx01000' in generated
    assert 'include=pci.inf' in generated and 'needs=pcid3coldsupported.hw' in generated
    assert (root/'ufxsynopsys.sys').read_bytes() == wim.files[REQUIRED[4]]
    assert wim.files[REQUIRED[-1]] == original
    for broken in (original.replace(b'Dependencies    = ufx01000', b'Dependencies = foreign'),
                   original.replace(b'CopyFiles = ufxsynopsys.CopyFiles', b'CopyFiles = missing'),
                   original.replace(b'Include=pci.inf', b'Include=foreign.inf'),
                   original.replace(b'StartType       = 3', b'StartType = 3\nStartType = 0')):
        try:
            m['VALIDATOR']['make_packaged_inf'](broken)
            raise AssertionError('unverified source layout accepted')
        except ValueError:
            pass
print('Source-owned generic function INF and byte-identical SYS packaging: PASS')
