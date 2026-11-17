"""Check a staged builder for game data and unresolved native dependencies."""
import argparse
from pathlib import Path
import subprocess
import sys
import json

SYSTEM=set('advapi32 bcrypt cfgmgr32 comctl32 comdlg32 crypt32 d3d11 d3d12 d3dcompiler_47 dbghelp dwmapi dxgi gdi32 hid imm32 iphlpapi kernel32 ktmw32 mf mfplat mfuuid msvcrt ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi ucrtbase user32 userenv usp10 version winmm winspool wintrust ws2_32 wtsapi32'.split())


def check(payload,platform,readobj=None):
    if any(p.suffix.lower() in {'.flc','.wav','.cue','.grp','.adb','.chr','.flac'} for p in payload.rglob('*')):
        raise ValueError('Game data in public builder')
    for p in payload.rglob('*.bin'):
        if p.parent.name!='models' or p.name!='realesr-animevideov3-x3.bin':
            raise ValueError('Unexpected binary data')
    if platform=='windows':
        executable=payload/'game/HorkeLeto.exe'
        if not all(value in executable.read_bytes() for value in [b'PerMonitorV2',b'UTF-8',b'asInvoker']):
            raise ValueError('Missing Windows UTF-8/DPI manifest')
        for p in payload.rglob('*'):
            if p.suffix.lower() not in {'.dll','.exe'}:continue
            output=subprocess.check_output([str(readobj),'--file-headers','--coff-imports',str(p)],text=True)
            if 'IMAGE_FILE_MACHINE_AMD64' not in output:raise ValueError('Non-x64 executable')
            import re
            for name in re.findall(r'^  Name: (.+)$',output,re.M):
                if name.lower().startswith(('api-ms-win-','ext-ms-win-')) or name.lower().removesuffix('.dll') in SYSTEM:continue
                if not any((folder/name).is_file() for folder in [p.parent,payload,payload/'game']):
                    raise ValueError('Unresolved dependency '+name)
    print(json.dumps({'platform':platform,'passed':True,'runtime_tested':False}))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('payload',type=Path);p.add_argument('--platform',choices=['macos','windows'],required=True);p.add_argument('--readobj',type=Path);a=p.parse_args();check(a.payload,a.platform,a.readobj)
