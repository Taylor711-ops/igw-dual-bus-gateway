import hashlib,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
with tempfile.TemporaryDirectory() as d:
 p=Path(d);apps=p/'apps';deploy=p/'deploy';deploy.mkdir();apps.mkdir()
 old1=b'old-monitor';old2=b'old-file';new1=b'new-monitor';new2=b'new-file'
 for folder,name,blob in [('r26_bidir_demo','igw_web',old1),('r23_sd_files','igw_file_web',old2)]:
  q=apps/folder;q.mkdir();(q/name).write_bytes(blob);(q/'SHA256SUMS.txt').write_text(sha(blob)+'  '+name+'\n')
 for n,b in [('igw_web',new1),('igw_file_web',new2)]: (deploy/n).write_bytes(b)
 script=(root/'install_ui_on_board.sh').read_text().replace('/mnt/mmcblk0p1/igw_apps',str(apps)).replace('2547617b5c34373a860e50ffd9c0b4ee45ea93fc74e2cb3ea5bf1c3f077781c3',sha(old1)).replace('ca12c2df6f25af3b3e77f452f4f51770f1f503d03f69ee58ffbfa786e75c21dd',sha(old2))
 (deploy/'install_ui_on_board.sh').write_text(script)
 (deploy/'SHA256SUMS.txt').write_text(''.join(sha((deploy/n).read_bytes())+'  '+n+'\n' for n in ['igw_web','igw_file_web','install_ui_on_board.sh']))
 x=subprocess.run(['sh','install_ui_on_board.sh'],cwd=deploy,text=True,capture_output=True);assert x.returncode==0,x.stdout+x.stderr
 assert (apps/'r26_bidir_ui/igw_web').read_bytes()==new1 and (apps/'r23_sd_ui/igw_file_web').read_bytes()==new2
 assert (apps/'r26_bidir_demo/igw_web').read_bytes()==old1 and (apps/'r23_sd_files/igw_file_web').read_bytes()==old2
 x=subprocess.run(['sh','install_ui_on_board.sh'],cwd=deploy,text=True,capture_output=True);assert x.returncode!=0 and 'destination already exists' in x.stdout
 print('PASS staging copies, preserves baseline, verifies checksums and rejects existing destinations')
