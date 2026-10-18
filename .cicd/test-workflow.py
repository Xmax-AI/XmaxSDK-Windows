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
        with self.assertRaisesRegex(RuntimeError,'fixture'):workflow.merge(self.f.root)
        self.assertEqual(old,self.f.git('ls-remote','origin','refs/heads/main'))
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
