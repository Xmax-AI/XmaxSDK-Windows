"""Isolated release workflow tests. Git remotes, binaries and GitHub calls are fixtures only."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import artifacts
import workflow

class Fixture:
    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='xmax-flow-tests-')
        self.root = Path(self.temporary.name)/'source'
        self.remote = Path(self.temporary.name)/'remote.git'
        self.root.mkdir()
        def call(*args, cwd=None):
            return subprocess.check_output(['git',*args],cwd=cwd or self.root,stderr=subprocess.DEVNULL,text=True).strip()
        self.git = call
        call('init','--bare',str(self.remote))
        call('init','-b','main')
        call('config','user.name','Workflow test')
        call('config','user.email','test@example.invalid')
        call('remote','add','origin',str(self.remote))
        (self.root/'VERSION').write_text('0.2.0\n')
        (self.root/'README.md').write_text('当前开发版本为 `0.2.0`。\n')
        (self.root/'Materials').mkdir()
        (self.root/'Materials/usage.md').write_text('当前版本 `0.2.0`。\n')
        notes=self.root/'.cicd/release-notes';notes.mkdir(parents=True)
        (notes/'0.2.0.md').write_text("# 0.2.0\n\n## What's New\nFeatures.\n\n## API Changes\nAdditive.\n\n## Validation\nWindows checked.\n")
        call('add','.');call('commit','-m','baseline')
        call('branch','develop');call('push','origin','main','develop')
        call('switch','-c','feature/test')
        (self.root/'feature.txt').write_text('feature\n')
        call('add','.');call('commit','-m','feature');call('push','origin','feature/test')
        self.head=call('rev-parse','HEAD')
    def close(self): self.temporary.cleanup()

class GitFlowTests(unittest.TestCase):
    def setUp(self): self.f=Fixture()
    def tearDown(self): self.f.close()
    def test_prepare_updates_version_without_tag(self):
        workflow.prepare('0.3.0',self.f.root)
        self.assertEqual((self.f.root/'VERSION').read_text(),'0.3.0\n')
        self.assertIn('`0.3.0`',(self.f.root/'README.md').read_text())
        self.assertTrue((self.f.root/'.cicd/release-notes/0.3.0.md').exists())
        self.assertEqual(self.f.git('tag'),'')
    def test_prepare_dry_run(self):
        workflow.prepare('0.3.0',self.f.root,True)
        self.assertEqual(self.f.git('status','--porcelain'),'')
    def test_prepare_rejects_dirty_source(self):
        (self.f.root/'untracked').write_text('x')
        with self.assertRaisesRegex(RuntimeError,'clean'): workflow.prepare('0.3.0',self.f.root)
    def test_prepare_rejects_main(self):
        self.f.git('switch','main')
        with self.assertRaisesRegex(RuntimeError,'feature'): workflow.prepare('0.3.0',self.f.root)
    def test_prepare_rejects_existing_local_tag(self):
        self.f.git('tag','0.3.0')
        with self.assertRaisesRegex(RuntimeError,'exists'): workflow.prepare('0.3.0',self.f.root)
    def test_prepare_rejects_existing_remote_tag(self):
        self.f.git('tag','0.3.0');self.f.git('push','origin','0.3.0');self.f.git('tag','-d','0.3.0')
        with self.assertRaisesRegex(RuntimeError,'exists'): workflow.prepare('0.3.0',self.f.root)
    def test_prepare_rejects_unsafe_version(self):
        for value in ['v1.0.0','../1.0.0','01.0.0','1.0','1.0.0;echo x','1.0.0-rc.1']:
            with self.assertRaises(ValueError): workflow.prepare(value,self.f.root)
    def test_atomic_fast_forward(self):
        workflow.merge(self.f.root,True)
        self.assertEqual(self.f.git('rev-parse','main'),self.f.head)
        self.assertEqual(self.f.git('rev-parse','develop'),self.f.head)
        for branch in ['main','develop']:
            self.assertIn(self.f.head,self.f.git('ls-remote','origin',f'refs/heads/{branch}'))
    def test_merge_dry_run(self):
        old=self.f.git('rev-parse','main');workflow.merge(self.f.root,True,True)
        self.assertEqual(self.f.git('rev-parse','main'),old)
    def test_merge_rejects_unpushed_feature(self):
        (self.f.root/'new').write_text('x');self.f.git('add','.');self.f.git('commit','-m','unpublished')
        with self.assertRaisesRegex(RuntimeError,'Push'):workflow.merge(self.f.root,True)
    def test_merge_rejects_local_baseline_drift(self):
        self.f.git('update-ref','refs/heads/main',self.f.head)
        with self.assertRaisesRegex(RuntimeError,'differs'):workflow.merge(self.f.root,True)
    def test_merge_rejects_diverged_remote(self):
        self.f.git('switch','main');(self.f.root/'main-only').write_text('x')
        self.f.git('add','.');self.f.git('commit','-m','diverged');self.f.git('push','origin','main');self.f.git('switch','feature/test')
        with self.assertRaisesRegex(RuntimeError,'fast-forward'):workflow.merge(self.f.root,True)
    def test_merge_rejects_missing_remote(self):
        self.f.git('push','origin','--delete','develop')
        with self.assertRaises(subprocess.CalledProcessError):workflow.merge(self.f.root,True)
    def test_validation_failure_does_not_push(self):
        old=self.f.git('ls-remote','origin','refs/heads/main')
        with patch.object(workflow,'ROOT',self.f.root),patch.object(workflow,'verify',side_effect=RuntimeError('validation failed')):
            with self.assertRaisesRegex(RuntimeError,'validation failed'):workflow.merge(self.f.root)
        self.assertEqual(old,self.f.git('ls-remote','origin','refs/heads/main'))
    def release_context(self, *, existing=False, network=False, tag_mismatch=False):
        self.f.git('switch','main');self.f.git('tag','0.2.0')
        head=self.f.git('rev-parse','HEAD')
        original=subprocess.run
        def api(path):
            return {'object':{'type':'commit','sha':'wrong' if tag_mismatch and '/tags/' in path else head}}
        def command(args,*a,**kw):
            if args[0]=='gh':
                return subprocess.CompletedProcess(args,0 if existing else 1,'{}','network failed' if network else 'HTTP 404')
            return original(args,*a,**kw)
        context=contextlib.ExitStack()
        context.enter_context(patch.object(workflow,'origin'))
        context.enter_context(patch.object(workflow,'gh_json',side_effect=api))
        context.enter_context(patch.object(workflow.subprocess,'run',side_effect=command))
        return context,head
    def test_release_preflight_success(self):
        context,head=self.release_context()
        with context:self.assertEqual(workflow.release_preflight('0.2.0',self.f.root)[0],head)
    def test_release_rejects_existing_release(self):
        context,_=self.release_context(existing=True)
        with context:
            with self.assertRaisesRegex(RuntimeError,'already exists'):workflow.release_preflight('0.2.0',self.f.root)
    def test_release_network_error_is_not_treated_as_missing_release(self):
        context,_=self.release_context(network=True)
        with context:
            with self.assertRaisesRegex(RuntimeError,'Unable to check'):workflow.release_preflight('0.2.0',self.f.root)
    def test_release_rejects_tag_mismatch(self):
        context,_=self.release_context(tag_mismatch=True)
        with context:
            with self.assertRaisesRegex(RuntimeError,'tag differs'):workflow.release_preflight('0.2.0',self.f.root)
    def test_release_rejects_incomplete_notes(self):
        context,_=self.release_context()
        with context:
            (self.f.root/'.cicd/release-notes/0.2.0.md').write_text('TODO')
            self.f.git('add','.');self.f.git('commit','-m','incomplete notes')
            with patch.object(workflow,'gh_json',return_value={'object':{'type':'commit','sha':self.f.git('rev-parse','HEAD')}}):
                self.f.git('tag','-f','0.2.0')
                with self.assertRaisesRegex(RuntimeError,'Notes'):workflow.release_preflight('0.2.0',self.f.root)
    def test_release_requires_main(self):
        with patch.object(workflow,'origin'):
            with self.assertRaisesRegex(RuntimeError,'main'):workflow.release_preflight('0.2.0',self.f.root)
    def test_release_requires_correct_origin(self):
        with self.assertRaisesRegex(RuntimeError,'origin'): workflow.origin(self.f.root)
    def test_release_rejects_version_mismatch(self):
        self.f.git('switch','main')
        with patch.object(workflow,'origin'):
            with self.assertRaisesRegex(RuntimeError,'VERSION'):workflow.release_preflight('0.3.0',self.f.root)
    def test_release_rejects_stale_main(self):
        self.f.git('switch','main')
        with patch.object(workflow,'origin'),patch.object(workflow,'gh_json',return_value={'object':{'sha':'other'}}):
            with self.assertRaisesRegex(RuntimeError,'remote main'):workflow.release_preflight('0.2.0',self.f.root)

class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='xmax-assets-test-');self.root=Path(self.temporary.name)
    def tearDown(self):self.temporary.cleanup()
    def package(self):
        stage=self.root/'SDK';stage.mkdir();(stage/'file').write_bytes(b'\x00\x01media')
        artifacts.write_manifest(stage,{'version':'0.2.0','commit':'abc','with_agora':True})
        archive=self.root/'sdk.zip';artifacts.archive(stage,archive);return stage,archive
    def assets(self):
        def binary(exports):
            data=bytearray(16384);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80)
            data[0x80:0x84]=b'PE\0\0';struct.pack_into('<HH',data,0x84,0x8664,1)
            struct.pack_into('<H',data,0x94,240);optional=0x98
            struct.pack_into('<H',data,optional,0x20b)
            section=optional+240;struct.pack_into('<IIII',data,section+8,8192,0x1000,8192,0x200)
            if exports:
                struct.pack_into('<II',data,optional+112,0x1000,40)
                struct.pack_into('<I',data,0x200+24,len(exports));struct.pack_into('<I',data,0x200+32,0x1100)
                cursor=0x500
                for i,name in enumerate(sorted(exports)):
                    struct.pack_into('<I',data,0x300+i*4,cursor-0x200+0x1000)
                    encoded=name.encode()+b'\0';data[cursor:cursor+len(encoded)]=encoded;cursor+=len(encoded)
            return bytes(data)
        sdk=self.root/'XmaxSDK-0.2.0-windows-x64';(sdk/'bin').mkdir(parents=True)
        (sdk/'bin/XmaxSDK.dll').write_bytes(binary(artifacts.public_exports(artifacts.ROOT/'include/xmax/xmax.h')))
        checked=artifacts.audit(sdk/'bin/XmaxSDK.dll',runtime=False);checked['runtime_checked']=True
        report={'version':'0.2.0','commit':'abc','sdk_abi':checked,**{key:True for key in ['windows_msvc','installed_c_consumer','installed_cpp_consumer','dotnet_consumer','ui_smoke','with_agora']}}
        demo=self.root/'XLab-0.2.0-windows-x64';demo.mkdir()
        (demo/'XmaxSDK.dll').write_bytes((sdk/'bin/XmaxSDK.dll').read_bytes());(demo/'XLab.exe').write_bytes(binary(set()))
        for kind,stage in [('sdk',sdk),('demo',demo)]:
            artifacts.write_manifest(stage,{'version':'0.2.0','commit':'abc','with_agora':True})
            archive=self.root/(stage.name+'.zip');artifacts.archive(stage,archive)
            report[kind+'_archive']=archive.name;report[kind+'_sha256']=artifacts.sha256(archive)
        (self.root/'validation.json').write_text(json.dumps(report))
        (self.root/'SHA256SUMS.txt').write_text(''.join(report[k+'_sha256']+'  '+report[k+'_archive']+'\n' for k in ['sdk','demo']))
        return report
    def test_complete_release_assets(self):
        self.assets();self.assertEqual(len(workflow.validate_release_assets(self.root,'0.2.0','abc')),4)
    def test_release_rejects_modified_archive(self):
        report=self.assets()
        with (self.root/report['sdk_archive']).open('ab') as f:f.write(b'tampered')
        with self.assertRaisesRegex(RuntimeError,'Archive changed'):workflow.validate_release_assets(self.root,'0.2.0','abc')
    def test_release_rejects_changed_checksums(self):
        self.assets();(self.root/'SHA256SUMS.txt').write_text('invalid')
        with self.assertRaisesRegex(RuntimeError,'SHA256SUMS'):workflow.validate_release_assets(self.root,'0.2.0','abc')
    def test_release_rejects_unvalidated_dll(self):
        report=self.assets();report['sdk_abi']['sha256']='0'*64
        (self.root/'validation.json').write_text(json.dumps(report))
        with self.assertRaisesRegex(RuntimeError,'runtime-validated'):workflow.validate_release_assets(self.root,'0.2.0','abc')
    def test_round_trip(self):
        _,archive=self.package();unpacked=artifacts.extract(archive,self.root/'out')
        self.assertEqual((unpacked/'file').read_bytes(),b'\x00\x01media')
    def test_hash_tampering(self):
        stage,_=self.package();(stage/'file').write_text('tampered')
        with self.assertRaisesRegex(ValueError,'checksum'):artifacts.verify_manifest(stage)
    def test_inventory_tampering(self):
        stage,_=self.package();(stage/'extra').write_text('x')
        with self.assertRaisesRegex(ValueError,'inventory'):artifacts.verify_manifest(stage)
    def test_reject_zip_traversal(self):
        archive=self.root/'bad.zip'
        with zipfile.ZipFile(archive,'w') as output:output.writestr('../escape','x')
        with self.assertRaisesRegex(ValueError,'Unsafe'):artifacts.extract(archive,self.root/'out')
        self.assertFalse((self.root.parent/'escape').exists())
    def test_reject_zip_symlink(self):
        archive=self.root/'bad.zip'
        with zipfile.ZipFile(archive,'w') as output:
            info=zipfile.ZipInfo('SDK/link');info.external_attr=0o120777<<16;output.writestr(info,'/tmp')
        with self.assertRaisesRegex(ValueError,'Symlink'):artifacts.extract(archive,self.root/'out')
    def test_reject_fake_pe(self):
        path=self.root/'fake.dll';path.write_bytes(b'not a DLL')
        with self.assertRaises(ValueError):artifacts.pe_metadata(path)
    def test_reject_incomplete_windows_validation(self):
        (self.root/'validation.json').write_text(json.dumps({'version':'0.2.0','commit':'abc'}))
        with self.assertRaisesRegex(RuntimeError,'complete Windows'):workflow.validate_release_assets(self.root,'0.2.0','abc')
    def test_reject_different_commit(self):
        (self.root/'validation.json').write_text(json.dumps({'version':'0.2.0','commit':'other'}))
        with self.assertRaisesRegex(RuntimeError,'different source'):workflow.validate_release_assets(self.root,'0.2.0','abc')
    def test_dry_run_never_publishes(self):
        with patch.object(workflow,'release_preflight',return_value=('abc',self.root/'notes')),patch.object(workflow,'validate_release_assets',return_value=[]),patch.object(workflow,'run') as operation:
            workflow.release('0.2.0',self.root)
            operation.assert_not_called()
    def test_publish_requires_explicit_flag(self):
        with patch.object(workflow,'release_preflight',return_value=('abc',self.root/'notes')),patch.object(workflow,'validate_release_assets',return_value=[]),patch.object(workflow,'run') as operation:
            workflow.release('0.2.0',self.root,True)
            self.assertIn('--verify-tag',operation.call_args.args[0])
            self.assertEqual(operation.call_args.args[0][:3],['gh','release','create'])

if __name__=='__main__': unittest.main(verbosity=2)
