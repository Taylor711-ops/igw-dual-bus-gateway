import hashlib,os,shutil,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PATCH=ROOT/'r23_archive_fix'
class Patch(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='r23_patch_');self.root=Path(self.tmp.name);self.app=self.root/'app';self.app.mkdir();self.bin=self.root/'bin';self.bin.mkdir()
  self.ps=self.bin/'ps';self.ps.write_text('#!/bin/sh\necho "1 root init"\n');self.ps.chmod(0o755)
  self.env=dict(os.environ,PATH=str(self.bin)+':'+os.environ['PATH'])
  (self.app/'run_file_web.sh').write_bytes((ROOT/'tests/archive_launcher_before_fix.txt').read_bytes());(self.app/'build_info.txt').write_text('R23_DUAL_FILE_WEB\nfixture\n');(self.app/'igw_file_web').write_bytes(b'fixture executable not run')
  self.manifest()
 def tearDown(self):self.tmp.cleanup()
 def manifest(self):
  names=['run_file_web.sh','build_info.txt','igw_file_web'];(self.app/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((self.app/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names))
 def install(self):return subprocess.run(['sh',str(PATCH/'apply_archive_fix.sh'),str(self.app)],capture_output=True,text=True,env=self.env)
 def test_pinned_patch_updates_manifest_and_is_idempotent(self):
  old=(self.app/'run_file_web.sh').read_bytes();p=self.install();self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('ARCHIVE_FIX_INSTALLED',p.stdout);self.assertEqual((self.app/'run_file_web.sh').read_bytes(),(PATCH/'run_file_web.sh').read_bytes())
  backups=list(self.app.glob('archive_fix_backup.*'));self.assertEqual(len(backups),1);self.assertEqual((backups[0]/'run_file_web.sh').read_bytes(),old)
  subprocess.run(['sha256sum','-c','SHA256SUMS.txt'],cwd=self.app,check=True,capture_output=True)
  p=self.install();self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('ALREADY_INSTALLED',p.stdout);self.assertEqual(len(list(self.app.glob('archive_fix_backup.*'))),1)
 def test_changed_launcher_is_not_overwritten(self):
  data=b'custom launcher';(self.app/'run_file_web.sh').write_bytes(data);self.manifest();p=self.install();self.assertEqual(p.returncode,2);self.assertEqual((self.app/'run_file_web.sh').read_bytes(),data);self.assertFalse(list(self.app.glob('archive_fix_backup.*')))
 def test_manifest_mismatch_is_not_overwritten(self):
  data=(self.app/'run_file_web.sh').read_bytes();(self.app/'igw_file_web').write_bytes(b'changed');p=self.install();self.assertEqual(p.returncode,2);self.assertEqual((self.app/'run_file_web.sh').read_bytes(),data)
 def test_active_service_refuses_install(self):
  self.ps.write_text('#!/bin/sh\necho "99 root ./igw_file_web"\n');p=self.install();self.assertEqual(p.returncode,2);self.assertFalse(list(self.app.glob('archive_fix_backup.*')))
 def test_evidence_recovery_copies_runtime_and_matching_launcher(self):
  source=Path(tempfile.mkdtemp(prefix='igw_r22_files.'));launcher=Path(tempfile.mkdtemp(prefix='igw_r23_launcher.'))
  try:
   (source/'task.log').write_text('PROGRESS bytes=61440/1048576\nCAN_BLOCK_SEND_TIMEOUT op=5 offset=99840\n');(source/'task.input').write_bytes(b'fixture')
   (launcher/'service.log').write_text('EVIDENCE_DIRECTORY='+str(source)+'\nR23_STOP jobs=1 blocked=1\n');(launcher/'can_before.txt').write_text('fixture')
   script=self.root/'recover.sh';script.write_text((PATCH/'save_existing_evidence.sh').read_text().replace('/mnt/mmcblk0p1',str(self.root/'sd')))
   p=subprocess.run(['sh',str(script),str(source)],capture_output=True,text=True,env=self.env);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   targets=list((self.root/'sd/igw_evidence/r23_file_web').glob('recovered_*'));self.assertEqual(len(targets),1);target=targets[0]
   self.assertEqual((target/'task.log').read_bytes(),(source/'task.log').read_bytes());self.assertTrue((source/'task.input').exists());self.assertTrue((target/'launcher_records'/launcher.name/'service.log').is_file());self.assertIn('MATCHED_LAUNCHER_RECORDS=1',(target/'RECOVERY_NOTE.txt').read_text())
   subprocess.run(['sha256sum','-c','RECOVERY_SHA256SUMS.txt'],cwd=target,check=True,capture_output=True)
  finally:shutil.rmtree(source);shutil.rmtree(launcher)
if __name__=='__main__':unittest.main(verbosity=2)
