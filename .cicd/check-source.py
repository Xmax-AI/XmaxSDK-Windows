"""Fast source, ABI, dependency and documentation consistency checks; no network or credentials."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
from artifacts import ROOT, public_exports
from workflow import version
from structure import validate as validate_structure

def main() -> None:
    validate_structure(ROOT)
    subprocess.run([sys.executable, str(ROOT/'.cicd/generate-dotnet-interop.py'), '--check'], check=True)
    current = version((ROOT/'VERSION').read_text().strip())
    for name in ['README.md','Materials/usage.md']:
        text = (ROOT/name).read_text(encoding='utf-8')
        found = re.findall(r'当前(?:开发)?版本(?:为)?\s*`([^`]+)`', text)
        assert found == [current], f'Version mismatch in {name}: {found}'
    baseline = json.loads((ROOT/'.cicd/abi-v1.json').read_text())
    assert baseline['abi_version'] == 1 and baseline['packing'] == 8
    assert set(baseline['exports']).issubset(public_exports(ROOT/'include/xmax/xmax.h'))
    dependencies = json.loads((ROOT/'.cicd/dependencies.json').read_text())
    assert re.fullmatch('[a-f0-9]{64}',dependencies['agora']['sha256'])
    assert dependencies['agora']['url'].startswith('https://download.agora.io/')
    assert dependencies['agora']['version'] in dependencies['agora']['url']
    notices = ROOT/'third_party/qt-notices'
    for module, record in json.loads((notices/'sources.json').read_text()).items():
        assert record['version'] == dependencies['qt']['version']
        for name, source in record['files'].items():
            assert hashlib.sha256((notices/module/name).read_bytes()).hexdigest() == source['sha256']
    expected = '9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6'
    assert hashlib.sha256((ROOT/'third_party/nlohmann/json.hpp').read_bytes()).hexdigest() == expected
    for path in [ROOT/'README.md',*(ROOT/'Materials').glob('*.md')]:
        for link in re.findall(r'\]\(([^)]+)\)',path.read_text(encoding='utf-8')):
            if re.match(r'https?:|#',link): continue
            assert (path.parent/link.split('#')[0]).exists(), f'Broken documentation link in {path}: {link}'
    attributes = subprocess.check_output(['git','check-attr','eol','--','third_party/nlohmann/json.hpp'],cwd=ROOT,text=True)
    assert attributes.strip().endswith(': lf'), 'Vendored text must retain LF for cross-platform SHA checks'
    assert subprocess.run(['git','check-ignore','-q','Docs/local-design.md'],cwd=ROOT).returncode == 0
    assert subprocess.run(['git','check-ignore','-q','.cicd/workflow.py'],cwd=ROOT).returncode == 1
    subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
    print(f'Source checks passed: {current}, ABI v1, locked dependencies, public docs and ignore rules')

if __name__ == '__main__':
    try: main()
    except (AssertionError,ValueError,OSError,subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}',file=sys.stderr);sys.exit(1)
