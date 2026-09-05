#!/usr/bin/env python3
"""Bind the URS function child using the *target WIM's* inbox installation.
Reject ambiguous/foreign-architecture models instead of guessing DDInstall names.
"""
import re
import struct
from pathlib import Path


def make_inf(raw: bytes) -> str:
    text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    sections = {}
    current = None
    for line in text.splitlines():
        line = line.split(';', 1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].lower()
            sections.setdefault(current, [])
        elif line and current:
            sections[current].append(line)
    models = []
    for line in sections.get('manufacturer', []):
        _, sep, rhs = line.partition('=')
        if not sep:
            continue
        parts = [p.strip() for p in rhs.split(',')]
        for arch in parts[1:]:
            if arch.lower().startswith('ntarm64'):
                models += sections.get((parts[0] + '.' + arch).lower(), [])
    installs = {line.split('=', 1)[1].split(',', 1)[0].strip().lower()
                for line in models if '=' in line}
    if not installs:
        raise ValueError('ufxsynopsys.inf: no ARM64 DDInstall resolved; '
                         f'Manufacturer={sections.get("manufacturer", [])!r}; '
                         f'sections={list(sections)!r}')
    if len(installs) > 1:
        raise ValueError('ufxsynopsys.inf: multiple ARM64 DDInstall targets: '
                         + ', '.join(sorted(installs)))
    install = installs.pop()
    choices = [name for name in sections if name == install.lower() + '.nt'
               or name == install.lower() + '.ntarm64']
    if len(choices) != 1:
        raise ValueError('ufxsynopsys.inf: ambiguous or missing decorated DDInstall')
    base = choices[0]
    if base + '.services' not in sections:
        raise ValueError('ufxsynopsys.inf: missing Services section')
    values = {}
    for line in sections.get('version', []):
        key, sep, value = line.partition('=')
        if sep:
            values[key.strip().lower()] = value.strip()
    cls, guid = values.get('class', ''), values.get('classguid', '')
    if not re.fullmatch(r'[a-zA-Z][a-zA-Z0-9]*', cls) or not re.fullmatch(r'\{[0-9a-fA-F-]{36}\}', guid):
        raise ValueError('ufxsynopsys.inf: invalid Class/ClassGuid')
    result = f'''; Generated from this boot.wim's ARM64 ufxsynopsys.inf. No inbox INF edits.
[Version]
Signature="$WINDOWS NT$"
Class={cls}
ClassGuid={guid}
Provider=%B2L%
CatalogFile=b2lusb.cat
DriverVer=09/05/2026,0.1.0.21
PnpLockdown=1
[Manufacturer]
%B2L%=B2L,NTarm64
[B2L.NTarm64]
%B2L.Function%=B2LFunction,URS\\BYN0002&FUNCTION
[B2LFunction.NT]
Include=ufxsynopsys.inf
Needs={base}
[B2LFunction.NT.Services]
Include=ufxsynopsys.inf
Needs={base}.services
'''
    if base + '.hw' in sections:
        result += f'[B2LFunction.NT.HW]\nInclude=ufxsynopsys.inf\nNeeds={base}.hw\n'
    result += '[Strings]\nB2L="beyond2lte"\nB2L.Function="Beyond2LTE USB Function Controller"\n'
    return result

def arm64_build(raw: bytes) -> tuple[int, int, int]:
    if raw[:2] != b'MZ' or len(raw) < 64:
        raise ValueError('Runtime is not PE')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if pe + 24 > len(raw) or raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE header')
    machine, count = struct.unpack_from('<HH', raw, pe+4)
    if machine != 0xaa64:
        raise ValueError('Runtime is not ARM64')
    optional = struct.unpack_from('<H', raw, pe+20)[0]
    table = pe + 24 + optional
    builds = []
    for i in range(count):
        section = table + 40*i
        if section + 40 > len(raw):
            raise ValueError('Truncated section table')
        if raw[section:section+8].rstrip(b'\0') != b'.rsrc':
            continue
        size, offset = struct.unpack_from('<II', raw, section+16)
        if offset + size > len(raw):
            raise ValueError('Truncated resource section')
        resource = raw[offset:offset+size]
        # VS_FIXEDFILEINFO signature + structure version. Reject ambiguous data.
        signature = struct.pack('<II', 0xfeef04bd, 0x00010000)
        start = 0
        while (pos := resource.find(signature, start)) >= 0:
            if pos + 52 > len(resource):
                raise ValueError('Truncated version information')
            ms, ls = struct.unpack_from('<II', resource, pos+8)
            builds.append((ms >> 16, ms & 0xffff, ls >> 16))
            start = pos + 52
    if len(set(builds)) != 1:
        raise ValueError('Missing or ambiguous file version')
    return builds[0]


def check_runtimes(paths: list[Path]) -> None:
    kernel_build = arm64_build(paths[0].read_bytes())
    for path in paths[1:]:
        if arm64_build(path.read_bytes()) != kernel_build:
            raise ValueError(f'{path.name}: runtime build differs from target ntoskrnl')


if __name__ == '__main__':
    import sys
    if sys.argv[1] == '--runtimes':
        check_runtimes([Path(p) for p in sys.argv[2:]])
    else:
        Path(sys.argv[2]).write_text(make_inf(Path(sys.argv[1]).read_bytes()), encoding='ascii')
