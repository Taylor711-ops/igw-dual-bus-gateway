"""Actual C vault / libcrypto vs independent Python AESGCM/PBKDF2.
No FPGA hardware, target OpenSSL 1.1, SD power-loss or timing claims.
"""
import concurrent.futures,hashlib,json,os,secrets,signal,socket,struct,subprocess,tempfile,time,unittest
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
ROOT=Path(__file__).resolve().parents[1]
PASSWORD=b'Host-fixture-only-1974!'
class VaultTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.binary=str(Path(cls.temp.name)/'vault')
  flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror']
  if os.environ.get('IGW_WEB_SANITIZE'):flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(flags+[str(ROOT/'src/igw_vault.c'),'-ldl','-o',cls.binary],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def setUp(self):self.tempdir=tempfile.TemporaryDirectory();self.path=Path(self.tempdir.name);self.processes=[]
 def tearDown(self):
  for p in self.processes:
   if p.poll() is None:p.terminate();p.wait(15)
  self.tempdir.cleanup()
 def run_cmd(self,*args,password=PASSWORD,confirm=False):
  return subprocess.run([self.binary,*map(str,args),'--pass-fd','0'],input=password+b'\n'+(password+b'\n' if confirm else b''),capture_output=True,timeout=20)
 def sealed(self,data=b'{"fixture":13579,"secret_business_data":true}'):
  src=self.path/'source';out=self.path/('sealed-'+secrets.token_hex(4));src.write_bytes(data)
  p=self.run_cmd('seal','--input',src,'--output',out,confirm=True);self.assertEqual(p.returncode,0,p.stderr);return out,data
 @staticmethod
 def independent_open(data,password=PASSWORD):
  assert data[:8]==b'IGWVLT11' and data[8:12]==b'\1\1\0\0'
  iterations=int.from_bytes(data[12:16],'big');assert iterations==600000
  assert int.from_bytes(data[44:52],'big')==len(data)-80
  key=hashlib.pbkdf2_hmac('sha256',password,data[16:32],iterations,32)
  return AESGCM(key).decrypt(data[32:44],data[64:],data[:64])
 def test_crypto_probe_known_vector(self):
  p=subprocess.run([self.binary,'probe'],capture_output=True);self.assertEqual(p.returncode,0);self.assertIn(b'KNOWN_VECTOR=PASS',p.stdout)
 def test_independent_decrypt_and_restart_recovery(self):
  encrypted,data=self.sealed();self.assertEqual(self.independent_open(encrypted.read_bytes()),data)
  out=self.path/'decoded';p=self.run_cmd('open','--input',encrypted,'--output',out);self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(out.read_bytes(),data)
  self.assertEqual(out.stat().st_mode&0o777,0o600);self.assertNotIn(PASSWORD,p.stdout+p.stderr);self.assertNotIn(data,encrypted.read_bytes())
 def test_independently_created_format_decrypts_in_c(self):
  payload=b'cross implementation '+bytes(range(256));h=bytearray(64);h[:8]=b'IGWVLT11';h[8:10]=b'\1\1';h[12:16]=(600000).to_bytes(4,'big');h[16:32]=os.urandom(16);h[32:44]=os.urandom(12);h[44:52]=len(payload).to_bytes(8,'big')
  key=hashlib.pbkdf2_hmac('sha256',PASSWORD,h[16:32],600000,32);src=self.path/'foreign';src.write_bytes(h+AESGCM(key).encrypt(bytes(h[32:44]),payload,bytes(h)));out=self.path/'opened'
  p=self.run_cmd('open','--input',src,'--output',out);self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(out.read_bytes(),payload)
 def test_wrong_password_rejects_without_output(self):
  encrypted,_=self.sealed();out=self.path/'wrong';p=self.run_cmd('open','--input',encrypted,'--output',out,password=b'Different-password-00');self.assertEqual(p.returncode,3);self.assertFalse(out.exists())
 def test_actual_archive_verify_wrong_key_and_tamper_no_file_change(self):
  encrypted,_=self.sealed();before=encrypted.read_bytes();p=self.run_cmd('verify','--input',encrypted);self.assertEqual(p.returncode,0,p.stderr);self.assertIn(b'PASS_ARCHIVE_VERIFY',p.stdout)
  self.assertIn(b'WRONG_PASSPHRASE_REJECT=PASS',p.stdout);self.assertIn(b'TAG_TAMPER_REJECT=PASS',p.stdout);self.assertEqual(encrypted.read_bytes(),before)
 def test_header_ciphertext_tag_and_truncation_rejected(self):
  encrypted,_=self.sealed();data=encrypted.read_bytes()
  mutations=[]
  for i in [0,8,9,10,12,16,31,32,43,44,51,52,63,64,len(data)-17,len(data)-1]:
   v=bytearray(data);v[i]^=1;mutations.append(v)
  mutations += [data[:30],data[:-1],data+b'x']
  for i,value in enumerate(mutations):
   src=self.path/'corrupt';src.write_bytes(value);out=self.path/f'out-{i}';p=self.run_cmd('open','--input',src,'--output',out);self.assertEqual(p.returncode,3,(i,p.stderr));self.assertFalse(out.exists())
 def test_randomized_salt_and_nonce_and_empty_and_binary(self):
  for payload in [b'',bytes(range(256))*8]:
   a,_=self.sealed(payload);b,_=self.sealed(payload);self.assertNotEqual(a.read_bytes(),b.read_bytes());self.assertNotEqual(a.read_bytes()[16:44],b.read_bytes()[16:44]);self.assertEqual(self.independent_open(a.read_bytes()),payload)
 def test_existing_output_symlink_and_input_symlink_refused(self):
  encrypted,_=self.sealed();out=self.path/'existing';out.write_text('preserve');p=self.run_cmd('open','--input',encrypted,'--output',out);self.assertNotEqual(p.returncode,0);self.assertEqual(out.read_text(),'preserve')
  link=self.path/'symlink';link.symlink_to(out);p=self.run_cmd('open','--input',encrypted,'--output',link);self.assertNotEqual(p.returncode,0);self.assertEqual(out.read_text(),'preserve')
  src=self.path/'inputlink';src.symlink_to(encrypted);p=self.run_cmd('open','--input',src,'--output',self.path/'absent');self.assertNotEqual(p.returncode,0);self.assertFalse((self.path/'absent').exists())
 def test_password_confirmation_and_cli_secret_refused(self):
  src=self.path/'source';src.write_text('x');out=self.path/'encrypted';p=subprocess.run([self.binary,'seal','--input',str(src),'--output',str(out),'--pass-fd','0'],input=PASSWORD+b'\n'+b'different-confirmation\n',capture_output=True);self.assertNotEqual(p.returncode,0);self.assertFalse(out.exists())
  p=self.run_cmd('seal','--input',src,'--output',out,'--password',PASSWORD.decode());self.assertNotEqual(p.returncode,0);self.assertNotIn(PASSWORD,p.stdout+p.stderr)
 def start_supervisor(self,password=PASSWORD,confirm=True,interval=1,script_text=None):
  sock=str(self.path/'export.sock');script=self.path/'child.sh'
  if script_text is None:
   script_text='''#!/bin/sh
exec python3 -u - <<'PYCODE'
import socket,os,signal,json,struct
s=socket.socket(fileno=3)
def stop(*_):
 s.close();raise SystemExit(0)
signal.signal(signal.SIGTERM,stop)
while True:
 try:
  request=s.recv(1)
 except BlockingIOError:
  import time;time.sleep(.01);continue
 if not request:break
 data=b'{"schema":"IGW_WEB_R10","session":"11111111111111111111111111111111","uptime_ms":100,"fixture":true}'
 s.sendall(struct.pack('>Q',len(data))+data)
PYCODE
'''
  script.write_text(script_text);read,write=os.pipe();os.write(write,password+b'\n'+(password+b'\n' if confirm else b''));os.close(write)
  log=self.path/('log-'+secrets.token_hex(3));handle=log.open('wb');p=subprocess.Popen([self.binary,'serve','--vault',str(self.path/'vault'),'--run-script',str(script),'--interval',str(interval),'--pass-fd',str(read)],pass_fds=(read,),stdout=handle,stderr=subprocess.STDOUT);os.close(read);handle.close();self.processes.append(p);return p,log
 def wait_saved(self,p,log,count=1):
  until=time.monotonic()+12
  while time.monotonic()<until:
   if log.read_text().count('ARCHIVE_SAVED')>=count:return
   if p.poll() is not None:self.fail(log.read_text())
   time.sleep(.05)
  self.fail('archive timeout '+log.read_text())
 def test_supervisor_periodic_archive_final_stop_and_reopen_vault(self):
  p,log=self.start_supervisor();self.wait_saved(p,log,2);p.terminate();self.assertEqual(p.wait(15),0,log.read_text());files=list((self.path/'vault').glob('igw-*.igwe'));self.assertGreaterEqual(len(files),3)
  for f in files:self.assertTrue(json.loads(self.independent_open(f.read_bytes()))['fixture'])
  self.assertFalse((self.path/'export.sock').exists());self.assertNotIn(PASSWORD.decode(),log.read_text())
  p2,log2=self.start_supervisor(confirm=False);self.wait_saved(p2,log2);p2.terminate();self.assertEqual(p2.wait(15),0,log2.read_text());self.assertIn('VAULT_UNLOCK=PASS',log2.read_text())
 def test_existing_vault_wrong_password_does_not_launch_child(self):
  p,log=self.start_supervisor();self.wait_saved(p,log);p.terminate();p.wait(15)
  marker=self.path/'should_not_exist';p2,log2=self.start_supervisor(password=b'Wrong-vault-passphrase',confirm=False,script_text=f'#!/bin/sh\ntouch "{marker}"\n');self.assertNotEqual(p2.wait(10),0);self.assertFalse(marker.exists())
 def test_second_supervisor_refuses_active_vault_lock(self):
  p,log=self.start_supervisor();self.wait_saved(p,log);p2,log2=self.start_supervisor(confirm=False);self.assertNotEqual(p2.wait(5),0);self.assertIn('vault_directory_or_lock',log2.read_text());self.assertIsNone(p.poll())
 def test_child_failure_causes_nonzero_supervisor_exit(self):
  p,log=self.start_supervisor(script_text='#!/bin/sh\nexit 7\n');self.assertNotEqual(p.wait(10),0,log.read_text());self.assertIn('RESULT=REVIEW',log.read_text())
if __name__=='__main__':unittest.main(verbosity=2)
