#!/usr/bin/env python3
"""Exercise the real WSL staging helper with an inspecting Inf2Cat stand-in."""
import json
import os
from pathlib import Path
import subprocess
import shutil
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
    native = work/'Windows Temp Çığ'
    native.mkdir()
    windows_temp = 'C:\\Users\\Orhun Çığ\\AppData\\Local\\Temp\\'
    powershell = tools/'powershell.exe'
    powershell.write_text('''#!/usr/bin/env python3
import os, sys
assert sys.argv[1:4] == ['-NoProfile', '-NonInteractive', '-Command']
assert len(sys.argv) == 5 and '[System.IO.Path]::GetTempPath()' in sys.argv[4]
if os.environ['CAT_MODE'] == 'temp-fail':
    sys.exit(19)
if os.environ['CAT_MODE'] == 'temp-unc':
    print('//server/temp', end='\\r\\n')
else:
    print(os.environ['FAKE_WINDOWS_TEMP'], end='\\r\\n')
''')
    powershell.chmod(0o755)
    copy = tools/'cp'
    copy.write_text('''#!/usr/bin/env python3
import os, pathlib, subprocess, sys
source, destination = map(pathlib.Path, sys.argv[1:])
result = subprocess.run([os.environ['REAL_CP'], *sys.argv[1:]])
if result.returncode:
    sys.exit(result.returncode)
if ((os.environ['CAT_MODE'] == 'input-corrupt' and source.name == 'b2lusb.sys') or
    (os.environ['CAT_MODE'] == 'catalog-corrupt' and source.name == 'b2lusb.cat')):
    destination.write_bytes(b'corrupted copy')
if os.environ['CAT_MODE'] == 'catalog-copy-fail' and source.name == 'b2lusb.cat':
    sys.exit(23)
''')
    copy.chmod(0o755)
    wslpath = tools/'wslpath'
    wslpath.write_text('''#!/usr/bin/env python3
import os, pathlib, sys
native = pathlib.Path(os.environ['FAKE_NATIVE_TEMP']).resolve()
if sys.argv[1] == '-u':
    assert sys.argv[2] == os.environ['FAKE_WINDOWS_TEMP']
    print(str(native))
elif sys.argv[1] == '-w':
    stage = pathlib.Path(sys.argv[2]).resolve()
    if stage.parent != native or os.environ['CAT_MODE'] == 'stage-unc':
        print('//wsl.localhost/Ubuntu/home/repo/stage')
    else:
        print(os.environ['FAKE_WINDOWS_TEMP'] + stage.name)
else:
    sys.exit(2)
''')
    wslpath.chmod(0o755)
    inf2cat = tools/'fake Inf2Cat'
    inf2cat.write_text('''#!/usr/bin/env python3
import hashlib, json, os, pathlib, sys
assert sys.argv[1].startswith('/driver:')
assert sys.argv[2:] == ['/os:10_GE_ARM64']
win_stage = pathlib.PureWindowsPath(sys.argv[1][8:])
assert win_stage.drive == 'C:'  # fail if the real helper gives Inf2Cat a WSL UNC path
assert win_stage.parent == pathlib.PureWindowsPath(os.environ['FAKE_WINDOWS_TEMP'])
stage = pathlib.Path(os.environ['FAKE_NATIVE_TEMP']) / win_stage.name
assert pathlib.Path.cwd().resolve() == stage.resolve()
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
               EXPECTED_PAYLOAD=json.dumps(payload), CAT_STAGE_LOG=str(log),
               FAKE_NATIVE_TEMP=str(native), FAKE_WINDOWS_TEMP=windows_temp,
               REAL_CP=shutil.which('cp'))
    for mode in ('ok', 'fail', 'empty', 'temp-fail', 'temp-unc', 'stage-unc',
                 'input-corrupt', 'catalog-corrupt', 'catalog-copy-fail', 'missing'):
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
        if mode == 'temp-fail':
            assert run.returncode == 19
        if mode == 'catalog-copy-fail':
            assert run.returncode == 23
        if mode in ('missing', 'temp-fail', 'temp-unc', 'stage-unc', 'input-corrupt'):
            assert not log.exists()  # do not call Inf2Cat with an incomplete payload
        assert not list(out.glob('.inf2cat.*'))
        assert not list(out.glob('.b2lusb.cat.*'))
        assert not list(native.iterdir())
        for file in evidence:
            assert file.read_bytes() == b'original Microsoft evidence, no CatalogFile'
        for name, content in payload.items():
            if not (mode == 'missing' and name == 'b2lusb.sys'):
                assert (out/name).read_text() == content

script = (ROOT/'tools_b2lusb.sh').read_text()
assert 'bash "$SRC/build-catalog.sh" "$OUT" "$INF2CAT"' in script
assert '/driver:"$(wslpath -w "$OUT")"' not in script
assert script.index('build-catalog.sh') < script.index('"$SIGNTOOL" sign')
print('Catalog staging: local Windows drive/cwd, exact payload, Unicode paths, UNC rejection, failures, cleanup: PASS')
