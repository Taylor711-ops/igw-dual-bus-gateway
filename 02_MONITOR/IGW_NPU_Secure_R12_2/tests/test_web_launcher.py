"""Shell launcher tests with fake MMIO/ip/services and temporary /proc inventory.
These tests never configure a real CAN interface or access physical memory.
"""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.mockbin = self.base/'bin'
        self.deploy = self.base/'deploy'
        self.state = self.base/'state'
        for path in [self.mockbin,self.deploy,self.state,self.base/'proc/1']:
            path.mkdir(parents=True,exist_ok=True)
        script = (ROOT/'deploy/run_web_gateway.sh').read_text()
        script = script.replace('/tmp/igw_',str(self.state/'igw_'))
        script = script.replace('/etc/init.d/S95igw_gateway',str(self.mockbin/'service'))
        script = script.replace('/proc/[0-9]*/comm',str(self.base/'proc/[0-9]*/comm'))
        self.launcher = self.deploy/'run_web_gateway.sh'
        self.launcher.write_text(script)
        self.write_exec(self.mockbin/'devmem', '#!/bin/sh\ncase "$1" in\n'
                        '0xf8800200) echo 0x00000005;;\n'
                        '0x1f0000020) echo 0x000000BF;;\n'
                        '0x8040000C) echo 0x00000000;;\n*) exit 2;;\nesac\n')
        self.write_exec(self.mockbin/'ip', '#!/bin/sh\n'
                        'if [ "$1" = -4 ]; then echo "inet 192.168.31.222/24"; fi\n')
        self.write_exec(self.mockbin/'service',f'#!/bin/sh\necho stopped > "{self.state}/service"\n')
        real_sha = shutil.which('sha256sum')
        self.write_exec(self.mockbin/'sha256sum', '#!/bin/sh\n'
                        'if [ "$1" = /mnt/mmcblk0p1/BOOT.bin ]; then\n'
                        'echo "7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c BOOT.bin"\n'
                        f'else exec "{real_sha}" "$@"; fi\n')
        for name in ['igw_can_udp','igw_rs485_rw','igw_web','run_npu_local.sh']:
            self.write_exec(self.deploy/name, '#!/usr/bin/env python3\n'
                            'import signal,time,os\n'
                            f'open({str(self.state/(name+".pid"))!r},"w").write(str(os.getpid()))\n'
                            'signal.signal(signal.SIGTERM,lambda *_:exit(0))\n' +
                            ('print("NPU_LOCAL_READY MOCK",flush=True)\n' if name=='run_npu_local.sh' else 'print("[READY] MOCK",flush=True)\n') +
                            'while True: time.sleep(.02)\n')
        (self.deploy/'run_npu_local.sh').rename(self.deploy/'npu_mock.py')
        self.write_exec(self.deploy/'run_npu_local.sh','#!/bin/sh\nexec python3 "$(dirname "$0")/npu_mock.py"\n')
        (self.deploy/'build_info.txt').write_text('TEST_ONLY\n')
        self.hashes()
        self.env = dict(os.environ, IGW_ARCHIVE_FD="3", PATH=str(self.mockbin)+os.pathsep+os.environ['PATH'])
        self.output = self.base/'output.txt'
        self.proc = None

    @staticmethod
    def write_exec(path,text):
        path.write_text(text)
        path.chmod(0o755)

    def hashes(self):
        result = subprocess.run(['sha256sum','igw_can_udp','igw_rs485_rw','igw_web',
                                 'run_web_gateway.sh','run_npu_local.sh','npu_mock.py','build_info.txt'],cwd=self.deploy,
                                 check=True,capture_output=True,text=True)
        (self.deploy/'SHA256SUMS.txt').write_text(result.stdout)

    def run_sync(self):
        return subprocess.run(['sh',str(self.launcher)],env=self.env,
                              capture_output=True,text=True,timeout=7)

    def start(self):
        self.handle = self.output.open('w')
        self.proc = subprocess.Popen(['sh',str(self.launcher)],env=self.env,
                                     stdout=self.handle,stderr=subprocess.STDOUT,start_new_session=True)
        deadline = time.monotonic()+4
        while time.monotonic()<deadline:
            if 'R12 NPU + SECURE WEB READY' in self.output.read_text():
                return
            if self.proc.poll() is not None:
                self.fail(self.output.read_text())
            time.sleep(.02)
        self.fail('launcher not ready: '+self.output.read_text())

    def tearDown(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(6)
        if hasattr(self,'handle'):
            self.handle.close()
        for path in self.state.glob('*.pid'):
            pid = int(path.read_text())
            try:
                os.kill(pid,signal.SIGTERM)
            except ProcessLookupError:
                pass
        self.temp.cleanup()

    def test_signal_cleanup_stops_all_four_owned_children(self):
        self.start()
        self.proc.terminate()
        self.assertEqual(self.proc.wait(6),143)
        self.handle.flush()
        text = self.output.read_text()
        self.assertIn('FINAL_TX_AUTHORIZATION=0x00000000',text)
        self.assertFalse((self.state/'igw_web_r10.lock').exists())
        for path in self.state.glob('*.pid'):
            with self.assertRaises(ProcessLookupError):
                os.kill(int(path.read_text()),0)

    def test_existing_rs_owner_is_not_stopped_or_unlocked(self):
        lock = self.state/'igw_rs485_once.lock'
        lock.mkdir()
        result = self.run_sync()
        self.assertNotEqual(result.returncode,0)
        self.assertIn('RS485 owned',result.stdout)
        self.assertTrue(lock.exists())
        self.assertFalse((self.state/'service').exists())

    def test_bad_checksum_stops_before_mutation(self):
        (self.deploy/'run_npu_local.sh').rename(self.deploy/'npu_mock.py')
        self.write_exec(self.deploy/'run_npu_local.sh','#!/bin/sh\nexec python3 "$(dirname "$0")/npu_mock.py"\n')
        (self.deploy/'build_info.txt').write_text('changed')
        result = self.run_sync()
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.state/'service').exists())
        self.assertFalse((self.state/'igw_web_r10.lock').exists())

    def test_old_manual_can_owner_is_refused(self):
        (self.base/'proc/1/comm').write_text('igw_can_udp\n')
        result = self.run_sync()
        self.assertNotEqual(result.returncode,0)
        self.assertIn('existing process igw_can_udp',result.stdout)
        self.assertFalse(list(self.state.glob('*.pid')))

    def test_child_startup_failure_cleans_companion(self):
        self.write_exec(self.deploy/'igw_can_udp','#!/bin/sh\nexit 2\n')
        self.hashes()
        result = self.run_sync()
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.state/'igw_web_r10.lock').exists())
        for path in self.state.glob('*.pid'):
            with self.assertRaises(ProcessLookupError):
                os.kill(int(path.read_text()),0)

    def test_web_startup_failure_cleans_both_backends(self):
        self.write_exec(self.deploy/'igw_web','#!/bin/sh\nexit 2\n')
        self.hashes()
        result = self.run_sync()
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.state/'igw_web_r10.lock').exists())
        for path in self.state.glob('*.pid'):
            with self.assertRaises(ProcessLookupError):
                os.kill(int(path.read_text()),0)



    def test_group_term_then_parent_term_finishes_web_cleanup(self):
        marker=self.state/'cleanup_entered'
        script=self.launcher.read_text().replace('    igw_stuck=0',f'    : > "{marker}"\n    /bin/sleep .25\n    igw_stuck=0')
        self.launcher.write_text(script);self.hashes();self.start()
        os.killpg(self.proc.pid,signal.SIGTERM)
        until=time.monotonic()+2
        while time.monotonic()<until and not marker.exists():time.sleep(.005)
        self.assertTrue(marker.exists())
        self.proc.terminate()
        self.assertEqual(self.proc.wait(6),143)
        self.assertFalse((self.state/'igw_web_r10.lock').exists())
        for path in self.state.glob('*.pid'):
            with self.assertRaises(ProcessLookupError):os.kill(int(path.read_text()),0)


if __name__ == '__main__':
    unittest.main(verbosity=2)

