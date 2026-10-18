"""Version preparation, atomic branch integration and explicit GitHub Release publication."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from artifacts import ROOT, REPOSITORY, audit, extract, pe_metadata, sha256
from pipeline import run, verify

SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')

def git(*args: str, root: Path = ROOT, check: bool = True) -> str:
    result = subprocess.run(['git', *args], cwd=root, text=True, capture_output=True)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or f'git {args[0]} failed')
    return result.stdout.strip() if result.returncode == 0 else ''

def clean(root: Path) -> None:
    if git('status','--porcelain',root=root): raise RuntimeError('Working tree must be clean')

def version(value: str) -> str:
    if not SEMVER.fullmatch(value): raise ValueError('Version must be a stable major.minor.patch number without v')
    return value

def origin(root: Path) -> None:
    remote = git('remote','get-url','origin',root=root).removesuffix('.git')
    if remote not in (f'https://github.com/{REPOSITORY}', f'git@github.com:{REPOSITORY}', f'ssh://git@github.com/{REPOSITORY}'):
        raise RuntimeError('origin must point to '+REPOSITORY)

def remote_tag_exists(value: str, root: Path) -> bool:
    result = subprocess.run(['git','ls-remote','--exit-code','--tags','origin',f'refs/tags/{value}'],
                            cwd=root,text=True,capture_output=True)
    if result.returncode == 2: return False
    if result.returncode: raise RuntimeError('Unable to verify remote tag: '+result.stderr.strip())
    return bool(result.stdout.strip())

def prepare(value: str, root: Path = ROOT, dry_run: bool = False) -> None:
    value = version(value)
    clean(root)
    if not git('branch','--show-current',root=root).startswith('feature/'):
        raise RuntimeError('Prepare a version on a feature/* branch')
    if git('rev-parse','-q','--verify',f'refs/tags/{value}',root=root,check=False) or remote_tag_exists(value,root):
        raise RuntimeError('Release tag already exists')
    updates = {root/'VERSION': value+'\n'}
    for name in ['README.md','Materials/usage.md']:
        path = root/name
        content = path.read_text(encoding='utf-8')
        pattern = r'(当前(?:开发)?版本(?:为)?\s*`)[0-9]+\.[0-9]+\.[0-9]+(`)'
        updated, count = re.subn(pattern, lambda m: m[1]+value+m[2], content)
        if count != 1: raise RuntimeError('Missing unambiguous current version in '+name)
        updates[path] = updated
    notes = root/'.cicd/release-notes'/f'{value}.md'
    if not notes.exists():
        updates[notes] = f'# XmaxSDK Windows {value}\n\n## What\'s New\n\nTODO: Describe user-visible changes.\n\n## API Changes\n\nTODO: Describe compatibility and API changes.\n\n## Validation\n\nTODO: Record Windows checks and hardware validation.\n'
    print(f'Preparing {value}; changes: '+', '.join(str(p.relative_to(root)) for p in updates))
    if not dry_run:
        for path, content in updates.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
        run(['git','diff','--check'],cwd=root)
    # No commit, push or tag is created by preparation.

def merge(root: Path = ROOT, skip_validation: bool = False, dry_run: bool = False) -> None:
    clean(root)
    branch = git('branch','--show-current',root=root)
    if not branch.startswith('feature/'): raise RuntimeError('Integration requires a feature/* branch')
    head = git('rev-parse','HEAD',root=root)
    run(['git','fetch','origin',branch,'develop','main'],cwd=root)
    for name in [branch,'develop','main']:
        remote = git('rev-parse',f'origin/{name}',root=root)
        if name == branch and remote != head: raise RuntimeError('Push the feature branch before integration')
        result = subprocess.run(['git','merge-base','--is-ancestor',remote,head],cwd=root)
        if result.returncode: raise RuntimeError(name+' cannot fast-forward to the feature commit')
        if name != branch:
            local = git('rev-parse','--verify',f'refs/heads/{name}',root=root,check=False)
            if local and local != remote: raise RuntimeError('Local '+name+' differs from origin/'+name)
            if f'branch refs/heads/{name}' in git('worktree','list','--porcelain',root=root):
                raise RuntimeError(name+' is checked out in another worktree')
    if not skip_validation:
        if root != ROOT: raise RuntimeError('Isolated fixture requires explicit --skip-validation')
        verify(root/'.build/integration')
        # The Windows job is essential even when integration is driven on a Mac.
        checks = json.loads(subprocess.check_output(['gh','api',f'repos/{REPOSITORY}/commits/{head}/check-runs'],text=True))
        names = {}
        for entry in checks.get('check_runs',[]):
            names.setdefault(entry['name'].split(' / ')[-1], entry)
        for name in ['Source checks','macOS SDK','macOS sanitizers','Windows SDK and package']:
            if name not in names or names[name].get('conclusion') != 'success':
                raise RuntimeError('Required remote check is not successful: '+name)
    if git('rev-parse','HEAD',root=root) != head: raise RuntimeError('HEAD changed during validation')
    clean(root)
    if dry_run:
        print('Would atomically fast-forward origin/develop and origin/main to '+head)
        return
    run(['git','push','--atomic','origin',f'{head}:refs/heads/develop',f'{head}:refs/heads/main'],cwd=root)
    # Only update local baselines after successful remote integration.
    for name in ['develop','main']:
        previous = git('rev-parse','--verify',f'refs/heads/{name}',root=root,check=False)
        run(['git','update-ref',f'refs/heads/{name}',head,previous or '0'*40],cwd=root)

def gh_json(path: str) -> dict:
    return json.loads(subprocess.check_output(['gh','api',path],text=True))

def release_preflight(value: str, root: Path = ROOT) -> tuple[str,Path]:
    value = version(value)
    clean(root); origin(root)
    if git('branch','--show-current',root=root) != 'main': raise RuntimeError('Publish from main')
    head = git('rev-parse','HEAD',root=root)
    if (root/'VERSION').read_text().strip() != value: raise RuntimeError('VERSION differs from requested release')
    if gh_json(f'repos/{REPOSITORY}/git/ref/heads/main')['object']['sha'] != head:
        raise RuntimeError('HEAD differs from remote main')
    tag = gh_json(f'repos/{REPOSITORY}/git/ref/tags/{value}')['object']
    for _ in range(8):
        if tag['type'] != 'tag': break
        tag = gh_json(f'repos/{REPOSITORY}/git/tags/{tag["sha"]}')['object']
    if tag['type'] != 'commit' or tag['sha'] != head: raise RuntimeError('Release tag differs from main')
    if git('rev-parse',f'{value}^{{commit}}',root=root) != head: raise RuntimeError('Local release tag differs from main')
    notes = root/'.cicd/release-notes'/f'{value}.md'
    text = notes.read_text(encoding='utf-8')
    if any(word in text.upper() for word in ['TODO','PLACEHOLDER']) or not all(title in text for title in ["## What's New",'## API Changes','## Validation']):
        raise RuntimeError('Complete Release Notes before publication')
    existing = subprocess.run(['gh','api',f'repos/{REPOSITORY}/releases/tags/{value}'],text=True,capture_output=True)
    if existing.returncode == 0: raise RuntimeError('Release already exists; assets will not be overwritten')
    if 'HTTP 404' not in existing.stderr: raise RuntimeError('Unable to check existing GitHub Releases: '+existing.stderr.strip())
    return head,notes

def validate_release_assets(directory: Path, value: str, head: str) -> list[Path]:
    report = json.loads((directory/'validation.json').read_text(encoding='utf-8'))
    if report.get('version') != value or report.get('commit') != head: raise RuntimeError('Artifacts were built from a different source commit/version')
    if not all(report.get(key) is True for key in ['windows_msvc','installed_c_consumer','installed_cpp_consumer','dotnet_consumer','ui_smoke','with_agora']):
        raise RuntimeError('A release requires the complete Windows MSVC/Agora/Qt/consumer validation')
    if not report.get('sdk_abi',{}).get('runtime_checked'): raise RuntimeError('DLL was not runtime checked')
    assets=[]
    for kind in ['sdk','demo']:
        name = report[f'{kind}_archive']
        expected = f'{"XmaxSDK" if kind=="sdk" else "XLab"}-{value}-windows-x64.zip'
        if name != expected: raise RuntimeError('Unexpected release archive name')
        asset = directory/name
        if sha256(asset) != report[f'{kind}_sha256']: raise RuntimeError('Archive changed after validation')
        with tempfile.TemporaryDirectory(prefix='xmax-verify-') as temporary:
            unpacked = extract(asset, Path(temporary))
            metadata = json.loads((unpacked/'manifest.json').read_text())
            if metadata.get('commit') != head or metadata.get('version') != value or not metadata.get('with_agora'):
                raise RuntimeError('Archive manifest source/flavor mismatch')
            if kind == 'sdk':
                checked = audit(unpacked/'bin/XmaxSDK.dll', runtime=False)
                if checked['sha256'] != report['sdk_abi']['sha256']:
                    raise RuntimeError('Packaged DLL differs from runtime-validated DLL')
            else:
                pe_metadata(unpacked/'XLab.exe')
                if sha256(unpacked/'XmaxSDK.dll') != report['sdk_abi']['sha256']:
                    raise RuntimeError('Demo DLL differs from SDK DLL')
        assets.append(asset)
    checksum = directory/'SHA256SUMS.txt'
    expected_checksums = ''.join(f'{sha256(asset)}  {asset.name}\n' for asset in assets)
    if checksum.read_text(encoding='utf-8') != expected_checksums: raise RuntimeError('SHA256SUMS differs from verified archives')
    return [*assets,checksum,directory/'validation.json']

def release(value: str, directory: Path, publish: bool = False, root: Path = ROOT) -> None:
    head, notes = release_preflight(value,root)
    assets = validate_release_assets(directory,value,head)
    if not publish:
        print('Release preflight passed. Dry run: no tag, GitHub Release or upload has been created.')
        print(json.dumps({'repository':REPOSITORY,'version':value,'commit':head,'assets':[p.name for p in assets]},indent=2))
        return
    run(['gh','release','create',value,'--repo',REPOSITORY,'--verify-tag','--title',f'XmaxSDK Windows {value}',
         '--notes-file',notes,*assets],cwd=root)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command',required=True)
    p = sub.add_parser('prepare'); p.add_argument('version'); p.add_argument('--dry-run',action='store_true')
    p = sub.add_parser('merge'); p.add_argument('--dry-run',action='store_true'); p.add_argument('--skip-validation',action='store_true')
    p = sub.add_parser('check-release'); p.add_argument('version')
    p = sub.add_parser('release'); p.add_argument('version'); p.add_argument('--artifacts-dir',type=Path,required=True); p.add_argument('--publish',action='store_true')
    args = parser.parse_args()
    if args.command=='prepare': prepare(args.version,dry_run=args.dry_run)
    elif args.command=='merge': merge(skip_validation=args.skip_validation,dry_run=args.dry_run)
    elif args.command=='check-release': release_preflight(args.version)
    else: release(args.version,args.artifacts_dir.resolve(),args.publish)

if __name__ == '__main__':
    try: main()
    except (RuntimeError,ValueError,KeyError,OSError,subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}',file=sys.stderr); sys.exit(1)
