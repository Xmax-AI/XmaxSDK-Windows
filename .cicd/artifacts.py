"""SDK binary/ABI audit, relocatable packaging and checksum verification. Standard library only."""
from __future__ import annotations
import argparse
import ctypes
import _ctypes
import hashlib
import json
import os
import posixpath
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = 'Xmax-AI/XmaxSDK-Windows'

def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def public_exports(header: Path) -> set[str]:
    return set(re.findall(r'XMAX_CALL\s+(xmax_\w+)\s*\(', header.read_text(encoding='utf-8')))

def pe_metadata(path: Path) -> tuple[set[str], list[str]]:
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('Expected a Windows PE binary')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664:
        raise ValueError('Expected Windows x64 PE')
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected a PE32+ binary')
    count = struct.unpack_from('<H', data, pe+6)[0]
    sections = optional + struct.unpack_from('<H', data, pe+20)[0]
    ranges = [struct.unpack_from('<IIII', data, sections+40*i+8) for i in range(count)]
    def offset(rva: int) -> int:
        for virtual_size, address, size, start in ranges:
            if address <= rva < address + max(virtual_size, size):
                result = start + rva - address
                if result >= len(data):
                    break
                return result
        raise ValueError('Invalid PE address')
    def string(rva: int) -> str:
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')
    export_rva, _ = struct.unpack_from('<II', data, optional+112)
    exports: set[str] = set()
    if export_rva:
        table = offset(export_rva)
        length = struct.unpack_from('<I', data, table+24)[0]
        names = offset(struct.unpack_from('<I', data, table+32)[0])
        for i in range(length):
            exports.add(string(struct.unpack_from('<I', data, names+4*i)[0]))
    import_rva, import_size = struct.unpack_from('<II', data, optional+120)
    imports: list[str] = []
    if import_rva:
        table = offset(import_rva)
        for i in range(min(import_size//20, 4096)):
            fields = struct.unpack_from('<IIIII', data, table+20*i)
            if not any(fields):
                break
            imports.append(string(fields[3]))
    return exports, imports

def audit(binary: Path, root: Path = ROOT, runtime: bool = True, rtc: bool | None = None) -> dict:
    expected = public_exports(root/'include/xmax/xmax.h')
    baseline = json.loads((root/'.cicd/abi-v1.json').read_text(encoding='utf-8'))
    if not set(baseline['exports']).issubset(expected):
        raise ValueError('An ABI v1 export was removed')
    if '#define XMAX_ABI_VERSION 1u' not in (root/'include/xmax/xmax.h').read_text(encoding='utf-8'):
        raise ValueError('Review and explicitly migrate the ABI baseline before changing ABI version')
    dependencies: list[str] = []
    if binary.suffix.lower() == '.dll':
        exports, dependencies = pe_metadata(binary)
    else:
        text = subprocess.check_output(['nm', '-gU', str(binary)], text=True)
        exports = {line.split()[-1][1:] for line in text.splitlines() if line.split()}
    if exports != expected:
        raise ValueError(f'Export mismatch: missing={sorted(expected-exports)}, extra={sorted(exports-expected)}')
    if any(name.lower().startswith('qt') for name in dependencies):
        raise ValueError('SDK depends on Qt')
    result = {'abi_version': 1, 'exports': sorted(exports), 'imports': dependencies,
              'sha256': sha256(binary), 'runtime_checked': False}
    if runtime:
        directory = os.add_dll_directory(str(binary.parent.resolve())) if os.name == 'nt' else None
        library = None
        try:
            library = ctypes.CDLL(str(binary.resolve()))
            library.xmax_version.restype = ctypes.c_char_p
            version = library.xmax_version().decode('utf-8')
            if version != (root/'VERSION').read_text(encoding='utf-8').strip():
                raise ValueError('Runtime SDK version differs from VERSION')
            class Config(ctypes.Structure):
                _pack_ = 8
                _fields_ = [('size', ctypes.c_uint32), ('abi', ctypes.c_uint32), ('key', ctypes.c_char_p),
                            ('model', ctypes.c_char_p), ('timeout', ctypes.c_uint32)]
            class Capabilities(ctypes.Structure):
                _pack_ = 8
                _fields_ = [('size', ctypes.c_uint32), ('abi', ctypes.c_uint32), ('flags', ctypes.c_uint64)]
            library.xmax_client_create.argtypes = [ctypes.POINTER(Config), ctypes.POINTER(ctypes.c_void_p)]
            library.xmax_client_create.restype = ctypes.c_int32
            library.xmax_client_destroy.argtypes = [ctypes.c_void_p]
            library.xmax_client_destroy.restype = None
            library.xmax_client_get_capabilities.argtypes = [ctypes.c_void_p, ctypes.POINTER(Capabilities)]
            library.xmax_client_get_capabilities.restype = ctypes.c_int32
            config = Config(ctypes.sizeof(Config), 1, b'', None, 15000)
            client = ctypes.c_void_p()
            if library.xmax_client_create(ctypes.byref(config), ctypes.byref(client)) != 0:
                raise ValueError('Installed DLL client creation failed')
            try:
                caps = Capabilities(ctypes.sizeof(Capabilities), 0, 0)
                if library.xmax_client_get_capabilities(client, ctypes.byref(caps)) != 0 or caps.abi != 1:
                    raise ValueError('Installed DLL capability query failed')
                if rtc is not None and bool(caps.flags & 1) != rtc:
                    raise ValueError('Packaged RTC capability differs from requested flavor')
                result.update(version=version, capabilities=caps.flags, runtime_checked=True)
            finally:
                library.xmax_client_destroy(client)
        finally:
            # ctypes keeps native DLLs loaded; explicitly unload after destroying all SDK objects.
            # Windows cannot remove the fresh staging directory while its DLL is mapped.
            if library is not None and library._handle:
                if os.name == 'nt': _ctypes.FreeLibrary(library._handle)
                else: _ctypes.dlclose(library._handle)
                library._handle = 0
            if directory:
                directory.close()
    return result

def write_manifest(directory: Path, metadata: dict) -> dict:
    entries = {p.relative_to(directory).as_posix(): sha256(p)
               for p in sorted(directory.rglob('*')) if p.is_file() and p.relative_to(directory).as_posix() != 'manifest.json'}
    record = dict(metadata, files=entries)
    (directory/'manifest.json').write_text(json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return record

def verify_manifest(directory: Path) -> dict:
    record = json.loads((directory/'manifest.json').read_text(encoding='utf-8'))
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*')
              if p.is_file() and p.relative_to(directory).as_posix() != 'manifest.json'}
    if actual != set(record['files']):
        raise ValueError('Package file inventory differs from manifest')
    for relative, digest in record['files'].items():
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts or sha256(directory/path) != digest:
            raise ValueError(f'Package checksum mismatch: {relative}')
    return record

def archive(directory: Path, destination: Path) -> None:
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                info = zipfile.ZipInfo(f'{directory.name}/{path.relative_to(directory).as_posix()}', (1980,1,1,0,0,0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                output.writestr(info, path.read_bytes())
    with zipfile.ZipFile(destination) as output:
        if output.testzip():
            raise ValueError('Corrupt ZIP')

def extract(archive_path: Path, destination: Path) -> Path:
    with zipfile.ZipFile(archive_path) as archive_file:
        for entry in archive_file.infolist():
            path = Path(entry.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or ':' in entry.filename:
                raise ValueError('Unsafe ZIP entry')
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Symlink in ZIP')
        if archive_file.testzip():
            raise ValueError('Corrupt ZIP')
        archive_file.extractall(destination)
    directories = list(destination.iterdir())
    if len(directories) != 1 or not directories[0].is_dir():
        raise ValueError('Expected exactly one package root')
    verify_manifest(directories[0])
    return directories[0]

def stage_documents(stage: Path, root: Path = ROOT) -> None:
    for name in ['LICENSE', 'THIRD_PARTY.md']:
        shutil.copy2(root/name, stage/name)
    materials = stage/'Materials'
    materials.mkdir(exist_ok=True)
    for name in ['installation.md', 'usage.md']:
        shutil.copy2(root/'Materials'/name, materials/name)
    # Source-relative links point to the exact commit when the linked source is not shipped in the binary package.
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    for document in materials.glob('*.md'):
        def link(match):
            target = match[1]
            if re.match(r'https?:|#', target) or (document.parent/target.split('#')[0]).exists():
                return match[0]
            relative = posixpath.normpath('Materials/'+target)
            return ']('+f'https://github.com/{REPOSITORY}/blob/{revision}/'+relative+')'
        document.write_text(re.sub(r'\]\(([^)]+)\)', link, document.read_text(encoding='utf-8')), encoding='utf-8')
    licenses = stage/'licenses' 
    licenses.mkdir(exist_ok=True)
    shutil.copy2(root/'third_party/nlohmann/LICENSE.MIT', licenses/'nlohmann-json-MIT.txt')
    sample = stage/'samples/consumer'
    sample.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root/'.cicd/consumer/CMakeLists.txt', sample/'CMakeLists.txt')
    for name in ['c_consumer.c','cpp_consumer.cpp']:
        shutil.copy2(root/'Tests/XmaxSDKTests'/name, sample/name)
    shutil.copytree(root/'Examples/DotNetSmoke', stage/'samples/DotNetSmoke',
                    ignore=shutil.ignore_patterns('bin','obj'), dirs_exist_ok=True)
    (stage/'README.txt').write_text('XmaxSDK Windows '+(root/'VERSION').read_text().strip()+
        '\nMSVC x64. SDK requires the Microsoft Visual C++ v14 x64 Redistributable.\n'
        'Start with Materials/installation.md and Materials/usage.md. Qt is not needed by the SDK.\n'
        'bin/: SDK and optional Agora DLLs; lib/: import library and CMake package; include/: public ABI.\n'
        'Build samples/consumer against this installation to verify your host.\n', encoding='utf-8')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--no-runtime', action='store_true')
    parser.add_argument('--rtc', choices=['on','off'])
    args = parser.parse_args()
    print(json.dumps(audit(args.binary, runtime=not args.no_runtime,
                           rtc=None if args.rtc is None else args.rtc=='on'), indent=2))
