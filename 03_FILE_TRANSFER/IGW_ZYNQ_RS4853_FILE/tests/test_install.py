import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class Installer(unittest.TestCase):
    def test_stages_new_directory_preserving_source(self):
        with tempfile.TemporaryDirectory(prefix='igw3_install_') as td:
            root=Path(td);apps=root/'igw_apps';base=apps/'r23_sd_ui';install=apps/'r23_sd_unit3_install'
            base.mkdir(parents=True);install.mkdir()
            original={'igw_file_web':b'old web', 'igw_rs485_file':b'old RS', 'igw_can_block':b'keep CAN', 'build_info.txt':b'old info'}
            for name,data in original.items(): (base/name).write_bytes(data)
            def sums(folder,names):
                (folder/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((folder/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names))
            sums(base,original)
            for name in ('igw_file_web','igw_rs485_file'): (install/name).write_bytes(('new '+name).encode())
            text=(ROOT/'host/install_on_board.sh').read_text().replace('/mnt/mmcblk0p1/igw_apps',str(apps))
            (install/'install_on_board.sh').write_text(text)
            sums(install,['igw_file_web','igw_rs485_file','install_on_board.sh'])
            p=subprocess.run(['sh',str(install/'install_on_board.sh')],text=True,capture_output=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            dest=apps/'r23_sd_unit3';self.assertTrue(dest.is_dir())
            for name,data in original.items(): self.assertEqual((base/name).read_bytes(),data)
            self.assertEqual((dest/'igw_can_block').read_bytes(),original['igw_can_block'])
            self.assertEqual((dest/'igw_file_web').read_bytes(),b'new igw_file_web')
            subprocess.run(['sha256sum','-c','SHA256SUMS.txt'],cwd=dest,check=True,capture_output=True)
            p=subprocess.run(['sh',str(install/'install_on_board.sh')],text=True,capture_output=True)
            self.assertNotEqual(p.returncode,0);self.assertIn('destination already exists',p.stdout+p.stderr)
            self.assertEqual((dest/'igw_file_web').read_bytes(),b'new igw_file_web')

if __name__=='__main__':unittest.main(verbosity=2)
