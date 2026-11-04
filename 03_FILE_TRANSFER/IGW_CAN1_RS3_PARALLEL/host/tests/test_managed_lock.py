import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ManagedLock(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='igw_managed_');cls.root=Path(cls.tmp.name);cls.lock=cls.root/'rs_lock'
  define='-DIGW_RS_LOCK="'+str(cls.lock)+'"'
  for source,name in [('tests/managed_lock_probe.c','probe'),('src/igw_can_block.c','can'),('src/igw_rs485_file.c','rs')]:
   subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror',define,str(ROOT/source),'-o',str(cls.root/name)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def setUp(self):
  self.lock.mkdir(mode=0o700);self.fd=os.open(self.lock,os.O_RDONLY|os.O_DIRECTORY)
 def tearDown(self):
  os.close(self.fd)
  if self.lock.is_symlink():self.lock.unlink()
  elif self.lock.exists():self.lock.rmdir()
 def probe(self,value=None,fd=True):
  env=os.environ.copy();env.pop('IGW_MANAGED_RS485_FD',None)
  if value is not None:env['IGW_MANAGED_RS485_FD']=value
  r=subprocess.run([str(self.root/'probe')],env=env,pass_fds=(self.fd,) if fd else (),capture_output=True,text=True,check=True)
  return int(r.stdout.strip())
 def test_capability_validation(self):
  self.assertEqual(self.probe(),0);self.assertEqual(self.probe(str(self.fd)),1)
  for v in ('','0','abc','99999999999999999999',str(self.fd)):
   self.assertEqual(self.probe(v,fd=False),-1,v)
  os.chmod(self.lock,0o755);self.assertEqual(self.probe(str(self.fd)),-1);os.chmod(self.lock,0o700)
  self.lock.rmdir();self.lock.mkdir(mode=0o700);self.assertEqual(self.probe(str(self.fd)),-1)
 def test_backends_require_managed_owner_and_preserve_it_on_hardware_failure(self):
  if os.geteuid()!=0:self.skipTest('hardware backend guard requires root')
  for name,args,available in [('can',['--unit','1','get',str(self.root/'canout')],Path('/sys/class/net/can0').exists()),
                              ('rs',['--unit','3','--baud','9600','get',str(self.root/'rsout')],Path('/dev/mem').exists())]:
   if available:continue # never touch available real hardware
   with self.subTest(backend=name):
    env=os.environ.copy();env.pop('IGW_MANAGED_RS485_FD',None)
    p=subprocess.run([str(self.root/name),*args],env=env,capture_output=True,text=True)
    self.assertNotEqual(p.returncode,0);self.assertIn('owner lock',p.stderr)
    env['IGW_MANAGED_RS485_FD']='invalid'
    p=subprocess.run([str(self.root/name),*args],env=env,capture_output=True,text=True)
    self.assertNotEqual(p.returncode,0);self.assertIn('Invalid managed RS485 owner',p.stderr)
    env['IGW_MANAGED_RS485_FD']=str(self.fd)
    p=subprocess.run([str(self.root/name),*args],env=env,pass_fds=(self.fd,),capture_output=True,text=True)
    self.assertNotEqual(p.returncode,0);self.assertIn('CAN connect' if name=='can' else 'devmem',p.stderr)
    self.assertTrue(self.lock.is_dir())

if __name__=='__main__':unittest.main(verbosity=2)
