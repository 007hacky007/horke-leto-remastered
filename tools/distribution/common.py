"""Checked downloads, bounded extraction and content-validated checkpoints."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import os
import stat
import ssl
import urllib.request
import zipfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=True) + '\n')
    temporary.replace(path)


def https_context():
    try:
        import certifi
    except ImportError:
        # Source dependency bootstrap can use the host store before freezing.
        return ssl.create_default_context()
    return ssl.create_default_context(cafile=certifi.where())


def download(url, target, expected, progress=print):
    target = Path(target)
    if len(expected) != 64 or any(c not in '0123456789abcdef' for c in expected):
        raise ValueError('Invalid pinned SHA-256')
    if target.is_file() and sha(target) == expected:
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + '.partial')
    # A partial transfer is retried; only fully hash-checked downloads are reused.
    with urllib.request.urlopen(url, timeout=60, context=https_context() if url.startswith('https:') else None) as source, partial.open('wb') as output:
        total = int(source.headers.get('Content-Length', 0))
        received = 0
        while chunk := source.read(1024 * 1024):
            output.write(chunk)
            received += len(chunk)
            if total: progress(f'Download {received * 100 // total}%')
    if sha(partial) != expected:
        partial.unlink()
        raise ValueError('Stažený balíček má jiný kontrolní součet; zkus to znovu')
    partial.replace(target)
    return target


def extract_zip(archive, target):
    target = Path(target)
    if target.is_symlink():
        raise ValueError('Symlink extraction root')
    target = target.resolve()
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        if sum(e.file_size for e in entries) > 4 * 1024**3:
            raise ValueError('Archive exceeds extraction limit')
        seen = set()
        for entry in entries:
            name = entry.filename
            path = PurePosixPath(name)
            if (path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name
                    or not path.parts or stat.S_ISLNK(entry.external_attr >> 16)
                    or name.casefold() in seen):
                raise ValueError('Unsafe archive member')
            seen.add(name.casefold())
            destination = target.joinpath(*path.parts)
            if any(p.is_symlink() for p in [destination, *destination.parents]):
                raise ValueError('Symlink extraction destination')
        target.mkdir(parents=True, exist_ok=True)
        bundle.extractall(target)
        for entry in entries:
            if entry.external_attr >> 16 & 0o111:
                os.chmod(target / entry.filename, 0o755)
