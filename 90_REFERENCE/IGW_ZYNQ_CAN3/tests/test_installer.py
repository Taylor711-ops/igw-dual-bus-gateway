"""Test installer copies and failure gates in a temporary directory, not a board."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def manifest(root,names):
 (root/'SHA256SUMS.txt').write_text(''.join(digest(root/n)+'  '+n+'\n' for n in names))
with tempfile.TemporaryDirectory(prefix='igw_can3_installer_') as directory:
 t=Path(directory);apps=t/'apps';base=apps/'r26_bidir_ui';base.mkdir(parents=True)
 (base/'igw_web').write_bytes(b'old-web\n');(base/'run_web_gateway.sh').write_bytes(b'old-launcher\n');(base/'other').write_bytes(b'other-baseline\n')
 manifest(base,['igw_web','run_web_gateway.sh','other']);before=(base/'SHA256SUMS.txt').read_bytes()
 deploy=t/'deploy';deploy.mkdir();(deploy/'igw_web').write_bytes(b'new-web\n');(deploy/'run_web_gateway.sh').write_bytes(b'new-launcher\n')
 code=(ROOT/'host/install_on_board.sh').read_text()
 code=code.replace('igw_apps=/mnt/mmcblk0p1/igw_apps',f'igw_apps={apps}')
 code=code.replace("grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: main SD is not mounted'; exit 1; }",': # test-only: no mount')
 code=code.replace('/proc/[0-9]*/comm',str(t/'no-process'/'comm'))
 code=code.replace('for igw_lock in /tmp/igw_web_r10.lock /tmp/igw_rs485_once.lock;',f'for igw_lock in {t}/no-lock;')
 code=code.replace('\nsync\n','\n: # test-only sync\n')
 (deploy/'install_on_board.sh').write_text(code);manifest(deploy,['igw_web','run_web_gateway.sh','install_on_board.sh'])
 first=subprocess.run(['sh','install_on_board.sh'],cwd=deploy,text=True,capture_output=True)
 assert first.returncode==0,(first.stdout,first.stderr)
 new=apps/'r26_can3_demo';assert (new/'igw_web').read_bytes()==b'new-web\n';assert (new/'run_web_gateway.sh').read_bytes()==b'new-launcher\n'
 assert (new/'other').read_bytes()==b'other-baseline\n' and (base/'igw_web').read_bytes()==b'old-web\n' and (base/'SHA256SUMS.txt').read_bytes()==before
 assert subprocess.run(['sha256sum','-c','SHA256SUMS.txt'],cwd=new,capture_output=True).returncode==0
 again=subprocess.run(['sh','install_on_board.sh'],cwd=deploy,text=True,capture_output=True)
 assert again.returncode!=0 and 'destination exists' in again.stdout
 print('PASS_CAN3_INSTALLER simulated copy, manifest, baseline preservation and no-overwrite gate')
