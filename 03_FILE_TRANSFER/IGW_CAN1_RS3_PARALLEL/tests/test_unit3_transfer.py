import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

class Unit3Transfer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='igw3_build_')
        cls.client=Path(cls.tmp.name)/'rs'
        cls.fixture=Path(cls.tmp.name)/'fixture'
        common=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        subprocess.run(common+['-DRSF_TEST_TRANSPORT',str(ROOT/'host/src/igw_rs485_file.c'),'-o',str(cls.client)],check=True)
        subprocess.run(common+['-I'+str(ROOT/'host/tests/stubs'),'-I'+str(ROOT/'host/tests/vendor_ff'),'-I'+str(ROOT/'zynq/src'),
                       str(ROOT/'tests/fixture3.c'),str(ROOT/'host/tests/vendor_ff/ff.c'),
                       str(ROOT/'host/tests/vendor_ff/ffunicode.c'),'-o',str(cls.fixture)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='igw3_case_');self.root=Path(self.tmp.name)
        a,b=os.pipe();c,d=os.pipe();self.w,self.r=b,c
        self.server=subprocess.Popen([str(self.fixture),str(a),str(d),str(self.root/'disk.img')],pass_fds=(a,d),stderr=subprocess.PIPE,env=ENV)
        os.close(a);os.close(d)
    def tearDown(self):
        os.close(self.w);os.close(self.r)
        _,err=self.server.communicate(timeout=30)
        self.assertEqual(self.server.returncode,0,err.decode(errors='replace'))
        self.tmp.cleanup()
    def cli(self,*args):
        result=subprocess.run([str(self.client),'--test-read-fd',str(self.r),'--test-write-fd',str(self.w),
                               '--unit','3','--baud','9600',*map(str,args)],
                              pass_fds=(self.r,self.w),capture_output=True,text=True,timeout=120,env=ENV)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('RESULT=PASS_RS485_FILE unit=3',result.stdout)
        return result.stdout
    def test_upload_and_download_one_mib(self):
        data=bytes((i*137+(i>>7)*21)&255 for i in range(1048576))
        source=self.root/'input.bin';source.write_bytes(data)
        self.assertIn(hashlib.sha256(data).hexdigest(),self.cli('put',source))
        output=self.root/'out.bin';self.cli('get',output);self.assertEqual(output.read_bytes(),data)
    def test_device_generate_and_download(self):
        output=self.root/'generated.bin';result=self.cli('generate','4096',output)
        self.assertEqual(output.stat().st_size,4096)
        self.assertIn(hashlib.sha256(output.read_bytes()).hexdigest(),result)

if __name__=='__main__': unittest.main(verbosity=2)
