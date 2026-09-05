#!/usr/bin/env python3
"""Test actual findstr replacement; UCRT BOM decoding is a host shim.

The shim only decodes Unicode input, as documented for _wfopen_s(ccs=UNICODE).
Line matching, bounds, file-mode selection, errors and output are real code.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'windows/b2lusb/b2lusbctl.c').read_text()

def function(name):
    start=source.rfind('\nstatic ',0,source.index(name+'('))+1
    brace=source.index('{',source.index(name+'(',start)); depth,end=1,brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]

harness=r'''
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
typedef wchar_t WCHAR;
typedef unsigned long ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define VOID void
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))
static int _wfopen_s(FILE **file,const WCHAR *path,const WCHAR *mode) {
    char name[1024];
    assert(wcstombs(name,path,sizeof(name)) < sizeof(name)-16);
    if(wcsstr(mode,L"ccs=UNICODE")) strcat(name,".decoded");
    *file=fopen(name,wcscmp(mode,L"rb")==0?"rb":"r");
    return *file?0:2;
}
'''
harness+=function('B2lLogLineMatches')+'\n'+function('B2lFilterLog')
harness+=r'''
int main(int argc,char **argv) {
    WCHAR path[1024], *patterns[]={L"BYN0003",L"status:",L"MAX_"};
    assert(argc==2 && setlocale(LC_ALL,"")!=NULL);
    assert(mbstowcs(path,argv[1],ARRAYSIZE(path))<ARRAYSIZE(path));
    return B2lFilterLog(path,3,patterns);
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-filter-test-') as directory:
    temp=Path(directory); (temp/'test.c').write_text(harness)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror',str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    def run(name,payload,decoded=None):
        path=temp/name
        if payload is not None: path.write_bytes(payload)
        if decoded is not None: Path(str(path)+'.decoded').write_text(decoded,encoding='utf-8')
        return subprocess.run([str(temp/'test'),str(path)],capture_output=True,text=True)
    value='unrelated\nACPI\\byn0003\\0\nStatus: Started\nMAX_STAGE=70'
    r=run('ascii',value.encode()); assert r.returncode==0 and r.stdout=='ACPI\\byn0003\\0\nStatus: Started\nMAX_STAGE=70\n'
    for name,encoded in [('utf8',b'\xef\xbb\xbf'+value.encode()),('utf16',b'\xff\xfe'+value.encode('utf-16le'))]:
        r=run(name,encoded,value); assert r.returncode==0 and 'Status: Started' in r.stdout and 'unrelated' not in r.stdout
    r=run('utf16be',b'\xfe\xff'+value.encode('utf-16be')); assert r.returncode==2 and 'UNSUPPORTED_UTF16BE' in r.stdout
    r=run('none',b'no devices\n'); assert r.returncode==0 and 'NO_MATCH' in r.stdout
    r=run('empty',b''); assert r.returncode==0 and 'NO_MATCH' in r.stdout
    r=run('missing',None); assert r.returncode==2 and 'OPEN_ERROR' in r.stdout
    r=run('long',b'x'*16384+b'Status: Started\n'); assert r.returncode==2 and 'LINE_TOO_LONG' in r.stdout
    r=run('metacharacters',b'MAX_PATH=!thing! & (device) %PATH%\n'); assert r.returncode==0 and '!thing! & (device) %PATH%' in r.stdout
# USB diagnostics are disabled in the disk-boot profile and must not leak into
# WinPE startup. The standalone tool remains tested above.
startnet=(ROOT/'windows/startnet.cmd').read_text()
assert 'findstr ' not in startnet.lower()
assert 'b2lusbctl.exe' not in startnet.lower()
assert 'b2lusb' not in startnet.lower()
print('B2LUSB log filter tests: PASS (UCRT Unicode decoding still needs WSL build)')
