#!/usr/bin/env python3
"""Stage the data-free builder payload and pack it into the native builder (.dmg on macOS, .exe on Windows)."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
from common import sha, write_json


def run(*args):
    return subprocess.check_output([str(a) for a in args],text=True)


def patch_macho(root):
    binaries=[]
    for p in root.rglob('*'):
        if p.is_file() and 'Mach-O' in run('file',p):binaries.append(p)
    libraries={p.name:p for p in binaries if p.suffix=='.dylib'}
    for binary in binaries:
        if binary.suffix=='.dylib':
            run('install_name_tool','-id','@rpath/'+binary.name,binary)
        for line in run('otool','-L',binary).splitlines()[1:]:
            dep=line.strip().split(' (')[0]
            if Path(dep).name in libraries and not dep.startswith(('/usr/lib/','/System/')):
                destination=binary.parent/Path(dep).name
                if not destination.is_file():
                    destination=libraries[Path(dep).name]
                relative=os.path.relpath(destination,binary.parent)
                run('install_name_tool','-change',dep,'@loader_path/'+relative,binary)
        for path in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',run('otool','-l',binary)):
            run('install_name_tool','-delete_rpath',path,binary)
        run('codesign','--force','--sign','-',binary)
        for dep in run('otool','-L',binary).splitlines()[1:]:
            path=dep.strip().split(' (')[0]
            if path.startswith('/') and not path.startswith(('/usr/lib/','/System/')):
                raise ValueError('Unbundled Mach-O dependency')


def stage(platform,build):
    payload=ROOT/'build'/('payload-'+platform)
    if payload.exists():shutil.rmtree(payload)
    payload.mkdir(parents=True)
    windows=platform=='windows'
    exe='.exe' if windows else ''
    shutil.copy2(build/('hl-tool'+exe),payload/('hl-tool'+exe))
    game=payload/'game'
    resources=game/'Resources' if windows else game/'Contents/Resources'
    resources.mkdir(parents=True)
    shutil.copy2(ROOT/'LICENSE',resources/'LICENSE')
    shutil.copy2(ROOT/'THIRD_PARTY_NOTICES.md',resources/'THIRD_PARTY_NOTICES.md')
    notices=resources/'notices';notices.mkdir()
    for path in (ROOT/'build/notices').glob('*'):shutil.copy2(path,notices/path.name)
    for name,path in [('SDL3','SDL3-3.4.20/LICENSE.txt'),('json','json/LICENSE.MIT'),('stb','stb/LICENSE'),('dr_libs','dr_libs/LICENSE'),('DejaVu','dejavu-fonts/LICENSE')]:
        shutil.copy2(ROOT/'tools/vendor'/path,notices/(name+'.txt'))
    for target in [payload/'tools/audio'/platform,resources/'tools/audio'/platform]:
        shutil.copytree(ROOT/'tools/audio'/platform,target)
    if windows:
        shutil.copy2(build/'packaging/hl-launch.exe',game/'HorkeLeto.exe')
        shutil.copy2(build/'tests/hl-tests.exe',payload/'hl-tests.exe')
        library=build/'tools/vendor/SDL3-3.4.20/SDL3.dll'
        toolchain=Path(os.environ.get('HL_MINGW', str(ROOT/'tools/vendor/llvm-mingw')))
        runtime=toolchain/'x86_64-w64-mingw32/bin'
        shutil.copy2(toolchain/'LICENSE.TXT',notices/'LLVM-runtime.txt')
        # Static import closure is also validated by check.py before the zip is made.
        for folder in [payload,game]:
            shutil.copy2(library,folder/'SDL3.dll')
            for name in ['libc++.dll','libunwind.dll']:shutil.copy2(runtime/name,folder/name)
    else:
        (game/'Contents/MacOS').mkdir()
        (game/'Contents/Frameworks').mkdir()
        shutil.copy2(build/'packaging/hl-launch',game/'Contents/MacOS/HorkeLeto')
        library=build/'tools/vendor/SDL3-3.4.20/libSDL3.0.dylib'
        shutil.copy2(library,game/'Contents/Frameworks'/library.name)
        shutil.copy2(library,payload/library.name)
        shutil.copy2(ROOT/'packaging/macos/Info.plist',game/'Contents/Info.plist')
        patch_macho(payload)
        run('codesign','--force','--sign','-',game)
    shutil.copy2(ROOT/'build/downloads'/('upscaler-'+platform+'.zip'),payload/'upscaler.zip')
    write_json(payload/'payload-manifest.json',{'files':[
        {'path':str(p.relative_to(payload)),'sha256':sha(p)} for p in sorted(payload.rglob('*')) if p.is_file()]})
    return payload


def sanitize_native_build_paths(target):
    # Fixed-width substitutions remove build-machine home paths without changing offsets.
    replacements=[(bytes([47,85,115,101,114,115,47]),b'/build/'),
                  (bytes([67,58,92,85,115,101,114,115,92]),bytes([67,58,92,98,117,105,108,100,92]))]
    replacements += [(a.decode().encode('utf-16le'),b.decode().encode('utf-16le')) for a,b in list(replacements)]
    changed=[]
    files=[target] if target.is_file() else sorted(target.rglob('*'))
    for path in files:
        if not path.is_file() or path.is_symlink():continue
        with path.open('rb') as stream:header=stream.read(4)
        if header not in [bytes.fromhex(v) for v in ['cffaedfe','cefaedfe','feedfacf','feedface','cafebabe','cafebabf']] and header[:2]!=b'MZ':continue
        data=path.read_bytes();clean=data
        for before,after in replacements:clean=clean.replace(before,after)
        if clean!=data:path.write_bytes(clean);changed.append(path)
    return changed


def zip_payload(payload,archive):
    """Deterministic ZIP: sorted names, fixed timestamps, executable bits kept."""
    skip={'hl-tests.exe','payload-manifest.json'}
    files=[p for p in sorted(payload.rglob('*')) if p.is_file() and p.name not in skip and 'work' not in p.relative_to(payload).parts[:1]]
    write_json(payload/'payload-manifest.json',{'files':[{'path':p.relative_to(payload).as_posix(),'sha256':sha(p)} for p in files]})
    files.append(payload/'payload-manifest.json')
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as bundle:
        for path in files:
            entry=zipfile.ZipInfo(path.relative_to(payload).as_posix(),(2026,1,1,0,0,0))
            entry.create_system=3
            entry.external_attr=(0o100755 if os.access(path,os.X_OK) else 0o100644)<<16
            entry.compress_type=zipfile.ZIP_DEFLATED
            bundle.writestr(entry,path.read_bytes())
    return archive


def checksum(path):
    Path(str(path)+'.sha256').write_text(sha(path)+'  '+path.name+'\n')


def bundle(platform,build,payload,version):
    payload=payload.resolve()
    dist=ROOT/'dist';dist.mkdir(exist_ok=True)
    changed=sanitize_native_build_paths(payload)
    if platform=='macos':
        for path in changed:run('codesign','--force','--sign','-',path)
        if (payload/'game').exists():run('codesign','--force','--deep','--sign','-',payload/'game')
    archive=zip_payload(payload,ROOT/'build'/('payload-'+platform+'.zip'))
    if platform=='macos':
        source=build/'builder/Horke Leto Builder.app'
        app=dist/'Horke Leto Builder.app'
        if app.exists():shutil.rmtree(app)
        shutil.copytree(source,app,symlinks=True)
        stray=app/'Contents/MacOS/payload.zip'
        if stray.exists():stray.unlink()
        shutil.copy2(archive,app/'Contents/Resources/payload.zip')
        plist=app/'Contents/Info.plist'
        for key,value in [('CFBundleShortVersionString',version),('CFBundleVersion',version)]:
            run('plutil','-replace',key,'-string',value,plist)
        sanitize_native_build_paths(app/'Contents/MacOS')
        run('codesign','--force','--deep','--sign','-',app)
        run('codesign','--verify','--deep','--strict',app)
        image=dist/'Horke-Leto-Builder-macOS.dmg'
        if image.exists():image.unlink()
        settings=ROOT/'packaging/macos/dmg/settings.py'
        subprocess.run([sys.executable,'-m','dmgbuild','-s',str(settings),'-D','app='+str(app),'-D','assets='+str(settings.parent),
                        'Horke Leto Builder '+version,str(image)],check=True)
        checksum(image)
        return image
    exe=dist/'Horke-Leto-Builder-Windows.exe'
    shutil.copy2(build/'builder/Horke Leto Builder.exe',exe)
    sanitize_native_build_paths(exe)
    data=archive.read_bytes()
    with exe.open('ab') as stream:
        stream.write(data)
        stream.write(len(data).to_bytes(8,'little'))
        stream.write(b'HLPAYLD1')
    checksum(exe)
    return exe


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--platform',choices=['macos','windows'],required=True)
    p.add_argument('--build',type=Path)
    p.add_argument('--payload',type=Path,help='Use an already staged payload directory')
    p.add_argument('--bundle',action='store_true',help='Also make the release .dmg or .exe in dist/')
    p.add_argument('--version',default='0.0.0')
    a=p.parse_args()
    build=(a.build or ROOT/'build'/(a.platform+'-release')).resolve()
    payload=a.payload or stage(a.platform,build)
    if a.bundle:print(bundle(a.platform,build,payload,a.version.removeprefix('v')))
