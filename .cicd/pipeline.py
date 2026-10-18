"""Build, install, package and consume the SDK. Publishing is deliberately separate."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
from artifacts import ROOT, archive, audit, extract, sha256, stage_documents, verify_manifest, write_manifest


def run(command: list[str | Path], *, cwd: Path = ROOT, env: dict | None = None) -> None:
    command = [str(x) for x in command]
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def tool(name: str) -> str:
    found = shutil.which(name)
    bundled = ROOT/'.build/tools/bin'/name
    if not found and bundled.exists(): return str(bundled)
    if not found: raise RuntimeError(f'Required tool is missing: {name}')
    return found


def installed_consumer(prefix: Path, build: Path, *, windows: bool, source: Path | None = None) -> None:
    cmake = tool('cmake')
    command = [cmake, '-S', str(source or ROOT/'.cicd/consumer'), '-B', str(build),
               f'-DCMAKE_PREFIX_PATH={prefix}']
    if windows: command += ['-G', 'Visual Studio 17 2022', '-A', 'x64']
    else: command += ['-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={tool("ninja")}']
    run(command)
    run([cmake, '--build', build, '--config', 'Release', '--parallel', '4'])
    run([tool('ctest'), '--test-dir', build, '-C', 'Release', '--output-on-failure'])


def verify(build: Path, qt_prefix: Path | None = None, sanitizers: bool = False) -> None:
    windows = os.name == 'nt'
    cmake = tool('cmake')
    command = [cmake, '-S', str(ROOT), '-B', str(build), '-DBUILD_TESTING=ON',
               '-DCMAKE_BUILD_TYPE=Debug', '-DXMAX_WITH_AGORA=OFF',
               f'-DXMAX_ENABLE_SANITIZERS={"ON" if sanitizers else "OFF"}',
               f'-DXMAX_BUILD_EXAMPLE={"ON" if qt_prefix else "OFF"}']
    if windows: command += ['-G', 'Visual Studio 17 2022', '-A', 'x64']
    else: command += ['-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={tool("ninja")}']
    if qt_prefix: command += [f'-DCMAKE_PREFIX_PATH={qt_prefix}']
    run(command)
    run([cmake, '--build', build, '--config', 'Debug', '--parallel', '4'])
    run([tool('ctest'), '--test-dir', build, '-C', 'Debug', '--output-on-failure'])
    if sanitizers:
        return # C/C++ test executables already load the instrumented DLL with the correct sanitizer runtime.
    prefix = build/'installed'
    run([cmake, '--install', build, '--config', 'Debug', '--prefix', prefix])
    binary = prefix/('bin/XmaxSDK.dll' if windows else 'lib/libXmaxSDK.dylib')
    audit(binary, rtc=False)
    installed_consumer(prefix, build/'installed-consumer', windows=windows)


def build_windows(build: Path, output: Path, qt_prefix: Path | None, agora_root: Path | None,
                  with_rtc: bool = True) -> None:
    if os.name != 'nt': raise RuntimeError('Release packaging requires a real Windows MSVC host')
    output.mkdir(parents=True, exist_ok=True)
    (output/'validation.json').unlink(missing_ok=True)
    (output/'SHA256SUMS.txt').unlink(missing_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip():
        raise RuntimeError('Release packaging requires a clean committed source tree')
    cmake = tool('cmake')
    if with_rtc and not agora_root:
        run(['pwsh', '-NoProfile', '-File', ROOT/'scripts/get-agora.ps1'])
        agora_root = ROOT/'.build/vendor/agora/Agora_Native_SDK_for_Windows_FULL/sdk'
    command = [cmake, '-S', str(ROOT), '-B', str(build), '-G', 'Visual Studio 17 2022', '-A', 'x64',
               '-DBUILD_TESTING=ON', f'-DXMAX_WITH_AGORA={"ON" if with_rtc else "OFF"}',
               f'-DXMAX_BUILD_EXAMPLE={"ON" if qt_prefix else "OFF"}']
    if agora_root: command += [f'-DXMAX_AGORA_ROOT={agora_root}']
    if qt_prefix: command += [f'-DCMAKE_PREFIX_PATH={qt_prefix}']
    run(command)
    run([cmake, '--build', build, '--config', 'Release', '--parallel', '4'])
    version = (ROOT/'VERSION').read_text().strip()
    output.mkdir(parents=True, exist_ok=True)
    # Use a fresh staging directory. No stale DLLs from earlier builds can enter the ZIP.
    with tempfile.TemporaryDirectory(prefix='xmax-package-', dir=build) as temporary:
        workspace = Path(temporary)
        stage = workspace/f'XmaxSDK-{version}-windows-x64'
        run([cmake, '--install', build, '--config', 'Release', '--prefix', stage])
        stage_documents(stage)
        if agora_root and with_rtc:
            notices = stage/'licenses/agora'
            notices.mkdir(parents=True)
            shutil.copy2(agora_root/'high_level_api/include/IAgoraRtcEngine.h', notices/'IAgoraRtcEngine.h')
            shutil.copy2(ROOT/'.cicd/dependencies.json', notices/'dependencies.json')
            for path in agora_root.parent.rglob('*'):
                if path.is_file() and any(word in path.name.lower() for word in ('license','notice','copyright')):
                    destination = notices/path.relative_to(agora_root.parent)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
        report = audit(stage/'bin/XmaxSDK.dll', rtc=with_rtc)
        metadata = {'version': version, 'commit': revision, 'platform': 'windows-x64', 'compiler': 'MSVC',
                    'with_agora': with_rtc, 'abi': report,
                    'dependencies': json.loads((ROOT/'.cicd/dependencies.json').read_text())}
        write_manifest(stage, metadata)
        sdk_zip = output/f'{stage.name}.zip'
        archive(stage, sdk_zip)
        unpacked = extract(sdk_zip, workspace/'unpacked')
        audit(unpacked/'bin/XmaxSDK.dll', rtc=with_rtc)
        installed_consumer(unpacked, build/'release-consumer', windows=True,
                           source=unpacked/'samples/consumer')
        run(['dotnet', 'run', '--project', unpacked/'samples/DotNetSmoke', '--configuration', 'Release',
             f'-p:XmaxNativeDir={unpacked / "bin"}'])
        # Debug CRT or Qt dependencies are disallowed for a distributable SDK.
        imports = [name.lower() for name in report['imports']]
        if any(name in ('ucrtbased.dll','vcruntime140d.dll','msvcp140d.dll') for name in imports):
            raise RuntimeError('Release DLL depends on the debug CRT')
        report_path = output/'validation.json'
        validation = {'version': version, 'commit': revision, 'windows_msvc': True,
                      'sdk_archive': sdk_zip.name, 'sdk_sha256': sha256(sdk_zip),
                      'sdk_abi': report, 'installed_c_consumer': True, 'installed_cpp_consumer': True,
                      'dotnet_consumer': True, 'ui_smoke': False, 'with_agora': with_rtc}
        if qt_prefix:
            demo = workspace/f'XLab-{version}-windows-x64'
            demo.mkdir()
            shutil.copy2(build/'Examples/XLab/Release/XLab.exe', demo/'XLab.exe')
            for dll in (stage/'bin').glob('*.dll'): shutil.copy2(dll, demo/dll.name)
            run([qt_prefix/'bin/windeployqt.exe', '--release', '--compiler-runtime',
                 '--qmldir', ROOT/'Examples/XLab/qml', demo/'XLab.exe'])
            # Offscreen is a test platform, so ensure it is present even if windeployqt selects only qwindows.
            platforms = demo/'platforms'
            platforms.mkdir(exist_ok=True)
            shutil.copy2(qt_prefix/'plugins/platforms/qoffscreen.dll', platforms/'qoffscreen.dll')
            # CTest invokes the build-tree example; run the extracted distribution as well.
            environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
            run([demo/'XLab.exe', '--smoke-test', '--screenshot-dir', build/'screenshots'], env=environment)
            shutil.copy2(ROOT/'Examples/XLab/assets/DMSans-OFL.txt', demo/'DMSans-OFL.txt')
            shutil.copy2(ROOT/'THIRD_PARTY.md', demo/'THIRD_PARTY.md')
            qt_notices = demo/'licenses/qt'
            qt_notices.mkdir(parents=True)
            shutil.copytree(ROOT/'third_party/qt-notices', qt_notices, dirs_exist_ok=True)
            for pattern in ('LICENSE*','LGPL*','GPL*'):
                for notice in qt_prefix.glob(pattern):
                    if notice.is_file(): shutil.copy2(notice, qt_notices/notice.name)
                    elif notice.is_dir(): shutil.copytree(notice, qt_notices/notice.name, dirs_exist_ok=True)
            (qt_notices/'README.txt').write_text('Qt 6.8.3: https://www.qt.io/licensing/\n'
                'Dynamically linked Qt libraries and plugins are included only in this Demo package.\n'
                'Corresponding Qt source: https://download.qt.io/archive/qt/6.8/6.8.3/single/\n')
            shutil.copytree(stage/'licenses/agora', demo/'licenses/agora', dirs_exist_ok=True) if with_rtc else None
            write_manifest(demo, dict(metadata, artifact='XLab Demo'))
            demo_zip = output/f'{demo.name}.zip'
            archive(demo, demo_zip)
            installed_demo = extract(demo_zip, workspace/'demo-unpacked')
            run([installed_demo/'XLab.exe', '--smoke-test'], env=environment)
            validation.update(ui_smoke=True, demo_archive=demo_zip.name, demo_sha256=sha256(demo_zip))
        test_environment = dict(os.environ)
        if qt_prefix:
            test_environment['PATH'] = str(qt_prefix/'bin') + os.pathsep + test_environment.get('PATH', '')
            test_environment['QT_PLUGIN_PATH'] = str(qt_prefix/'plugins')
            test_environment['QML_IMPORT_PATH'] = str(qt_prefix/'qml')
        run([tool('ctest'), '--test-dir', build, '-C', 'Release', '--output-on-failure'], env=test_environment)
        # Validation is written only after *every* check succeeds, and only for unchanged committed inputs.
        if subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() != revision or subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip():
            raise RuntimeError('Source tree changed during release packaging')
        report_path.write_text(json.dumps(validation, indent=2)+'\n')
    assets = [sdk_zip]
    if qt_prefix: assets.append(output/validation['demo_archive'])
    (output/'SHA256SUMS.txt').write_text(''.join(f'{sha256(path)}  {path.name}\n' for path in assets), encoding='utf-8')
    print(f'Validated artifacts: {output}', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['verify','build-windows'])
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output-dir', type=Path, default=ROOT/'.build/artifacts')
    parser.add_argument('--qt-prefix', type=Path)
    parser.add_argument('--agora-root', type=Path)
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--without-rtc', action='store_true')
    args = parser.parse_args()
    build = (args.build_dir or ROOT/'.build'/('windows-release' if args.command=='build-windows' else 'ci')).resolve()
    if args.command == 'verify': verify(build, args.qt_prefix, args.sanitizers)
    else: build_windows(build, args.output_dir.resolve(), args.qt_prefix, args.agora_root, not args.without_rtc)

if __name__ == '__main__':
    try: main()
    except (RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
