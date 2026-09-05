#!/usr/bin/env python3
"""Exercise the real WSL staging helper with an inspecting Inf2Cat stand-in."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = ('b2lusb.inf', 'b2lusb-host.inf', 'b2lusb-function.inf', 'b2lmax77705.inf',
           'b2lusb.sys', 'b2lusbmon.sys', 'b2lmax77705.sys')

with tempfile.TemporaryDirectory(prefix='b2l catalog ') as temp:
    work = Path(temp)
    out = work/'output with spaces'
    out.mkdir()
    tools = work/'tools'
    tools.mkdir()
    wslpath = tools/'wslpath'
    wslpath.write_text('#!/bin/sh\n[ "$1" = "-w" ] || exit 2\nprintf "%s\\n" "$2"\n')
    wslpath.chmod(0o755)
    inf2cat = tools/'fake Inf2Cat'
    inf2cat.write_text('''#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys
assert sys.argv[1].startswith('/driver:')
assert sys.argv[2:] == ['/os:10_GE_ARM64']
stage = pathlib.Path(sys.argv[1][8:])
expected = json.loads(os.environ['EXPECTED_PAYLOAD'])
assert sorted(p.name for p in stage.iterdir()) == sorted(expected)
assert {p.name: p.read_text() for p in stage.iterdir()} == expected
pathlib.Path(os.environ['CAT_STAGE_LOG']).write_text(str(stage))
if os.environ['CAT_MODE'] == 'fail':
    (stage/'b2lusb.cat').write_bytes(b'partial catalog')
    sys.exit(17)
if os.environ['CAT_MODE'] == 'empty':
    sys.exit(0)
(stage/'b2lusb.cat').write_bytes(b'new catalog')
''')
    inf2cat.chmod(0o755)
    payload = {name: f'current signed payload: {name}' for name in PAYLOAD}
    for name, content in payload.items():
        (out/name).write_text(content)
    # Reproduce both reported names, plus nested and unrelated INF contamination.
    evidence = (out/'25h2-usb-runtime-check.ufxsynopsys.inf',
                out/'winpe-usb-runtime-check.ufxsynopsys.inf',
                out/'old-diagnostics'/'extra.inf')
    for file in evidence:
        file.parent.mkdir(exist_ok=True)
        file.write_bytes(b'original Microsoft evidence, no CatalogFile')
    (out/'b2lusbctl.exe').write_bytes(b'separately signed EXE')
    log = work/'stage.txt'
    env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'],
               EXPECTED_PAYLOAD=json.dumps(payload), CAT_STAGE_LOG=str(log))
    for mode in ('ok', 'fail', 'empty', 'missing'):
        (out/'b2lusb.cat').write_bytes(b'stale catalog')
        log.unlink(missing_ok=True)
        if mode == 'missing':
            (out/'b2lusb.sys').unlink()
        run = subprocess.run(['bash', str(ROOT/'windows/b2lusb/build-catalog.sh'),
                              str(out), str(inf2cat)], env=dict(env, CAT_MODE=mode),
                             capture_output=True, text=True)
        assert (run.returncode == 0) == (mode == 'ok'), run.stderr
        if mode == 'ok':
            assert (out/'b2lusb.cat').read_bytes() == b'new catalog'
        else:
            assert not (out/'b2lusb.cat').exists()
        if mode == 'fail':
            assert run.returncode == 17
        if mode == 'missing':
            assert not log.exists()  # do not call Inf2Cat with an incomplete payload
        assert not list(out.glob('.inf2cat.*'))
        for file in evidence:
            assert file.read_bytes() == b'original Microsoft evidence, no CatalogFile'
        for name, content in payload.items():
            if not (mode == 'missing' and name == 'b2lusb.sys'):
                assert (out/name).read_text() == content

script = (ROOT/'tools_b2lusb.sh').read_text()
assert 'bash "$SRC/build-catalog.sh" "$OUT" "$INF2CAT"' in script
assert '/driver:"$(wslpath -w "$OUT")"' not in script
assert script.index('build-catalog.sh') < script.index('"$SIGNTOOL" sign')
print('Catalog staging: exact payload, preserved evidence, tool failure, missing output/input, cleanup: PASS')
