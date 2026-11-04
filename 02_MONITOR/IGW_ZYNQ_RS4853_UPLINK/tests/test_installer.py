"""Exercise the board installer in a temporary directory, not on the board."""
from hashlib import sha256
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
def manifest(folder,names):
 (folder/'SHA256SUMS.txt').write_text(''.join(f'{sha256((folder/n).read_bytes()).hexdigest()}  {n}\n' for n in names))

class InstallTests(unittest.TestCase):
 def test_stages_and_preserves_existing_version(self):
  with tempfile.TemporaryDirectory() as t:
   tmp=Path(t);apps=tmp/'apps';base=apps/'r26_rs4853_demo';base.mkdir(parents=True)
   names=['igw_web','igw_rs485_rw','run_web_gateway.sh','igw_can_udp']
   for n in names:(base/n).write_text('original '+n)
   manifest(base,names);original={n:(base/n).read_bytes() for n in names+['SHA256SUMS.txt']}
   deploy=tmp/'deploy';deploy.mkdir()
   for n in names[:3]:(deploy/n).write_text('new '+n)
   installer=(ROOT/'host/install_on_board.sh').read_text()
   installer=installer.replace('igw_apps=/mnt/mmcblk0p1/igw_apps',f'igw_apps="{apps}"')
   mount="grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: main SD is not mounted'; exit 1; }"
   self.assertIn(mount,installer);installer=installer.replace(mount,':')
   installer=installer.replace('/tmp/igw_web_r10.lock /tmp/igw_rs485_once.lock',f'{tmp}/web.lock {tmp}/rs.lock')
   installer=installer.replace('\nsync\n','\n:\n')
   (deploy/'install_on_board.sh').write_text(installer)
   manifest(deploy,names[:3]+['install_on_board.sh'])
   run=subprocess.run(['sh','install_on_board.sh'],cwd=deploy,text=True,capture_output=True)
   self.assertEqual(run.returncode,0,run.stdout+run.stderr)
   dest=apps/'r26_rs4853_uplink_demo';self.assertTrue(dest.is_dir())
   for n in names[:3]:self.assertEqual((dest/n).read_text(),'new '+n)
   self.assertEqual((dest/'igw_can_udp').read_text(),'original igw_can_udp')
   self.assertEqual(subprocess.run(['sha256sum','-c','SHA256SUMS.txt'],cwd=dest,capture_output=True).returncode,0)
   self.assertEqual({n:(base/n).read_bytes() for n in original},original)
   rerun=subprocess.run(['sh','install_on_board.sh'],cwd=deploy,text=True,capture_output=True)
   self.assertNotEqual(rerun.returncode,0);self.assertIn('destination exists',rerun.stdout)

if __name__=='__main__':unittest.main(verbosity=2)
