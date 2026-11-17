#!/usr/bin/env python3
"""Fetch pinned public build inputs, without touching global installations."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
from common import download, extract_zip


def unpack_tar(archive, target):
    with tarfile.open(archive) as bundle:
        members=bundle.getmembers()
        for entry in members:
            if entry.issym() or entry.islnk():
                # Official toolchains use safe in-tree symlinks, handled by data_filter.
                pass
            if Path(entry.name).is_absolute() or '..' in Path(entry.name).parts:
                raise ValueError('Unsafe source archive path')
        bundle.extractall(target, filter='data')


def fetch(platform, toolchain=False):
    pins=json.loads((ROOT/'dependencies.lock.json').read_text())
    build=ROOT/'build/downloads';build.mkdir(parents=True,exist_ok=True)
    vendor=ROOT/'tools/vendor';vendor.mkdir(parents=True,exist_ok=True)
    for dep in pins['dependencies']:
        target=vendor/dep['name']
        if target.exists():continue
        archive=download(dep['url'],build/(dep['name']+'.tar.gz'),dep['sha256'])
        with tempfile.TemporaryDirectory(dir=build) as tmp:
            unpack_tar(archive,tmp)
            roots=list(Path(tmp).iterdir())
            if len(roots)!=1 or not roots[0].is_dir():raise ValueError('Unexpected source archive layout')
            shutil.copytree(roots[0],target,symlinks=True)
    if toolchain:
        host='macos-universal' if sys.platform=='darwin' else 'ubuntu-22.04-x86_64'
        dep=pins['tools'][host]
        archive=download(dep['url'],build/'llvm-mingw.tar.xz',dep['sha256'])
        target=vendor/'llvm-mingw'
        if not target.exists():
            with tempfile.TemporaryDirectory(dir=build) as tmp:
                unpack_tar(archive,tmp)
                shutil.copytree(next(Path(tmp).iterdir()),target,symlinks=True)
    model_pins=json.loads((ROOT/'builder/pins.json').read_text())
    dep=model_pins['upscalers'][platform]
    archive=download(dep['url'],build/('upscaler-'+platform+'.zip'),dep['sha256'])
    upscaler=ROOT/'build'/('upscaler-'+platform)
    if upscaler.exists():shutil.rmtree(upscaler)
    extract_zip(archive,upscaler)
    if platform=='macos':(upscaler/'realesrgan-ncnn-vulkan').chmod(0o755)
    audio=ROOT/'tools/audio'/platform
    audio.mkdir(parents=True,exist_ok=True)
    if platform=='windows':
        dep=pins['flac']['windows']
        archive=download(dep['url'],build/'flac.zip',dep['sha256'])
        with tempfile.TemporaryDirectory(dir=build) as tmp:
            extract_zip(archive,Path(tmp))
            for name in ['flac.exe','libFLAC.dll']:
                choices=[p for p in Path(tmp).rglob(name) if 'win64' in str(p).lower() or 'x64' in str(p).lower()]
                if len(choices)!=1:raise ValueError('Unexpected FLAC x64 layout')
                shutil.copyfile(choices[0],audio/name)
    else:
        dep=pins['flac']['source']
        archive=download(dep['url'],build/'flac.tar.xz',dep['sha256'])
        source=ROOT/'build/flac-source'
        if not source.exists():
            with tempfile.TemporaryDirectory(dir=build) as tmp:
                unpack_tar(archive,tmp)
                shutil.copytree(next(Path(tmp).iterdir()),source)
        flac_build=ROOT/'build/flac-build'
        subprocess.run(['cmake','-S',str(source),'-B',str(flac_build),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES=arm64',
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0','-DBUILD_SHARED_LIBS=OFF',
            '-DWITH_OGG=OFF','-DCMAKE_DISABLE_FIND_PACKAGE_Intl=TRUE','-DBUILD_TESTING=OFF','-DBUILD_CXXLIBS=OFF',
            '-DBUILD_EXAMPLES=OFF','-DBUILD_DOCS=OFF'],check=True)
        subprocess.run(['cmake','--build',str(flac_build),'--target','flac','-j','3'],check=True)
        for previous in audio.iterdir():
            if previous.is_file():previous.unlink()
        shutil.copy2(flac_build/'src/flac/flac',audio/'flac')
    licenses=ROOT/'build/notices';licenses.mkdir(exist_ok=True)
    for row in pins['licenses']:
        download(row['url'],licenses/row['name'],row['sha256'])
    return upscaler

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--platform',choices=['macos','windows'],required=True);p.add_argument('--toolchain',action='store_true');a=p.parse_args();fetch(a.platform,a.toolchain)
